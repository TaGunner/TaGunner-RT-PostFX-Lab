package com.tagunner.rtlab;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.net.Uri;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.InputStream;

public final class MainActivity extends Activity {
  static { System.loadLibrary("tagunner_rt"); }
  private static native String inspectVulkan();
  private static native int[] renderRayQueryScene(android.content.res.AssetManager assets,int width,int height);
  private static native String benchmarkRayQuery(android.content.res.AssetManager assets,int width,int height,int passes);
  private static native int[] renderLightFrame(android.content.res.AssetManager assets,int phase);
  private TextView report,tip;
  private LinearLayout scanPanel,fxPanel;
  private PostFXView fx;
  private boolean fxOpen=false,compare=true;
  private float sharp=.3f,vibrance=.3f,bloom=.2f,exposure=1f;
  private int dp(int v){return Math.round(v*getResources().getDisplayMetrics().density);}
  private Button button(String text){Button b=new Button(this);b.setAllCaps(false);b.setText(text);return b;}
  private TextView text(String content,int size,int color){
    TextView v=new TextView(this);v.setText(content);v.setTextSize(size);v.setTextColor(color);
    v.setPadding(dp(4),dp(6),dp(4),dp(6));return v;
  }
  private void controls(LinearLayout parent,String label,int initial,java.util.function.IntConsumer callback){
    parent.addView(text(label,13,Color.LTGRAY));
    SeekBar s=new SeekBar(this);s.setMax(100);s.setProgress(initial);parent.addView(s);
    s.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
      public void onProgressChanged(SeekBar b,int v,boolean fromUser){if(fromUser){callback.accept(v);updateFx();}}
      public void onStartTrackingTouch(SeekBar b){}
      public void onStopTrackingTouch(SeekBar b){}
    });
  }
  private void updateFx(){if(fx!=null)fx.parameters(sharp,vibrance,bloom,exposure,compare?.5f:0f);}
  @Override public void onCreate(Bundle bundle){
    super.onCreate(bundle);
    getWindow().setStatusBarColor(0xff090d1b);getWindow().setNavigationBarColor(0xff090d1b);
    LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(dp(14),dp(8),dp(14),dp(12));root.setBackgroundColor(0xff090d1b);
    // Android 15/16 enforces edge-to-edge for target SDK 35. Respect system bars.
    if (android.os.Build.VERSION.SDK_INT >= 30) {
      root.setOnApplyWindowInsetsListener((view, insets) -> {
        android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars());
        root.setPadding(dp(14), dp(8) + bars.top, dp(14), dp(12) + bars.bottom);
        return insets;
      });
    }
    root.addView(text("TaGunner  |  RT + PostFX Lab",22,0xff9eceff));
    root.addView(text("v0.7  •  Offline  •  Moving light Ray Query + GPU PostFX",12,0xff8aabc7));
    LinearLayout tabs=new LinearLayout(this);tabs.setOrientation(0);
    Button vkBtn=button("Vulkan / Ray Tracing"),fxBtn=button("GPU PostFX");
    tabs.addView(vkBtn,new LinearLayout.LayoutParams(0,dp(50),1f));
    tabs.addView(fxBtn,new LinearLayout.LayoutParams(0,dp(50),1f));root.addView(tabs);
    scanPanel=new LinearLayout(this);scanPanel.setOrientation(1);
    scanPanel.addView(text("นี่คือการตรวจความสามารถ Vulkan จาก GPU จริง ไม่ใช่การเปิด RTX ใน Roblox",13,0xffd9e5f8));
    Button scan=button("ตรวจ GPU + ทดลองเปิด Ray Query Device");scanPanel.addView(scan);
    Button copy=button("คัดลอกผลตรวจส่งแชต");scanPanel.addView(copy);
    Button scene=button("เรนเดอร์ 3D ด้วย Hardware Ray Query");
    scanPanel.addView(scene);
    TextView sceneStatus=text("ทดลองสร้าง BLAS/TLAS แล้วยิงรังสีสร้างภาพบน GPU จริง",12,0xffb1c9e4);
    scanPanel.addView(sceneStatus);
    scanPanel.addView(text("RT Benchmark • ชาร์จแบตให้เพียงพอ และพักเครื่องถ้าร้อนเกินไป",12,0xffffd6a5));
    Button benchLow=button("Benchmark 480×300 • 16 เฟรม");
    Button benchHigh=button("Benchmark 720p • 8 เฟรม");
    Button bench1080=button("Benchmark 1080p • 4 เฟรม");
    scanPanel.addView(benchLow);
    scanPanel.addView(benchHigh);
    scanPanel.addView(bench1080);
    TextView benchStatus=text("ยังไม่ได้วัด GPU • ตัวเลขที่ได้เป็นเวลา Dispatch ของฉากนิ่ง ไม่ใช่ FPS ของ Roblox",12,0xffd9f6cf);
    benchStatus.setTextIsSelectable(true);
    scanPanel.addView(benchStatus);
    android.widget.ImageView sceneImage=new android.widget.ImageView(this);
    sceneImage.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
    scanPanel.addView(sceneImage,new LinearLayout.LayoutParams(-1,dp(236)));
    Button lightA=button("แสงมุม A / Shadow 0");
    Button lightB=button("แสงมุม B / Shadow 7");
    scanPanel.addView(lightA);
    scanPanel.addView(lightB);
    java.util.function.IntConsumer showLight=(phase)->{
      lightA.setEnabled(false);lightB.setEnabled(false);
      sceneStatus.setText("GPU Ray Query กำลังเรนเดอร์เงาจากแสงมุม "+phase+"...");
      new Thread(()->{
        android.graphics.Bitmap bitmap=null;
        String status;
        try {
          final int w=480,h=300;
          int[] pixels=renderLightFrame(getAssets(),phase);
          if(pixels==null||pixels.length!=w*h)throw new IllegalStateException("Invalid scene image");
          bitmap=android.graphics.Bitmap.createBitmap(pixels,w,h,android.graphics.Bitmap.Config.ARGB_8888);
          status="Ray-traced shadow / lighting phase "+phase+" — SUCCESS";
        }catch(Throwable e){status="Render ERROR: "+e.getMessage();}
        final android.graphics.Bitmap result=bitmap;
        final String message=status;
        runOnUiThread(()->{
          if(result!=null)sceneImage.setImageBitmap(result);
          sceneStatus.setText(message);
          lightA.setEnabled(true);lightB.setEnabled(true);
        });
      }).start();
    };
    lightA.setOnClickListener(v->showLight.accept(0));
    lightB.setOnClickListener(v->showLight.accept(7));
    ScrollView scroll=new ScrollView(this);scanPanel.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
    report=text("กดตรวจ GPU เพื่ออ่าน Vulkan Extensions / Features / การสร้างอุปกรณ์ RT",13,0xffb3ffdc);
    report.setTypeface(Typeface.MONOSPACE);report.setTextIsSelectable(true);
    scroll.addView(report);
    root.addView(scanPanel,new LinearLayout.LayoutParams(-1,0,1));
    fxPanel=new LinearLayout(this);fxPanel.setOrientation(1);
    Button pick=button("เลือกภาพ Roblox จากมือถือ");fxPanel.addView(pick);
    Button toggle=button("เปรียบเทียบ Before / After");fxPanel.addView(toggle);
    tip=text("GPU PostFX ทำงานกับภาพนิ่ง ไม่ได้ฉีด Shader เข้าเกม Roblox",12,0xffbbcbdd);fxPanel.addView(tip);
    fx=new PostFXView(this);
    fxPanel.addView(fx,new LinearLayout.LayoutParams(-1,dp(255)));
    ScrollView sliders=new ScrollView(this);LinearLayout slidersBody=new LinearLayout(this);slidersBody.setOrientation(1);
    controls(slidersBody,"Sharpen",30,x->sharp=x/80f);
    controls(slidersBody,"Vibrance",30,x->vibrance=x/70f);
    controls(slidersBody,"Bloom",20,x->bloom=x/55f);
    controls(slidersBody,"Exposure",50,x->exposure=.5f+x/100f);
    sliders.addView(slidersBody);
    fxPanel.addView(sliders,new LinearLayout.LayoutParams(-1,dp(205)));
    fxPanel.setVisibility(View.GONE);
    root.addView(fxPanel,new LinearLayout.LayoutParams(-1,0,1));
    setContentView(root);
    vkBtn.setOnClickListener(v->showFx(false));fxBtn.setOnClickListener(v->showFx(true));
    java.util.concurrent.atomic.AtomicBoolean benchmarking=new java.util.concurrent.atomic.AtomicBoolean(false);
    java.util.function.IntConsumer doBenchmark=(preset)->{
      if(!benchmarking.compareAndSet(false,true))return;
      benchLow.setEnabled(false);benchHigh.setEnabled(false);bench1080.setEnabled(false);
      final int w=preset==0?480:(preset==1?1280:1920);
      final int h=preset==0?300:(preset==1?720:1080);
      final int count=preset==0?16:(preset==1?8:4);
      benchStatus.setText("Testing "+w+"x"+h+" / "+count+" dispatches ...");
      new Thread(()->{
        String result;
        try{result=benchmarkRayQuery(getAssets(),w,h,count);}
        catch(Throwable err){result="Benchmark ERROR: "+err.getClass().getSimpleName()+": "+err.getMessage();}
        final String textResult=result;
        runOnUiThread(()->{
          benchStatus.setText(textResult);
          benchLow.setEnabled(true);benchHigh.setEnabled(true);bench1080.setEnabled(true);
          benchmarking.set(false);
        });
      }).start();
    };
    benchLow.setOnClickListener(v->doBenchmark.accept(0));
    benchHigh.setOnClickListener(v->doBenchmark.accept(1));
    bench1080.setOnClickListener(v->doBenchmark.accept(2));
    scene.setOnClickListener(v->{
      scene.setEnabled(false);
      sceneStatus.setText("กำลังสร้าง Acceleration Structures และเรนเดอร์ด้วย Vulkan...");
      new Thread(()->{
        final int w=480,h=300;
        android.graphics.Bitmap bitmap=null;
        String status;
        try{
          int[] pixels=renderRayQueryScene(getAssets(),w,h);
          if(pixels==null||pixels.length!=w*h)throw new IllegalStateException("Invalid GPU image size");
          bitmap=android.graphics.Bitmap.createBitmap(pixels,w,h,android.graphics.Bitmap.Config.ARGB_8888);
          status="Hardware Ray Query render SUCCESS: "+w+" × "+h+" (BLAS + TLAS + rayQueryEXT)";
        }catch(Throwable err){
          status="Hardware Ray Query renderer FAILED: "+err.getClass().getSimpleName()+": "+err.getMessage();
        }
        final android.graphics.Bitmap b=bitmap;
        final String t=status;
        runOnUiThread(()->{
          if(b!=null)sceneImage.setImageBitmap(b);
          sceneStatus.setText(t);
          scene.setEnabled(true);
        });
      }).start();
    });
    scan.setOnClickListener(v->{
      scan.setEnabled(false);report.setText("Scanning Vulkan...");
      new Thread(()->{
        String result;
        try { result=inspectVulkan(); } catch(Throwable t){result="Vulkan native probe failed: "+t; }
        final String txt=result;
        runOnUiThread(()->{report.setText(txt);scan.setEnabled(true);});
      }).start();
    });
    copy.setOnClickListener(v->{
      ClipboardManager cb=(ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
      cb.setPrimaryClip(ClipData.newPlainText("TaGunner Vulkan + Benchmark Report",report.getText()+"\n\n"+benchStatus.getText()));
    });
    pick.setOnClickListener(v->{
      Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);
      i.setType("image/*");startActivityForResult(i,11);
    });
    toggle.setOnClickListener(v->{compare=!compare;toggle.setText(compare?"Before / After: ON":"Before / After: OFF");updateFx();});
    Bitmap demo=Bitmap.createBitmap(800,500,Bitmap.Config.ARGB_8888);
    Canvas canvas=new Canvas(demo);Paint p=new Paint(3);
    for(int y=0;y<500;y+=2){p.setColor(Color.rgb(15+y/20,25+y/12,48+y/8));canvas.drawRect(0,y,800,y+2,p);}
    p.setColor(0xfff5c86f);p.setTextSize(45);canvas.drawText("TaGunner GPU Preview",45,165,p);
    p.setColor(0xff76baf4);canvas.drawRect(60,230,740,245,p);
    fx.image(demo);updateFx();fx.onPause();
  }
  private void showFx(boolean enable){
    scanPanel.setVisibility(enable?View.GONE:View.VISIBLE);
    fxPanel.setVisibility(enable?View.VISIBLE:View.GONE);
    fxOpen=enable;
    if(enable){fx.onResume();fx.requestRender();}else fx.onPause();
  }
  @Override protected void onResume(){super.onResume();if(fx!=null && fxOpen)fx.onResume();}
  @Override protected void onPause(){if(fx!=null && fxOpen)fx.onPause();super.onPause();}
  @Override protected void onActivityResult(int req,int result,Intent data){
    super.onActivityResult(req,result,data);
    if(req!=11||result!=RESULT_OK||data==null||data.getData()==null)return;
    try{
      Uri uri=data.getData();BitmapFactory.Options meta=new BitmapFactory.Options();meta.inJustDecodeBounds=true;
      try(InputStream in=getContentResolver().openInputStream(uri)){BitmapFactory.decodeStream(in,null,meta);}
      BitmapFactory.Options opt=new BitmapFactory.Options();opt.inSampleSize=1;
      while(Math.max(meta.outWidth/opt.inSampleSize,meta.outHeight/opt.inSampleSize)>2048)opt.inSampleSize*=2;
      try(InputStream in=getContentResolver().openInputStream(uri)){
        Bitmap bmp=BitmapFactory.decodeStream(in,null,opt);
        if(bmp==null)throw new IllegalArgumentException("Cannot decode image");
        fx.image(bmp);tip.setText("Image "+bmp.getWidth()+"×"+bmp.getHeight()+" | GLSL PostFX");
      }
    } catch(Exception e){tip.setText("Read image failed: "+e.getMessage());}
  }
}
