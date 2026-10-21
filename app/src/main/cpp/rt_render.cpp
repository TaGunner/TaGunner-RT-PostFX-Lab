#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void checked(VkResult r,const char* what) {
  if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+" VkResult="+std::to_string(r));
}
struct Buffer {
  VkBuffer buf=VK_NULL_HANDLE;
  VkDeviceMemory mem=VK_NULL_HANDLE;
  VkDeviceAddress address=0;
  VkDeviceSize size=0;
};
struct Lab {
  VkInstance instance=VK_NULL_HANDLE;
  VkPhysicalDevice gpu=VK_NULL_HANDLE;
  VkDevice device=VK_NULL_HANDLE;
  VkQueue queue=VK_NULL_HANDLE;
  uint32_t family=0;
  VkCommandPool commandPool=VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
  VkPipeline pipeline=VK_NULL_HANDLE;
  VkShaderModule shader=VK_NULL_HANDLE;
  VkAccelerationStructureKHR blas=VK_NULL_HANDLE,tlas=VK_NULL_HANDLE;
  std::vector<Buffer> allocated;
  PFN_vkGetBufferDeviceAddress getBufferAddress=nullptr;
  PFN_vkGetAccelerationStructureBuildSizesKHR getSizes=nullptr;
  PFN_vkCreateAccelerationStructureKHR createAS=nullptr;
  PFN_vkDestroyAccelerationStructureKHR destroyAS=nullptr;
  PFN_vkGetAccelerationStructureDeviceAddressKHR getASAddress=nullptr;
  PFN_vkCmdBuildAccelerationStructuresKHR cmdBuild=nullptr;
  ~Lab() {
    if(device) {
      vkDeviceWaitIdle(device);
      if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
      if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
      if(setLayout)vkDestroyDescriptorSetLayout(device,setLayout,nullptr);
      if(descriptorPool)vkDestroyDescriptorPool(device,descriptorPool,nullptr);
      if(shader)vkDestroyShaderModule(device,shader,nullptr);
      if(destroyAS) {
        if(tlas)destroyAS(device,tlas,nullptr);
        if(blas)destroyAS(device,blas,nullptr);
      }
      for(auto& b:allocated){
        if(b.buf)vkDestroyBuffer(device,b.buf,nullptr);
        if(b.mem)vkFreeMemory(device,b.mem,nullptr);
      }
      if(commandPool)vkDestroyCommandPool(device,commandPool,nullptr);
      vkDestroyDevice(device,nullptr);
    }
    if(instance)vkDestroyInstance(instance,nullptr);
  }
  void init() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="TaGunner Hardware Ray Query Scene";
    app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo=&app;
    checked(vkCreateInstance(&ici,nullptr,&instance),"vkCreateInstance");
    uint32_t n=0;
    checked(vkEnumeratePhysicalDevices(instance,&n,nullptr),"Enumerate GPU");
    if(!n)throw std::runtime_error("No Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(n);
    checked(vkEnumeratePhysicalDevices(instance,&n,devices.data()),"Enumerate GPUs");
    for(auto candidate:devices) {
      uint32_t extN=0;
      vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extN,nullptr);
      std::vector<VkExtensionProperties> ext(extN);
      vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extN,ext.data());
      auto has=[&](const char* name){
        for(const auto& x:ext)if(strcmp(name,x.extensionName)==0)return true;
        return false;
      };
      if(!has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)||
         !has(VK_KHR_RAY_QUERY_EXTENSION_NAME)||
         !has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME))continue;
      VkPhysicalDeviceBufferDeviceAddressFeatures addr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
      VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
      VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f.pNext=&addr;addr.pNext=&as;as.pNext=&rq;
      auto getFeatures2=(PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceFeatures2");
      if(!getFeatures2)throw std::runtime_error("vkGetPhysicalDeviceFeatures2 symbol unavailable");
      getFeatures2(candidate,&f);
      if(!addr.bufferDeviceAddress||!as.accelerationStructure||!rq.rayQuery)continue;
      uint32_t qN=0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate,&qN,nullptr);
      std::vector<VkQueueFamilyProperties> families(qN);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate,&qN,families.data());
      bool found=false;
      for(uint32_t q=0;q<qN;++q){
        if(families[q].queueCount&&(families[q].queueFlags&VK_QUEUE_COMPUTE_BIT)){
          gpu=candidate;family=q;found=true;break;
        }
      }
      if(found)break;
    }
    if(!gpu)throw std::runtime_error("Ray Query / AS feature or compute queue missing");
    const char* exts[]={
      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
      VK_KHR_RAY_QUERY_EXTENSION_NAME,
      VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures addr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    addr.bufferDeviceAddress=VK_TRUE;as.accelerationStructure=VK_TRUE;rq.rayQuery=VK_TRUE;
    features.pNext=&addr;addr.pNext=&as;as.pNext=&rq;
    float priority=1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext=&features;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
    dci.enabledExtensionCount=3;dci.ppEnabledExtensionNames=exts;
    checked(vkCreateDevice(gpu,&dci,nullptr,&device),"vkCreateDevice");
    vkGetDeviceQueue(device,family,0,&queue);
    auto proc=[&](const char* name)->PFN_vkVoidFunction{
      auto f=vkGetDeviceProcAddr(device,name);
      if(!f)throw std::runtime_error(std::string("Missing Vulkan symbol ")+name);
      return f;
    };
    getBufferAddress=reinterpret_cast<PFN_vkGetBufferDeviceAddress>(proc("vkGetBufferDeviceAddress"));
    getSizes=reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(proc("vkGetAccelerationStructureBuildSizesKHR"));
    createAS=reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(proc("vkCreateAccelerationStructureKHR"));
    destroyAS=reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(proc("vkDestroyAccelerationStructureKHR"));
    getASAddress=reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(proc("vkGetAccelerationStructureDeviceAddressKHR"));
    cmdBuild=reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(proc("vkCmdBuildAccelerationStructuresKHR"));
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    checked(vkCreateCommandPool(device,&pool,nullptr,&commandPool),"Command pool");
  }
  uint32_t memoryIndex(uint32_t compatible,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(gpu,&props);
    for(uint32_t i=0;i<props.memoryTypeCount;++i)
      if((compatible&(1u<<i))&&(props.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("No Vulkan compatible memory type");
  }
  Buffer makeBuffer(VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags flags,bool address) {
    Buffer b{};b.size=size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=size;bi.usage=usage;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    checked(vkCreateBuffer(device,&bi,nullptr,&b.buf),"vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device,b.buf,&req);
    VkMemoryAllocateFlagsInfo allocateFlags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    allocateFlags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=req.size;ai.memoryTypeIndex=memoryIndex(req.memoryTypeBits,flags);
    if(address)ai.pNext=&allocateFlags;
    checked(vkAllocateMemory(device,&ai,nullptr,&b.mem),"vkAllocateMemory");
    checked(vkBindBufferMemory(device,b.buf,b.mem,0),"vkBindBufferMemory");
    if(address){
      VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
      info.buffer=b.buf;b.address=getBufferAddress(device,&info);
      if(!b.address)throw std::runtime_error("Buffer device address is zero");
    }
    allocated.push_back(b);
    return b;
  }
  void upload(const Buffer& b,const void* bytes,size_t count) {
    void* mapped=nullptr;checked(vkMapMemory(device,b.mem,0,b.size,0,&mapped),"vkMapMemory upload");
    memcpy(mapped,bytes,count);
    vkUnmapMemory(device,b.mem);
  }
  VkCommandBuffer begin() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=commandPool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cb=VK_NULL_HANDLE;
    checked(vkAllocateCommandBuffers(device,&ai,&cb),"Allocate command buffer");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checked(vkBeginCommandBuffer(cb,&bi),"Begin command buffer");
    return cb;
  }
  void finish(VkCommandBuffer cb) {
    checked(vkEndCommandBuffer(cb),"End command buffer");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount=1;si.pCommandBuffers=&cb;
    checked(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE),"vkQueueSubmit");
    checked(vkQueueWaitIdle(queue),"vkQueueWaitIdle");
    vkFreeCommandBuffers(device,commandPool,1,&cb);
  }
  VkDeviceAddress asAddress(VkAccelerationStructureKHR as) {
    VkAccelerationStructureDeviceAddressInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    info.accelerationStructure=as;
    return getASAddress(device,&info);
  }
  VkAccelerationStructureKHR buildAs(VkAccelerationStructureBuildGeometryInfoKHR info,uint32_t primitive,VkCommandBuffer cb) {
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    getSizes(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,&primitive,&sizes);
    Buffer asBuf=makeBuffer(sizes.accelerationStructureSize,
       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,true);
    VkAccelerationStructureCreateInfoKHR ac{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ac.buffer=asBuf.buf;ac.size=sizes.accelerationStructureSize;ac.type=info.type;
    VkAccelerationStructureKHR result=VK_NULL_HANDLE;
    checked(createAS(device,&ac,nullptr,&result),"Create AS");
    // Hold handle immediately so exceptions are cleaned up by the Lab destructor.
    if(info.type==VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)blas=result;else tlas=result;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext=&p;
    auto getProperties2=(PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceProperties2");
    if(!getProperties2)throw std::runtime_error("vkGetPhysicalDeviceProperties2 symbol unavailable");
    getProperties2(gpu,&props);
    const VkDeviceSize alignment=std::max((VkDeviceSize)256,(VkDeviceSize)p.minAccelerationStructureScratchOffsetAlignment);
    Buffer scratch=makeBuffer(sizes.buildScratchSize+alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,true);
    info.dstAccelerationStructure=result;
    info.scratchData.deviceAddress=(scratch.address+alignment-1)&~(alignment-1);
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount=primitive;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[]={&range};
    cmdBuild(cb,1,&info,ranges);
    return result;
  }
};
static void appendTri(std::vector<float>& out,const float* a,const float* b,const float* c) {
  for(auto p:{a,b,c})for(int i=0;i<3;++i)out.push_back(p[i]);
}
static void quad(std::vector<float>& v,
                 float ax,float ay,float az,float bx,float by,float bz,
                 float cx,float cy,float cz,float dx,float dy,float dz) {
  float a[3]={ax,ay,az},b[3]={bx,by,bz},c[3]={cx,cy,cz},d[3]={dx,dy,dz};
  appendTri(v,a,b,c);appendTri(v,a,c,d);
}
static std::vector<float> geometry() {
  std::vector<float> v;
  // Primitive indices 0-1 are floor; 2-13 are front/right/back/left/top/bottom cube.
  quad(v,-5,0,-5, 5,0,-5, 5,0,5, -5,0,5);
  const float s=.6f,h=1.25f;
  quad(v,-s,0,s, s,0,s, s,h,s, -s,h,s);
  quad(v,s,0,s, s,0,-s, s,h,-s, s,h,s);
  quad(v,s,0,-s, -s,0,-s, -s,h,-s, s,h,-s);
  quad(v,-s,0,-s, -s,0,s, -s,h,s, -s,h,-s);
  quad(v,-s,h,s, s,h,s, s,h,-s, -s,h,-s);
  quad(v,-s,0,-s, s,0,-s, s,0,s, -s,0,s);
  return v;
}
static std::vector<uint32_t> render(AAssetManager* assets,uint32_t width,uint32_t height) {
  Lab lab;
  lab.init();
  std::vector<float> vertices=geometry();
  const uint32_t triangleCount=(uint32_t)vertices.size()/9;
  Buffer vertex=lab.makeBuffer(vertices.size()*sizeof(float),
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
  lab.upload(vertex,vertices.data(),vertices.size()*sizeof(float));
  VkAccelerationStructureGeometryTrianglesDataKHR tri{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
  tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT;
  tri.vertexData.deviceAddress=vertex.address;
  tri.vertexStride=3*sizeof(float);
  tri.maxVertex=(uint32_t)vertices.size()/3-1;
  tri.indexType=VK_INDEX_TYPE_NONE_KHR;
  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geom.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  geom.geometry.triangles=tri;
  geom.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR binfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  binfo.type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  binfo.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  binfo.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  binfo.geometryCount=1;binfo.pGeometries=&geom;
  VkCommandBuffer build=lab.begin();
  lab.buildAs(binfo,triangleCount,build);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  barrier.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(build,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,0,1,&barrier,0,nullptr,0,nullptr);
  VkAccelerationStructureInstanceKHR ins{};
  ins.transform.matrix[0][0]=1.f;ins.transform.matrix[1][1]=1.f;ins.transform.matrix[2][2]=1.f;
  ins.mask=0xff;
  ins.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  ins.accelerationStructureReference=lab.asAddress(lab.blas);
  Buffer instance=lab.makeBuffer(sizeof(ins),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,true);
  lab.upload(instance,&ins,sizeof(ins));
  VkAccelerationStructureGeometryInstancesDataKHR insData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
  insData.arrayOfPointers=VK_FALSE;insData.data.deviceAddress=instance.address;
  VkAccelerationStructureGeometryKHR tgeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tgeom.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tgeom.geometry.instances=insData;
  VkAccelerationStructureBuildGeometryInfoKHR tinfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  tinfo.type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tinfo.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tinfo.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tinfo.geometryCount=1;tinfo.pGeometries=&tgeom;
  lab.buildAs(tinfo,1,build);
  lab.finish(build);

  Buffer pixels=lab.makeBuffer((VkDeviceSize)width*height*sizeof(uint32_t),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,false);
  // shader bytecode is generated by GitHub Actions using glslangValidator before Gradle.
  AAsset* a=AAssetManager_open(assets,"rt_scene.spv",AASSET_MODE_BUFFER);
  if(!a)throw std::runtime_error("rt_scene.spv not bundled in APK");
  size_t bytes=(size_t)AAsset_getLength(a);
  if(bytes<20||bytes%4) {AAsset_close(a);throw std::runtime_error("Bad SPIR-V shader size");}
  std::vector<uint32_t> code(bytes/4);
  int count=AAsset_read(a,code.data(),bytes);
  AAsset_close(a);
  if(count<0||(size_t)count!=bytes)throw std::runtime_error("Cannot read SPIR-V shader");
  VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sm.codeSize=bytes;sm.pCode=code.data();
  checked(vkCreateShaderModule(lab.device,&sm,nullptr,&lab.shader),"Create ray-query shader");

  VkDescriptorSetLayoutBinding bindings[2]{};
  bindings[0].binding=0;bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  bindings[0].descriptorCount=1;bindings[0].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
  bindings[1].binding=1;bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[1].descriptorCount=1;bindings[1].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dl.bindingCount=2;dl.pBindings=bindings;
  checked(vkCreateDescriptorSetLayout(lab.device,&dl,nullptr,&lab.setLayout),"Descriptor layout");
  VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,8};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount=1;pl.pSetLayouts=&lab.setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
  checked(vkCreatePipelineLayout(lab.device,&pl,nullptr,&lab.pipelineLayout),"Pipeline layout");
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;stage.module=lab.shader;stage.pName="main";
  VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pc.stage=stage;pc.layout=lab.pipelineLayout;
  checked(vkCreateComputePipelines(lab.device,VK_NULL_HANDLE,1,&pc,nullptr,&lab.pipeline),"Create ray-query compute pipeline");
  VkDescriptorPoolSize poolSizes[2]={{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
  VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=poolSizes;
  checked(vkCreateDescriptorPool(lab.device,&pool,nullptr,&lab.descriptorPool),"Descriptor pool");
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  da.descriptorPool=lab.descriptorPool;da.descriptorSetCount=1;da.pSetLayouts=&lab.setLayout;
  VkDescriptorSet ds=VK_NULL_HANDLE;
  checked(vkAllocateDescriptorSets(lab.device,&da,&ds),"Descriptor set");
  VkWriteDescriptorSetAccelerationStructureKHR asWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  asWrite.accelerationStructureCount=1;asWrite.pAccelerationStructures=&lab.tlas;
  VkDescriptorBufferInfo pi{pixels.buf,0,VK_WHOLE_SIZE};
  VkWriteDescriptorSet writes[2]{};
  writes[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].pNext=&asWrite;writes[0].dstSet=ds;writes[0].dstBinding=0;
  writes[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;writes[0].descriptorCount=1;
  writes[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet=ds;writes[1].dstBinding=1;
  writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].descriptorCount=1;
  writes[1].pBufferInfo=&pi;
  vkUpdateDescriptorSets(lab.device,2,writes,0,nullptr);
  VkCommandBuffer cmd=lab.begin();
  vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,lab.pipeline);
  vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,lab.pipelineLayout,0,1,&ds,0,nullptr);
  uint32_t wh[2]={width,height};
  vkCmdPushConstants(cmd,lab.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(wh),wh);
  vkCmdDispatch(cmd,(width+7)/8,(height+7)/8,1);
  VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  hostBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
  hostBarrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
       0,1,&hostBarrier,0,nullptr,0,nullptr);
  lab.finish(cmd);
  void* src=nullptr;
  checked(vkMapMemory(lab.device,pixels.mem,0,pixels.size,0,&src),"Map ray-traced pixels");
  std::vector<uint32_t> image(width*height);
  auto rgba=(const uint32_t*)src;
  for(size_t i=0;i<image.size();++i){
    uint32_t v=rgba[i];
    image[i]=0xff000000u | ((v&0xffu)<<16) | (v&0xff00u) | ((v>>16)&0xffu);
  }
  vkUnmapMemory(lab.device,pixels.mem);
  return image;
}
}
extern "C" JNIEXPORT jintArray JNICALL
Java_com_tagunner_rtlab_MainActivity_renderRayQueryScene(JNIEnv* env,jclass,jobject assetManager,jint width,jint height) {
  try {
    if(width<=0||height<=0||width>1024||height>1024)
      throw std::runtime_error("Invalid image resolution");
    AAssetManager* assets=AAssetManager_fromJava(env,assetManager);
    if(!assets)throw std::runtime_error("Android AssetManager unavailable");
    auto pixels=render(assets,(uint32_t)width,(uint32_t)height);
    jintArray result=env->NewIntArray((jsize)pixels.size());
    if(result)env->SetIntArrayRegion(result,0,(jsize)pixels.size(),reinterpret_cast<const jint*>(pixels.data()));
    return result;
  }catch(const std::exception& e){
    jclass cls=env->FindClass("java/lang/IllegalStateException");
    if(cls)env->ThrowNew(cls,e.what());
    return nullptr;
  }
}
