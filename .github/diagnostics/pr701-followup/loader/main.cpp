#include <vkbuilder.hpp>
#include <SDL.h>
#include <SDL_vulkan.h>
#include <execinfo.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
bool vkb::InstanceBuilder::loaded = false;
void stage(const char* text) { std::fprintf(stderr,"LOADER_STAGE %s\n",text); std::fflush(stderr); }
void crash(int signal) { void* frames[48]; int n=backtrace(frames,48); backtrace_symbols_fd(frames,n,2); std::_Exit(128+signal); }
int main(int argc,char**argv) {
 std::signal(SIGABRT,crash);std::signal(SIGSEGV,crash);
 const char* mode=argc>1?argv[1]:"default";
 stage(mode);stage("SDL_get_proc");
 auto proc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
 std::fprintf(stderr,"SDL proc=%p error=%s\n",reinterpret_cast<void*>(proc),SDL_GetError());
 if(!std::strcmp(mode,"sdl")){SDL_Init(SDL_INIT_VIDEO); SDL_Vulkan_LoadLibrary(nullptr);proc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());}
 if(proc) VULKAN_HPP_DEFAULT_DISPATCHER.init(proc);
 else if(!std::strcmp(mode,"keep")){static vk::DynamicLoader loader;VULKAN_HPP_DEFAULT_DISPATCHER.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));}
 else {stage("VKBuilder_load_default");vkb::InstanceBuilder::loadDefault();}
 stage("dispatch_loaded");
 Dl_info info{};dladdr(reinterpret_cast<void*>(VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr),&info);std::fprintf(stderr,"dispatcher library=%s\n",info.dli_fname?info.dli_fname:"unknown");
 stage("enumerate_extensions");auto exts=vk::enumerateInstanceExtensionProperties();std::fprintf(stderr,"extensions=%zu\n",exts.size());
 vk::InstanceCreateFlags flags{};std::vector<const char*> names;
 for(auto&e:exts){if(!std::strcmp(e.extensionName,"VK_KHR_portability_enumeration")){names.push_back("VK_KHR_portability_enumeration");flags|=vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;}}
 vk::ApplicationInfo app{};app.apiVersion=VK_API_VERSION_1_0;
 vk::InstanceCreateInfo ci{};ci.flags=flags;ci.pApplicationInfo=&app;ci.enabledExtensionCount=names.size();ci.ppEnabledExtensionNames=names.data();
 stage("create_instance");auto instance=vk::createInstance(ci);VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
 stage("enumerate_devices");auto devices=instance.enumeratePhysicalDevices();std::fprintf(stderr,"devices=%zu\n",devices.size());instance.destroy();stage("PASS");
}
