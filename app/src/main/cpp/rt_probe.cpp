#include <jni.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static std::string ver(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v))+"."+std::to_string(VK_API_VERSION_MINOR(v))+"."+std::to_string(VK_API_VERSION_PATCH(v));
}
static const char* yn(bool b) { return b?"YES":"NO"; }
static std::string probe() {
  std::ostringstream o;
  o<<"TaGunner RT + PostFX Lab / Android Vulkan\n";
  o<<"Driver capability test - does NOT activate RTX in Roblox.\n\n";
  uint32_t loader=VK_API_VERSION_1_0;
  auto getInstanceVersion=(PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE,"vkEnumerateInstanceVersion");
  if(getInstanceVersion)getInstanceVersion(&loader);
  o<<"Vulkan loader "<<ver(loader)<<"\n";
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName="TaGunner RT Lab";app.apiVersion=(loader>=VK_API_VERSION_1_1)?VK_API_VERSION_1_1:VK_API_VERSION_1_0;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
  VkInstance inst=VK_NULL_HANDLE;
  VkResult ret=vkCreateInstance(&ici,nullptr,&inst);
  if(ret!=VK_SUCCESS) { o<<"vkCreateInstance failed "<<ret<<"\n";return o.str(); }
  uint32_t count=0;ret=vkEnumeratePhysicalDevices(inst,&count,nullptr);
  if(ret!=VK_SUCCESS||!count) { o<<"No Vulkan GPU. VkResult="<<ret<<"\n";vkDestroyInstance(inst,nullptr);return o.str(); }
  std::vector<VkPhysicalDevice> devs(count);
  vkEnumeratePhysicalDevices(inst,&count,devs.data());
  auto getFeatures2=(PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(inst,"vkGetPhysicalDeviceFeatures2");
  for (auto gpu:devs) {
    VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(gpu,&p);
    VkPhysicalDeviceFeatures base{};vkGetPhysicalDeviceFeatures(gpu,&base);
    o<<"GPU: "<<p.deviceName<<"\nAPI: "<<ver(p.apiVersion)<<"\nDriver version: "<<p.driverVersion<<"\n";
    o<<"Max texture 2D: "<<p.limits.maxImageDimension2D<<"\n";
    o<<"Sampler anisotropy: "<<yn(base.samplerAnisotropy)<<" / maximum "<<p.limits.maxSamplerAnisotropy<<"x\n";
    o<<"Framebuffer MSAA sample count mask: "<<p.limits.framebufferColorSampleCounts<<"\n";
    uint32_t extCount=0;ret=vkEnumerateDeviceExtensionProperties(gpu,nullptr,&extCount,nullptr);
    if(ret!=VK_SUCCESS) {o<<"Extension query failed "<<ret<<"\n";continue;}
    std::vector<VkExtensionProperties> ext(extCount);
    vkEnumerateDeviceExtensionProperties(gpu,nullptr,&extCount,ext.data());
    std::set<std::string> names;
    for(const auto& x:ext)names.insert(x.extensionName);
    auto has=[&](const char* e){return names.count(e)>0;};
    bool accel=has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    bool query=has(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    bool pipeline=has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    bool deferred=has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    bool bda=p.apiVersion>=VK_API_VERSION_1_2||has(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    bool spirv=p.apiVersion>=VK_API_VERSION_1_2||has("VK_KHR_spirv_1_4");
    bool floatControls=p.apiVersion>=VK_API_VERSION_1_2||has("VK_KHR_shader_float_controls");
    o<<"\nVK_KHR_acceleration_structure: "<<yn(accel)
     <<"\nVK_KHR_ray_query: "<<yn(query)
     <<"\nVK_KHR_ray_tracing_pipeline: "<<yn(pipeline)
     <<"\nVK_KHR_deferred_host_operations: "<<yn(deferred)
     <<"\nBuffer device address: "<<yn(bda)
     <<"\nSPIR-V 1.4: "<<yn(spirv)
     <<"\nShader float controls: "<<yn(floatControls)<<"\n";
    bool fa=false,fq=false,fp=false,fb=false;
    if(getFeatures2 && p.apiVersion>=VK_API_VERSION_1_1) {
      VkPhysicalDeviceFeatures2 root{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      VkPhysicalDeviceBufferDeviceAddressFeatures b{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR a{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
      VkPhysicalDeviceRayQueryFeaturesKHR q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
      VkPhysicalDeviceRayTracingPipelineFeaturesKHR rp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
      void** tail=&root.pNext;
      if(bda){*tail=&b;tail=&b.pNext;}
      if(accel){*tail=&a;tail=&a.pNext;}
      if(query){*tail=&q;tail=&q.pNext;}
      if(pipeline){*tail=&rp;tail=&rp.pNext;}
      getFeatures2(gpu,&root);
      fa=a.accelerationStructure;fq=q.rayQuery;fp=rp.rayTracingPipeline;fb=b.bufferDeviceAddress;
    }else o<<"vkGetPhysicalDeviceFeatures2 unavailable.\n";
    o<<"Feature.accelerationStructure: "<<yn(fa)
     <<"\nFeature.rayQuery: "<<yn(fq)
     <<"\nFeature.rayTracingPipeline: "<<yn(fp)
     <<"\nFeature.bufferDeviceAddress: "<<yn(fb)<<"\n";
    bool rayQueryReady=accel&&query&&deferred&&bda&&spirv&&floatControls&&fa&&fq&&fb;
    bool rayPipelineReady=accel&&pipeline&&deferred&&bda&&spirv&&floatControls&&fa&&fp&&fb;
    o<<"\nRay Query requirements: "<<(rayQueryReady?"PRESENT":"MISSING")<<"\n";
    o<<"Ray Tracing Pipeline requirements: "<<(rayPipelineReady?"PRESENT":"MISSING")<<"\n";
    if(rayQueryReady) {
      uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);
      std::vector<VkQueueFamilyProperties> queues(nq);
      vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,queues.data());
      int qi=-1;
      for(uint32_t i=0;i<nq;i++)if(queues[i].queueCount&&(queues[i].queueFlags&(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))){qi=(int)i;break;}
      if(qi<0){o<<"No suitable compute queue.\n";continue;}
      std::vector<const char*> enable={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
      if(p.apiVersion<VK_API_VERSION_1_2){
        enable.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        enable.push_back("VK_KHR_spirv_1_4");
        enable.push_back("VK_KHR_shader_float_controls");
      }
      VkPhysicalDeviceBufferDeviceAddressFeatures b{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR a{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
      VkPhysicalDeviceRayQueryFeaturesKHR q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
      b.bufferDeviceAddress=VK_TRUE;a.accelerationStructure=VK_TRUE;q.rayQuery=VK_TRUE;
      b.pNext=&a;a.pNext=&q;
      VkPhysicalDeviceFeatures2 root{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};root.pNext=&b;
      float priority=1.f;
      VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=(uint32_t)qi;qci.queueCount=1;qci.pQueuePriorities=&priority;
      VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.pNext=&root;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=(uint32_t)enable.size();dci.ppEnabledExtensionNames=enable.data();
      VkDevice logical=VK_NULL_HANDLE;ret=vkCreateDevice(gpu,&dci,nullptr,&logical);
      o<<"vkCreateDevice with Ray Query: "<<(ret==VK_SUCCESS?"SUCCESS":"FAILED")<<" (result="<<ret<<")\n";
      if(logical)vkDestroyDevice(logical,nullptr);
    }
    o<<"\nIMPORTANT: Driver support does not guarantee Roblox uses RT.\n";
  }
  vkDestroyInstance(inst,nullptr);
  return o.str();
}
extern "C" JNIEXPORT jstring JNICALL Java_com_tagunner_rtlab_MainActivity_inspectVulkan(JNIEnv* env,jclass) {
  auto result=probe();return env->NewStringUTF(result.c_str());
}
