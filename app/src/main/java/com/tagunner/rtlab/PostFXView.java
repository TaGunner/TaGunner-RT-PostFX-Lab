package com.tagunner.rtlab;

import android.content.Context;
import android.graphics.Bitmap;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.GLUtils;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public final class PostFXView extends GLSurfaceView {
  private final RendererFx renderer=new RendererFx();
  public PostFXView(Context c) {
    super(c);setEGLContextClientVersion(3);setPreserveEGLContextOnPause(true);
    setRenderer(renderer);setRenderMode(RENDERMODE_WHEN_DIRTY);
  }
  public void image(Bitmap b){renderer.bitmap=b;renderer.newImage=b;requestRender();}
  public void parameters(float s,float v,float b,float e,float cmp){
    renderer.sharp=s;renderer.vibrance=v;renderer.bloom=b;renderer.exposure=e;renderer.compare=cmp;requestRender();
  }
  private static final class RendererFx implements GLSurfaceView.Renderer {
    private final String vs="#version 300 es\nlayout(location=0) in vec2 p;layout(location=1) in vec2 uv;out vec2 vUV;void main(){gl_Position=vec4(p,0.,1.);vUV=uv;}";
    private final String fs="#version 300 es\nprecision highp float;in vec2 vUV;out vec4 color;uniform sampler2D tex;uniform vec2 stepTex;uniform float sharp,vibrance,bloom,exposure,compare;float lum(vec3 v){return dot(v,vec3(.2126,.7152,.0722));}void main(){vec3 src=texture(tex,vUV).rgb;if(vUV.x<compare){color=vec4(src,1.);return;}vec3 n=texture(tex,vUV+vec2(0.,stepTex.y)).rgb;vec3 s=texture(tex,vUV-vec2(0.,stepTex.y)).rgb;vec3 e=texture(tex,vUV+vec2(stepTex.x,0.)).rgb;vec3 w=texture(tex,vUV-vec2(stepTex.x,0.)).rgb;vec3 c=clamp(src+(src*4.-n-s-e-w)*sharp*.35,0.,1.);float l=lum(c);c=clamp(mix(vec3(l),c,1.+vibrance*(1.-max(c.r,max(c.g,c.b)))),0.,1.);vec3 glow=vec3(0.);for(int i=0;i<8;i++){float a=6.2831853*float(i)/8.;vec3 v=texture(tex,vUV+vec2(cos(a),sin(a))*stepTex*5.).rgb;glow+=v*max(lum(v)-.65,0.);}c=clamp(c+glow*bloom*.18,0.,1.);c=clamp(c*exposure,0.,1.);if(compare>0.&&abs(vUV.x-compare)<.002)c=vec3(.15,.95,1.);color=vec4(c,1.);}";
    volatile Bitmap bitmap,newImage;
    volatile float sharp=.3f,vibrance=.3f,bloom=.2f,exposure=1f,compare=.5f;
    private int program,texture,vao,vbo,screenW=1,screenH=1,imageW=1,imageH=1;
    private FloatBuffer vertices;
    private int compile(int t,String code){
      int sh=GLES30.glCreateShader(t);GLES30.glShaderSource(sh,code);GLES30.glCompileShader(sh);
      int[] ok=new int[1];GLES30.glGetShaderiv(sh,GLES30.GL_COMPILE_STATUS,ok,0);
      if(ok[0]==0)throw new RuntimeException(GLES30.glGetShaderInfoLog(sh));return sh;
    }
    @Override public void onSurfaceCreated(GL10 gl,EGLConfig config){
      newImage=bitmap;
      int v=compile(GLES30.GL_VERTEX_SHADER,vs),f=compile(GLES30.GL_FRAGMENT_SHADER,fs);
      program=GLES30.glCreateProgram();GLES30.glAttachShader(program,v);GLES30.glAttachShader(program,f);GLES30.glLinkProgram(program);
      int[] linked=new int[1];GLES30.glGetProgramiv(program,GLES30.GL_LINK_STATUS,linked,0);
      if(linked[0]==0)throw new RuntimeException(GLES30.glGetProgramInfoLog(program));
      GLES30.glDeleteShader(v);GLES30.glDeleteShader(f);
      float[] q={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
      ByteBuffer bb=ByteBuffer.allocateDirect(q.length*4).order(ByteOrder.nativeOrder());
      vertices=bb.asFloatBuffer();vertices.put(q).position(0);
      int[] ids=new int[1];GLES30.glGenVertexArrays(1,ids,0);vao=ids[0];GLES30.glBindVertexArray(vao);
      GLES30.glGenBuffers(1,ids,0);vbo=ids[0];GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,vbo);
      GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER,q.length*4,vertices,GLES30.GL_STATIC_DRAW);
      GLES30.glEnableVertexAttribArray(0);GLES30.glVertexAttribPointer(0,2,GLES30.GL_FLOAT,false,16,0);
      GLES30.glEnableVertexAttribArray(1);GLES30.glVertexAttribPointer(1,2,GLES30.GL_FLOAT,false,16,8);
      GLES30.glBindVertexArray(0);
      GLES30.glGenTextures(1,ids,0);texture=ids[0];GLES30.glBindTexture(GLES30.GL_TEXTURE_2D,texture);
      GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_MIN_FILTER,GLES30.GL_LINEAR);
      GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_MAG_FILTER,GLES30.GL_LINEAR);
      GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_WRAP_S,GLES30.GL_CLAMP_TO_EDGE);
      GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_WRAP_T,GLES30.GL_CLAMP_TO_EDGE);
    }
    @Override public void onSurfaceChanged(GL10 gl,int w,int h){screenW=w;screenH=h;}
    @Override public void onDrawFrame(GL10 gl){
      GLES30.glViewport(0,0,screenW,screenH);
      GLES30.glClearColor(.04f,.05f,.1f,1f);GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
      Bitmap b=newImage;
      if(b!=null){newImage=null;GLES30.glBindTexture(GLES30.GL_TEXTURE_2D,texture);GLUtils.texImage2D(GLES30.GL_TEXTURE_2D,0,b,0);imageW=b.getWidth();imageH=b.getHeight();}
      if(program==0||imageW<2)return;
      float aspect=(float)imageW/imageH,frame=(float)screenW/screenH;
      int w=screenW,h=screenH;if(frame>aspect)w=Math.max(1,(int)(h*aspect));else h=Math.max(1,(int)(w/aspect));
      GLES30.glViewport((screenW-w)/2,(screenH-h)/2,w,h);
      GLES30.glUseProgram(program);GLES30.glActiveTexture(GLES30.GL_TEXTURE0);GLES30.glBindTexture(GLES30.GL_TEXTURE_2D,texture);
      GLES30.glUniform1i(GLES30.glGetUniformLocation(program,"tex"),0);
      GLES30.glUniform2f(GLES30.glGetUniformLocation(program,"stepTex"),1f/imageW,1f/imageH);
      GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"sharp"),sharp);
      GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"vibrance"),vibrance);
      GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"bloom"),bloom);
      GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"exposure"),exposure);
      GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"compare"),compare);
      GLES30.glBindVertexArray(vao);GLES30.glDrawArrays(GLES30.GL_TRIANGLES,0,6);GLES30.glBindVertexArray(0);
    }
  }
}
