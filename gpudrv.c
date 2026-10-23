#include "jni.h"
#include "vulkan_core.h"
#include <stddef.h>
#include <stdarg.h>

/* Android/Bionic ABI declarations. No host C library is linked into this DSO. */
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern int setenv(const char *, const char *, int);
extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);
extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern void *memset(void *, int, size_t);
extern void *malloc(size_t);
extern void free(void *);
extern int open(const char *, int, ...);
extern int close(int);
extern long write(int, const void *, size_t);
extern int getuid(void);
extern int *__errno(void);
extern int __android_log_print(int, const char *, const char *, ...);

static PFN_vkGetInstanceProcAddr active_gipa;
static PFN_vkGetDeviceProcAddr active_gdpa;
static int active_mode=-1, game_locked=0;
static int restart_required=0;
static char log_directory[1024];
static char report[24576];
static size_t used;
static void line(const char *fmt, ...) {
    if (used>=sizeof(report)-2) return;
    va_list args; va_start(args,fmt);
    int n=vsnprintf(report+used,sizeof(report)-used,fmt,args);
    va_end(args);
    if (n<0) return;
    if ((size_t)n>=sizeof(report)-used) used=sizeof(report)-2;
    else used+=(size_t)n;
    report[used++]='\n'; report[used]=0;
}
static void save_report(void) {
    if (!log_directory[0]) return;
    char path[1200]; snprintf(path,sizeof(path),"%s/mali-driver-diagnostics.txt",log_directory);
    int fd=open(path,1|64|512,0600);
    if(fd>=0) { size_t n=0; while(n<used) {long r=write(fd,report+n,used-n);if(r<=0)break;n+=(size_t)r;} close(fd); }
    __android_log_print(4,"SleepingDogsMali","%s",report);
}
static PFN_vkGetInstanceProcAddr load_driver(const char *directory,int mode) {
    char path[1200];
    if(mode==1) snprintf(path,sizeof(path),"%s/libvulkan_panfrost.so",directory);
    else snprintf(path,sizeof(path),"libvulkan.so");
    line("Loading selected Vulkan driver: %s",path);save_report();
    void *handle=dlopen(path,2);
    if(!handle) {line("ERROR: dlopen(%s): %s",path,dlerror());return 0;}
    const char *symbol=mode==1?"vk_icdGetInstanceProcAddr":"vkGetInstanceProcAddr";
    PFN_vkGetInstanceProcAddr p=(PFN_vkGetInstanceProcAddr)dlsym(handle,symbol);
    if(!p) {line("ERROR: missing %s: %s",symbol,dlerror());return 0;}
    if(mode==1) {
        typedef VkResult (*Negotiate)(uint32_t *);
        Negotiate negotiate=(Negotiate)dlsym(handle,"vk_icdNegotiateLoaderICDInterfaceVersion");
        uint32_t version=5;
        if(negotiate) {VkResult r=negotiate(&version);line("ICD interface negotiation: %d, version %u",r,version);if(r!=VK_SUCCESS)return 0;}
    }
    line("Driver loaded: %s",path);
    return p;
}
/* All game/DXVK paths enter here, never the original Turnip WSI wrapper. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,const char *name) {
    game_locked=1;
    if(!active_gipa)return 0;
    if(strcmp(name,"vkGetInstanceProcAddr")==0)return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    PFN_vkVoidFunction p=active_gipa(instance,name);
    if(instance&&strcmp(name,"vkGetDeviceProcAddr")==0) active_gdpa=(PFN_vkGetDeviceProcAddr)p;
    return p;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,const char *name) {
    if(!active_gipa)return 0;
    PFN_vkGetDeviceProcAddr p=active_gdpa;
    return p?p(device,name):0;
}

static void probe_driver(PFN_vkGetInstanceProcAddr gipa) {
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)gipa(VK_NULL_HANDLE,"vkCreateInstance");
    if(!create){line("ERROR: vkCreateInstance unavailable");return;}
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Sleeping Dogs Mali diagnostics",.apiVersion=VK_API_VERSION_1_3};
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance=VK_NULL_HANDLE;
    line("Starting real Vulkan instance probe...");save_report();
    VkResult result=create(&ci,0,&instance);line("vkCreateInstance (Vulkan 1.3): %d",result);
    if(result!=VK_SUCCESS)return;
#define IF(name) PFN_##name name=(PFN_##name)gipa(instance,#name)
    IF(vkDestroyInstance);IF(vkEnumeratePhysicalDevices);IF(vkGetPhysicalDeviceProperties);
    IF(vkGetPhysicalDeviceFeatures2);IF(vkEnumerateDeviceExtensionProperties);
    IF(vkGetPhysicalDeviceQueueFamilyProperties);IF(vkCreateDevice);IF(vkGetDeviceProcAddr);
    IF(vkGetPhysicalDeviceMemoryProperties);
#undef IF
    if(!vkDestroyInstance||!vkEnumeratePhysicalDevices||!vkGetPhysicalDeviceProperties||!vkGetPhysicalDeviceFeatures2||!vkEnumerateDeviceExtensionProperties||!vkGetPhysicalDeviceQueueFamilyProperties||!vkCreateDevice||!vkGetDeviceProcAddr||!vkGetPhysicalDeviceMemoryProperties) {
        line("ERROR: incomplete Vulkan instance dispatch");if(vkDestroyInstance)vkDestroyInstance(instance,0);return;
    }
    uint32_t count=0;result=vkEnumeratePhysicalDevices(instance,&count,0);line("Physical devices: %u (result %d)",count,result);
    if(result!=VK_SUCCESS||count==0){line("ERROR: driver cannot access a GPU on this device/kernel");vkDestroyInstance(instance,0);return;}
    VkPhysicalDevice *physical=malloc(sizeof(*physical)*count);
    if(!physical){line("ERROR: allocation failed");vkDestroyInstance(instance,0);return;}
    result=vkEnumeratePhysicalDevices(instance,&count,physical);
    if(result!=VK_SUCCESS||count==0){line("ERROR: GPU enumeration %d, count %u",result,count);free(physical);vkDestroyInstance(instance,0);return;}
    VkPhysicalDevice gpu=physical[0];free(physical);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);
    line("GPU: %s",props.deviceName);line("Vulkan %u.%u.%u; vendor 0x%x; device 0x%x; driver 0x%x",VK_VERSION_MAJOR(props.apiVersion),VK_VERSION_MINOR(props.apiVersion),VK_VERSION_PATCH(props.apiVersion),props.vendorID,props.deviceID,props.driverVersion);
    VkPhysicalDeviceRobustness2FeaturesEXT robust={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceVulkan12Features f12={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,.pNext=&robust};
    VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&f12};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13};
    vkGetPhysicalDeviceFeatures2(gpu,&features);
    uint32_t ext_count=0;vkEnumerateDeviceExtensionProperties(gpu,0,&ext_count,0);
    VkExtensionProperties *exts=malloc(sizeof(*exts)*ext_count);
    int has_robust=0,has_swapchain=0;
    if(exts){result=vkEnumerateDeviceExtensionProperties(gpu,0,&ext_count,exts);for(uint32_t n=0;result==VK_SUCCESS&&n<ext_count;n++){
        if(strcmp(exts[n].extensionName,"VK_EXT_robustness2")==0)has_robust=1;
        if(strcmp(exts[n].extensionName,"VK_KHR_swapchain")==0)has_swapchain=1;
    }free(exts);}
    line("VK_EXT_robustness2: %s",has_robust?"YES":"MISSING");
    line("shaderClipDistance: %s",features.features.shaderClipDistance?"YES":"MISSING");
    line("robustBufferAccess2=%u robustImageAccess2=%u nullDescriptor=%u",robust.robustBufferAccess2,robust.robustImageAccess2,robust.nullDescriptor);
    line("geometryShader=%u textureCompressionBC=%u shaderCullDistance=%u",features.features.geometryShader,features.features.textureCompressionBC,features.features.shaderCullDistance);
    line("VulkanMemoryModel=%u bufferDeviceAddress=%u synchronization2=%u dynamicRendering=%u",f12.vulkanMemoryModel,f12.bufferDeviceAddress,f13.synchronization2,f13.dynamicRendering);
    line("VK_KHR_swapchain: %s",has_swapchain?"YES":"MISSING");
    uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&queues,0);
    VkQueueFamilyProperties *qp=malloc(sizeof(*qp)*queues);uint32_t family=UINT32_MAX;
    if(qp){vkGetPhysicalDeviceQueueFamilyProperties(gpu,&queues,qp);for(uint32_t n=0;n<queues;n++)if(qp[n].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=n;break;}free(qp);}
    if(family==UINT32_MAX){line("ERROR: no graphics queue");vkDestroyInstance(instance,0);return;}
    if(!has_robust||!features.features.shaderClipDistance||!robust.nullDescriptor){line("ERROR: required graphics capabilities still missing; original game checks remain enabled");vkDestroyInstance(instance,0);return;}
    float priority=1.0f;VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkPhysicalDeviceFeatures enabled={.shaderClipDistance=VK_TRUE};
    VkPhysicalDeviceRobustness2FeaturesEXT enable_robust={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,.nullDescriptor=VK_TRUE};
    const char *extension="VK_EXT_robustness2";
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&enable_robust,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=1,.ppEnabledExtensionNames=&extension,.pEnabledFeatures=&enabled};
    VkDevice device=VK_NULL_HANDLE;line("Creating logical device with real clip-distance + null descriptors...");save_report();result=vkCreateDevice(gpu,&dci,0,&device);line("Logical device with real clip-distance + null descriptors: %d",result);
    if(result==VK_SUCCESS){
#define DF(name) PFN_##name name=(PFN_##name)vkGetDeviceProcAddr(device,#name)
        DF(vkDestroyDevice);DF(vkCreateBuffer);DF(vkGetBufferMemoryRequirements);DF(vkAllocateMemory);DF(vkBindBufferMemory);
        DF(vkCreateCommandPool);DF(vkAllocateCommandBuffers);DF(vkBeginCommandBuffer);DF(vkCmdFillBuffer);
        DF(vkEndCommandBuffer);DF(vkCmdPipelineBarrier);DF(vkGetDeviceQueue);DF(vkQueueSubmit);DF(vkCreateFence);DF(vkWaitForFences);DF(vkDestroyFence);
        DF(vkMapMemory);DF(vkUnmapMemory);DF(vkDestroyCommandPool);DF(vkDestroyBuffer);DF(vkFreeMemory);
#undef DF
        if(!vkDestroyDevice||!vkCreateBuffer||!vkGetBufferMemoryRequirements||!vkAllocateMemory||!vkBindBufferMemory||!vkCreateCommandPool||!vkAllocateCommandBuffers||!vkBeginCommandBuffer||!vkCmdFillBuffer||!vkEndCommandBuffer||!vkCmdPipelineBarrier||!vkGetDeviceQueue||!vkQueueSubmit||!vkCreateFence||!vkWaitForFences||!vkDestroyFence||!vkMapMemory||!vkUnmapMemory||!vkDestroyCommandPool||!vkDestroyBuffer||!vkFreeMemory){
            line("ERROR: incomplete Vulkan device dispatch");
            if(vkDestroyDevice){vkDestroyDevice(device,0);vkDestroyInstance(instance,0);}else{restart_required=1;line("Force-stop before retrying; device cleanup function missing.");}
            return;
        }
        VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
        VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=4096,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        result=vkCreateBuffer(device,&bci,0,&buffer);
        VkMemoryRequirements req;VkPhysicalDeviceMemoryProperties mp;uint32_t type=UINT32_MAX;
        if(result==VK_SUCCESS){vkGetBufferMemoryRequirements(device,buffer,&req);vkGetPhysicalDeviceMemoryProperties(gpu,&mp);for(uint32_t n=0;n<mp.memoryTypeCount;n++)if((req.memoryTypeBits&(1u<<n))&&((mp.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))){type=n;break;}}
        if(type!=UINT32_MAX){VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};result=vkAllocateMemory(device,&ai,0,&memory);if(result==VK_SUCCESS)result=vkBindBufferMemory(device,buffer,memory,0);}else result=VK_ERROR_FEATURE_NOT_PRESENT;
        if(result==VK_SUCCESS){VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};result=vkCreateCommandPool(device,&pci,0,&pool);}
        VkCommandBuffer cb=VK_NULL_HANDLE;
        if(result==VK_SUCCESS){VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};result=vkAllocateCommandBuffers(device,&ai,&cb);}
        if(result==VK_SUCCESS){VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};result=vkBeginCommandBuffer(cb,&bi);if(result==VK_SUCCESS){vkCmdFillBuffer(cb,buffer,0,4096,0x61561561);VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,0,0,0);result=vkEndCommandBuffer(cb);}}
        if(result==VK_SUCCESS){VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};result=vkCreateFence(device,&fi,0,&fence);}
        int completion_unknown=0;line("Starting GPU transfer/readback smoke test...");save_report();
        if(result==VK_SUCCESS){VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};result=vkQueueSubmit(queue,1,&si,fence);if(result==VK_SUCCESS){result=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL);completion_unknown=result!=VK_SUCCESS;}}
        int matched=0;
        if(result==VK_SUCCESS){void *mapped=0;result=vkMapMemory(device,memory,0,4096,0,&mapped);if(result==VK_SUCCESS){matched=1;for(int n=0;n<1024;n++)if(((uint32_t *)mapped)[n]!=0x61561561){matched=0;break;}vkUnmapMemory(device,memory);}}
        line("GPU transfer/readback smoke test: %s (VkResult %d)",matched?"PASS":"FAIL",result);
        if(completion_unknown){restart_required=1;line("ERROR: GPU submission completion is unknown. Force-stop this app before retrying. Resources retained to avoid destroying in-flight objects.");return;}
        if(fence)vkDestroyFence(device,fence,0);if(pool)vkDestroyCommandPool(device,pool,0);if(buffer)vkDestroyBuffer(device,buffer,0);if(memory)vkFreeMemory(device,memory,0);vkDestroyDevice(device,0);
    }
    vkDestroyInstance(instance,0);
    line("Surface presentation, DXVK initialization and actual game play have NOT been tested by this probe.");
}

JNIEXPORT jstring JNICALL Java_com_sdre_mali_MaliBridge_configure(JNIEnv *env,jclass cls,jstring dir,jstring files,jint mode) {
    (void)cls;used=0;report[0]=0;
    const char *d=(*env)->GetStringUTFChars(env,dir,0),*f=(*env)->GetStringUTFChars(env,files,0);
    if(!d||!f)return (*env)->NewStringUTF(env,"ERROR: cannot read app paths");
    snprintf(log_directory,sizeof(log_directory),"%s",f);
    int success=0;
    if(restart_required){line("ERROR: force-stop this app before retrying; an earlier GPU operation has not completed.");}
    else if(game_locked&&active_mode!=mode){line("ERROR: driver cannot be changed after game startup. Force-stop the app, then select a driver.");}
    else {PFN_vkGetInstanceProcAddr p=load_driver(d,mode);if(p){if(active_mode!=mode)active_gdpa=0;active_gipa=p;active_mode=mode;success=1;setenv("DXVK_LOG_PATH",f,1);line("OK: %s configured. Original Vulkan feature checks remain enabled.",mode==1?"bundled PanVK RC3 (experimental)":"phone system Vulkan");}}
    (*env)->ReleaseStringUTFChars(env,dir,d);(*env)->ReleaseStringUTFChars(env,files,f);
    save_report();
    if(success){char message[512];snprintf(message,sizeof(message),"OK: %s selected. Restart the app before changing drivers after playing.",mode==1?"PanVK RC3":"System Vulkan");return (*env)->NewStringUTF(env,message);}
    return (*env)->NewStringUTF(env,report);
}
JNIEXPORT jstring JNICALL Java_com_sdre_mali_MaliBridge_probe(JNIEnv *env,jclass cls,jstring dir,jstring files,jint mode) {
    (void)cls;used=0;report[0]=0;
    const char *d=(*env)->GetStringUTFChars(env,dir,0),*f=(*env)->GetStringUTFChars(env,files,0);
    if(!d||!f)return (*env)->NewStringUTF(env,"ERROR: cannot read app paths");
    snprintf(log_directory,sizeof(log_directory),"%s",f);
    line("Sleeping Dogs Mali compatibility TEST BUILD; selected mode=%d; uid=%d",mode,getuid());
    int fd=open("/dev/mali0",2);int err=*__errno();line("/dev/mali0 O_RDWR: %s (errno %d)",fd>=0?"accessible":"unavailable",fd>=0?0:err);if(fd>=0)close(fd);
    if(restart_required){line("ERROR: force-stop this app before retrying; an earlier GPU operation has not completed.");}
    else if(game_locked){line("ERROR: force-stop and reopen before running diagnostics after game startup.");}
    else {PFN_vkGetInstanceProcAddr p=load_driver(d,mode);if(p)probe_driver(p);}
    (*env)->ReleaseStringUTFChars(env,dir,d);(*env)->ReleaseStringUTFChars(env,files,f);
    save_report();return (*env)->NewStringUTF(env,report);
}
