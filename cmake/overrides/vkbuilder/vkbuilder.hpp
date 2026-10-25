#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#if defined(VKB_ENABLE_VMA)
#if !defined(VKB_VMA_HEADER)
#if __has_include(<vma/vk_mem_alloc.h>)
#define VKB_VMA_HEADER <vma/vk_mem_alloc.h>
#elif __has_include(<vulkan/vk_mem_alloc.h>)
#define VKB_VMA_HEADER <vulkan/vk_mem_alloc.h>
#else
#error "VKB_ENABLE_VMA requires vk_mem_alloc.h"
#endif
#endif
#include VKB_VMA_HEADER
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <stdexcept>
#include <utility>

// Vulkan-Hpp >= 1.4 moved the dynamic dispatch loader classes (DispatchLoaderDynamic,
// DynamicLoader) from the vk namespace into vk::detail. Re-export them at vk::
// scope so vkbuilder compiles against both older (e.g. the 1.3.x header vendored by
// EVEngine) and newer SDK headers.
#if VK_HEADER_VERSION >= 357
namespace vk {
using detail::DispatchLoaderDynamic;
using detail::DynamicLoader;
} // namespace vk
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <dlfcn.h>
#define VKB_LOAD_FROM_PROCESS 1
#endif
#endif

namespace vkb {

// Agent template for adding a default object for vkb::Instance/Device...
template<class T>
struct Agent {
  T instance;

  vk::Optional<const vk::AllocationCallbacks> allocation_callbacks = nullptr;

  T* operator->() { return &instance; }
  const T* operator->() const { return &instance; }
  
  operator T() { return instance; }
  operator const T() const { return instance; }
};


#pragma region Instance
class PhysicalDeviceSelector;

struct Instance : Agent<vk::Instance> {
  vk::DebugUtilsMessengerEXT debug_messenger;

  bool headless = false;
  uint32_t instance_version = VK_MAKE_VERSION(1, 2, 0);

  void destroy() { instance.destroy(debug_messenger, allocation_callbacks); }

  /// Physical device selection requires a live Instance.
  PhysicalDeviceSelector selectPhysicalDevice() const;
};

// Gathers useful information about the available vulkan capabilities, like
// layers and instance extensions. Use this for enabling features conditionally,
// ie if you would like an extension but can use a fallback if it isn't
// supported but need to know if support is available first.
struct SystemInfo {
private:
  SystemInfo() {}

public:
  static SystemInfo query() {
    SystemInfo info;
    info.available_layers = vk::enumerateInstanceLayerProperties();
    if (info.is_layer_available("VK_LAYER_KHRONOS_validation"))
      info.validation_layers_available = true;

    info.available_extensions = vk::enumerateInstanceExtensionProperties();
    if (info.is_extension_available("VK_EXT_debug_utils"))
      info.debug_utils_available = true;

    if (info.debug_utils_available == false)
      for (auto &layer : info.available_layers) {
        std::vector<vk::ExtensionProperties> layer_extensions =
            vk::enumerateInstanceExtensionProperties(
                std::string(layer.layerName.data()));
        for (auto &ext : layer_extensions)
          if (strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
            info.debug_utils_available = true;
      }
    return info;
  }

  // Returns true if a layer is available
  bool is_layer_available(std::string layer_name) const {
    for (auto &layer : available_layers)
      if (layer_name == layer.layerName)
        return true;
    return false;
  }

  // Returns true if an extension is available
  bool is_extension_available(std::string extension_name) {
    for (auto &ext : available_extensions)
      if (extension_name == ext.extensionName)
        return true;
    return false;
  }

  std::vector<vk::LayerProperties> available_layers;
  std::vector<vk::ExtensionProperties> available_extensions;
  bool validation_layers_available = false;
  bool debug_utils_available = false;
};

namespace helper {

static inline const char *
to_string_message_severity(VkDebugUtilsMessageSeverityFlagBitsEXT s) {
  switch (s) {
  case VkDebugUtilsMessageSeverityFlagBitsEXT::
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    return "VERBOSE";
  case VkDebugUtilsMessageSeverityFlagBitsEXT::
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    return "ERROR";
  case VkDebugUtilsMessageSeverityFlagBitsEXT::
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    return "WARNING";
  case VkDebugUtilsMessageSeverityFlagBitsEXT::
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    return "INFO";
  default:
    return "UNKNOWN";
  }
}
static inline const char *
to_string_message_type(VkDebugUtilsMessageTypeFlagsEXT s) {
  if (s == 7)
    return "General | Validation | Performance";
  if (s == 6)
    return "Validation | Performance";
  if (s == 5)
    return "General | Performance";
  if (s == 4 /*VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT*/)
    return "Performance";
  if (s == 3)
    return "General | Validation";
  if (s == 2 /*VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT*/)
    return "Validation";
  if (s == 1 /*VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT*/)
    return "General";
  return "Unknown";
}

static inline bool
check_layer_supported(std::vector<vk::LayerProperties> const &available_layers,
                      const char *layer_name) {
  if (!layer_name)
    return false;
  for (const auto &layer_properties : available_layers) {
    if (strcmp(layer_name, layer_properties.layerName) == 0) {
      return true;
    }
  }
  return false;
}

static inline bool
check_layers_supported(std::vector<vk::LayerProperties> const &available_layers,
                       std::vector<const char *> const &layer_names) {
  bool all_found = true;
  for (const auto &layer_name : layer_names) {
    bool found = check_layer_supported(available_layers, layer_name);
    if (!found)
      all_found = false;
  }
  return all_found;
}

static inline bool check_extension_supported(
    std::vector<vk::ExtensionProperties> const &available_extensions,
    const char *extension_name) {
  if (!extension_name)
    return false;
  for (const auto &extension_properties : available_extensions) {
    if (strcmp(extension_name, extension_properties.extensionName) == 0) {
      return true;
    }
  }
  return false;
}

static inline bool check_extensions_supported(
    std::vector<vk::ExtensionProperties> const &available_extensions,
    std::vector<const char *> const &extension_names) {
  bool all_found = true;
  for (const auto extension_name : extension_names) {
    bool found =
        check_extension_supported(available_extensions, extension_name);
    if (!found)
      all_found = false;
  }
  return all_found;
}

static inline bool check_add_window_ext(const char *name,
                                        const SystemInfo &system,
                                        std::vector<const char *>& extensions) {
  if (!check_extension_supported(system.available_extensions, name))
    return false;
  extensions.push_back(name);
  return true;
};

static inline std::vector<const char *>
strvec2c_str(std::vector<std::string> const &strvec) {
  std::vector<const char *> result;
  result.reserve(strvec.size());
  for (auto const &s : strvec) {
    result.push_back(s.c_str());
  }
  return result;
}

template <typename T>
void setup_pNext_chain(T &structure,
                       const std::vector<vk::BaseOutStructure *> &structs) {
  structure.pNext = nullptr;
  if (structs.size() <= 0)
    return;
  for (size_t i = 0; i < structs.size() - 1; i++) {
    structs[i]->pNext = structs[i + 1];
  }
  structs[structs.size()-1]->pNext = nullptr;
  structure.pNext = structs.at(0);
}



extern VKAPI_ATTR VkBool32 VKAPI_CALL default_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);

static inline vk::DebugUtilsMessengerEXT create_debug_utils_messenger(
    vk::Instance instance, PFN_vkDebugUtilsMessengerCallbackEXT debug_callback,
    vk::DebugUtilsMessageSeverityFlagsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    vk::Optional<const vk::AllocationCallbacks> allocation_callbacks) {

  vk::DispatchLoaderDynamic dldi( instance, vkGetInstanceProcAddr );
  if (debug_callback == nullptr)
    debug_callback = default_debug_callback;
  vk::DebugUtilsMessengerCreateInfoEXT messengerCreateInfo;
  messengerCreateInfo.pNext = nullptr;
  messengerCreateInfo.messageSeverity = severity;
  messengerCreateInfo.messageType = type;
#if VK_HEADER_VERSION >= 357
  // Vulkan-Hpp 1.4 types the callback with enum-class parameters; the C-style
  // PFN_vkDebugUtilsMessengerCallbackEXT is ABI-compatible, so cast (the header
  // itself performs the same cast).
  messengerCreateInfo.pfnUserCallback =
      reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debug_callback);
#else
  messengerCreateInfo.pfnUserCallback = debug_callback;
#endif

  return instance.createDebugUtilsMessengerEXT(messengerCreateInfo,
                                               allocation_callbacks, 
                                               dldi);
}

} // namespace helper

class InstanceBuilder {
public:
  static bool loaded;
  static void loadDefault() {
#ifdef VKB_LOAD_FROM_PROCESS
    // iOS has no libvulkan.dylib to dlopen: MoltenVK is an embedded framework
    // linked into the app, so its symbols are already in the process.
    auto vkGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr"));
    if (vkGetInstanceProcAddr == nullptr)
      throw std::runtime_error("Failed to locate vkGetInstanceProcAddr (MoltenVK not loaded)");
#else
    vk::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
#endif
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
  }

  InstanceBuilder() {
    if (!loaded) loadDefault();
  }

  // Create a VkInstance. Throw an error if it failed.
  Instance build() const {
    SystemInfo system = SystemInfo::query();

    uint32_t api_version = VK_MAKE_VERSION(1, 0, 0);

    if (required_api_version > VK_MAKE_VERSION(1, 0, 0) ||
        desired_api_version > VK_MAKE_VERSION(1, 0, 0)) {

      uint32_t queried_api_version = VK_MAKE_VERSION(1, 0, 0);
      queried_api_version = vk::enumerateInstanceVersion();

      if (queried_api_version < required_api_version) {
        throw std::runtime_error("required_api_version not satisfied");
      }
      if (required_api_version > VK_MAKE_VERSION(1, 0, 0)) {
        api_version = required_api_version;
      } else if (desired_api_version > VK_MAKE_VERSION(1, 0, 0)) {
        if (queried_api_version >= desired_api_version)
          api_version = desired_api_version;
        else
          api_version = queried_api_version;
      }
    }

    vk::ApplicationInfo app_info;
    app_info.pApplicationName = app_name.c_str();
    app_info.applicationVersion = application_version;
    app_info.pEngineName = engine_name.c_str();
    app_info.engineVersion = engine_version;
    app_info.apiVersion = api_version;

    std::vector<const char *> extensions;
    for (auto &ext : this->extensions)
      extensions.push_back(ext.c_str());
    if (debug_callback != nullptr && system.debug_utils_available) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (!headless_context) {

      bool khr_surface_added =
          helper::check_add_window_ext("VK_KHR_surface", system, extensions);
#if defined(_WIN32)
      bool added_window_exts = helper::check_add_window_ext(
          "VK_KHR_win32_surface", system, extensions);
#elif defined(__ANDROID__)
      bool added_window_exts = helper::check_add_window_ext(
          "VK_KHR_android_surface", system, extensions);
#elif defined(_DIRECT2DISPLAY)
      bool added_window_exts =
          helper::check_add_window_ext("VK_KHR_display", system, extensions);
#elif defined(__linux__)
      // make sure all three calls to check_add_window_ext, don't allow short
      // circuiting
      bool added_window_exts = helper::check_add_window_ext(
          "VK_KHR_xcb_surface", system, extensions);
      added_window_exts = helper::check_add_window_ext("VK_KHR_xlib_surface",
                                                       system, extensions) ||
                          added_window_exts;
      added_window_exts = helper::check_add_window_ext("VK_KHR_wayland_surface",
                                                       system, extensions) ||
                          added_window_exts;
#elif defined(__APPLE__)
      bool added_window_exts = helper::check_add_window_ext(
          "VK_EXT_metal_surface", system, extensions);
#endif
      if (!khr_surface_added || !added_window_exts)
        throw std::runtime_error("window exts not found");
    }
    
    bool all_extensions_supported = helper::check_extensions_supported(
        system.available_extensions, extensions);
    if (!all_extensions_supported) {
      throw std::runtime_error("required extensions are not supported");
    }

    std::vector<const char *> layers;
    for (auto &layer : this->layers)
      layers.push_back(layer.c_str());

    if (m_enable_validation_layers ||
        (m_request_validation_layers && system.validation_layers_available)) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    bool all_layers_supported =
        helper::check_layers_supported(system.available_layers, layers);
    if (!all_layers_supported)
      throw std::runtime_error("not all required layer supported");

    std::vector<vk::BaseOutStructure *> pNext_chain;
    vk::DebugUtilsMessengerCreateInfoEXT messengerCreateInfo;
    if (use_debug_messenger) {
      messengerCreateInfo.setMessageSeverity(debug_message_severity);
      messengerCreateInfo.setMessageType(debug_message_type);
#if VK_HEADER_VERSION >= 357
      messengerCreateInfo.setPfnUserCallback(
          reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(
              debug_callback));
#else
      messengerCreateInfo.setPfnUserCallback(debug_callback);
#endif
      pNext_chain.push_back(
          reinterpret_cast<vk::BaseOutStructure *>(&messengerCreateInfo));
    }
    vk::ValidationFeaturesEXT features;
    if (enabled_validation_features.size() != 0 ||
        disabled_validation_features.size()) {
      features.enabledValidationFeatureCount =
          static_cast<uint32_t>(enabled_validation_features.size());
      features.pEnabledValidationFeatures = enabled_validation_features.data();
      features.disabledValidationFeatureCount =
          static_cast<uint32_t>(disabled_validation_features.size());
      features.pDisabledValidationFeatures =
          disabled_validation_features.data();
      pNext_chain.push_back(
          reinterpret_cast<vk::BaseOutStructure *>(&features));
    }

    vk::ValidationFlagsEXT checks;
    if (disabled_validation_checks.size() != 0) {
      checks.disabledValidationCheckCount =
          static_cast<uint32_t>(disabled_validation_checks.size());
      checks.pDisabledValidationChecks = disabled_validation_checks.data();
      pNext_chain.push_back(reinterpret_cast<vk::BaseOutStructure *>(&checks));
    }

    vk::InstanceCreateInfo instance_create_info;
    helper::setup_pNext_chain<vk::InstanceCreateInfo>(instance_create_info,
                                                      pNext_chain);
    instance_create_info.flags = flags;
    instance_create_info.pApplicationInfo = &app_info;
    instance_create_info.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    instance_create_info.ppEnabledExtensionNames = extensions.data();
    instance_create_info.enabledLayerCount =
        static_cast<uint32_t>(layers.size());
    instance_create_info.ppEnabledLayerNames = layers.data();

    Instance inst;
    inst.instance = vk::createInstance(instance_create_info, allocation_callbacks);
      
    VULKAN_HPP_DEFAULT_DISPATCHER.init(inst.instance);

    if (use_debug_messenger) {
      inst.debug_messenger = helper::create_debug_utils_messenger(
          inst.instance, debug_callback, debug_message_severity,
          debug_message_type, allocation_callbacks);
    }

    return inst;
  }

  // Sets the name of the application. Defaults to "" if none is provided.
  InstanceBuilder &set_app_name(std::string app_name) {
    this->app_name = app_name;
    return *this;
  }
  // Sets the name of the engine. Defaults to "" if none is provided.
  InstanceBuilder &set_engine_name(std::string engine_name) {
    this->engine_name = engine_name;
    return *this;
  }

  // Sets the (major, minor, patch) version of the application.
  InstanceBuilder &set_app_version(uint32_t major, uint32_t minor,
                                   uint32_t patch = 0) {
    this->application_version = VK_MAKE_VERSION(major, minor, patch);
    return *this;
  }
  // Sets the (major, minor, patch) version of the engine.
  InstanceBuilder &set_engine_version(uint32_t major, uint32_t minor,
                                      uint32_t patch = 0) {
    this->engine_version = VK_MAKE_VERSION(major, minor, patch);
    return *this;
  }

  // Require a vulkan instance API version. Will fail to create if this version
  // isn't available.
  InstanceBuilder &require_api_version(uint32_t major, uint32_t minor,
                                       uint32_t patch = 0) {
    this->required_api_version = VK_MAKE_VERSION(major, minor, patch);
    return *this;
  }

  // Prefer a vulkan instance API version. If the desired version isn't
  // available, it will use the highest version available.
  InstanceBuilder &desire_api_version(uint32_t major, uint32_t minor,
                                      uint32_t patch = 0) {
    this->desired_api_version = VK_MAKE_VERSION(major, minor, patch);
    return *this;
  }

  // Adds a layer to be enabled. Will fail to create an instance if the layer
  // isn't available.
  InstanceBuilder &enable_layer(std::string layer_name) {
    layers.push_back(layer_name);
    return *this;
  }

  // Adds an extension to be enabled. Will fail to create an instance if the
  // extension isn't available.
  InstanceBuilder &enable_extension(std::string extension_name) {
    extensions.push_back(extension_name);
    return *this;
  }

  // Adds instance creation flags (e.g. EnumeratePortabilityKHR for MoltenVK).
  InstanceBuilder &add_flags(vk::InstanceCreateFlags create_flags) {
    flags |= create_flags;
    return *this;
  }

  // Headless Mode does not load the required extensions for presentation.
  // Defaults to true.
  InstanceBuilder &set_headless(bool headless = true) {
    headless_context = headless;
    return *this;
  }

  // Enables the validation layers. Will fail to create an instance if the
  // validation layers aren't available.
  InstanceBuilder &enable_validation_layers(bool require_validation = true) {
    m_enable_validation_layers = require_validation;
    return *this;
  }
  // Checks if the validation layers are available and loads them if they are.
  InstanceBuilder &request_validation_layers(bool enable_validation = true) {
    m_request_validation_layers = enable_validation;
    return *this;
  }

  // Use a default debug callback that prints to standard out.
  InstanceBuilder &use_default_debug_messenger() {
    use_debug_messenger = true;
    debug_callback = helper::default_debug_callback;
    return *this;
  }
  // Provide a user defined debug callback.
  InstanceBuilder &
  set_debug_callback(PFN_vkDebugUtilsMessengerCallbackEXT callback) {
    use_debug_messenger = true;
    debug_callback = callback;
    return *this;
  }

  // Set what message severity is needed to trigger the callback.
  InstanceBuilder &
  set_debug_messenger_severity(vk::DebugUtilsMessageSeverityFlagsEXT severity) {
    debug_message_severity = severity;
    return *this;
  }
  // Add a message severity to the list that triggers the callback.
  InstanceBuilder &
  add_debug_messenger_severity(vk::DebugUtilsMessageSeverityFlagsEXT severity) {
    debug_message_severity = debug_message_severity | severity;
    return *this;
  }
  // Set what message type triggers the callback.
  InstanceBuilder &
  set_debug_messenger_type(vk::DebugUtilsMessageTypeFlagsEXT type) {
    debug_message_type = type;
    return *this;
  }
  // Add a message type to the list of that triggers the callback.
  InstanceBuilder &
  add_debug_messenger_type(vk::DebugUtilsMessageTypeFlagsEXT type) {
    debug_message_type = debug_message_type | type;
    return *this;
  }

  // Disable some validation checks.
  // Checks: All, and Shaders
  InstanceBuilder &add_validation_disable(vk::ValidationCheckEXT check) {
    disabled_validation_checks.push_back(check);
    return *this;
  }

  // Enables optional parts of the validation layers.
  // Parts: best practices, gpu assisted, and gpu assisted reserve binding slot.
  InstanceBuilder &
  add_validation_feature_enable(vk::ValidationFeatureEnableEXT enable) {
    enabled_validation_features.push_back(enable);
    return *this;
  }

  // Disables sections of the validation layers.
  // Options: All, shaders, thread safety, api parameters, object lifetimes,
  // core checks, and unique handles.
  InstanceBuilder &
  add_validation_feature_disable(vk::ValidationFeatureDisableEXT disable) {
    disabled_validation_features.push_back(disable);
    return *this;
  }

  // Provide custom allocation callbacks.
  InstanceBuilder &set_allocation_callbacks(vk::Optional<const vk::AllocationCallbacks> callbacks) {
    allocation_callbacks = callbacks;
    return *this;
  }

private:
  // VkApplicationInfo
  std::string app_name;
  std::string engine_name;
  uint32_t application_version = 0;
  uint32_t engine_version = 0;
  uint32_t required_api_version = VK_MAKE_VERSION(1, 0, 0);
  uint32_t desired_api_version = VK_MAKE_VERSION(1, 2, 0);

  // VkInstanceCreateInfo
  std::vector<std::string> layers;
  std::vector<std::string> extensions;
  vk::InstanceCreateFlags flags;
  std::vector<VkBaseOutStructure *> pNext_elements;

  bool m_enable_validation_layers = false;
  bool m_request_validation_layers = false;
  bool use_debug_messenger = false;
  bool headless_context = false;

  // debug callback
  PFN_vkDebugUtilsMessengerCallbackEXT debug_callback = nullptr;
  vk::DebugUtilsMessageSeverityFlagsEXT debug_message_severity = 
		    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
  vk::DebugUtilsMessageTypeFlagsEXT debug_message_type = 
		    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
		    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;


  // validation features
  std::vector<vk::ValidationCheckEXT> disabled_validation_checks;
  std::vector<vk::ValidationFeatureEnableEXT> enabled_validation_features;
  std::vector<vk::ValidationFeatureDisableEXT> disabled_validation_features;

  // Custom allocator
  vk::Optional<const vk::AllocationCallbacks> allocation_callbacks = nullptr;
};

#pragma endregion

#pragma region PhysicalDevice
// ---- Physical Device ---- //
class PhysicalDeviceSelector;
class DeviceBuilder;

constexpr uint32_t QUEUE_INDEX_MAX_VALUE = 65536;

struct QueueFamilies {
  QueueFamilies(const std::vector<vk::QueueFamilyProperties> &families)
      : families(families) {}
  QueueFamilies() {}
  virtual ~QueueFamilies() {}

  // Upstream note: MultiQueue parallel workload not yet supported

  // finds the first queue which supports graphics operations. returns
  // QUEUE_INDEX_MAX_VALUE if none is found
  uint32_t get_graphics_queue_index() const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++) {
      if (families[i].queueFlags & vk::QueueFlagBits::eGraphics)
        return i;
    }
    return QUEUE_INDEX_MAX_VALUE;
  }

  // finds the first queue which supports only compute (not graphics or
  // transfer). returns QUEUE_INDEX_MAX_VALUE if none is found
  uint32_t get_dedicated_compute_queue_index() const {
    for (uint32_t i = 0; i < families.size(); i++) {
      if ((families[i].queueFlags & vk::QueueFlagBits::eCompute) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eTransfer))
        return i;
    }
    return QUEUE_INDEX_MAX_VALUE;
  }

  // finds a compute queue which is separate from the graphics queue and tries
  // to find one without transfer support returns QUEUE_INDEX_MAX_VALUE if none
  // is found
  uint32_t get_separate_compute_queue_index() const {
    uint32_t compute = QUEUE_INDEX_MAX_VALUE;
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++) {
      if ((families[i].queueFlags & vk::QueueFlagBits::eCompute) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eGraphics)) {
        if (!(families[i].queueFlags & vk::QueueFlagBits::eTransfer)) {
          return i;
        } else {
          compute = i;
        }
      }
    }
    return compute;
  }

  // finds a transfer queue which is separate from the graphics queue and tries
  // to find one without compute support returns QUEUE_INDEX_MAX_VALUE if none
  // is found
  uint32_t get_separate_transfer_queue_index() const {
    uint32_t transfer = QUEUE_INDEX_MAX_VALUE;
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++) {
      if ((families[i].queueFlags & vk::QueueFlagBits::eTransfer) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eGraphics)) {
        if (!(families[i].queueFlags & vk::QueueFlagBits::eCompute)) {
          return i;
        } else {
          transfer = i;
        }
      }
    }
    return transfer;
  }

  // finds the first queue which supports only transfer (not graphics or
  // compute). returns QUEUE_INDEX_MAX_VALUE if none is found
  uint32_t get_dedicated_transfer_queue_index() const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++) {
      if ((families[i].queueFlags & vk::QueueFlagBits::eTransfer) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
          !(families[i].queueFlags & vk::QueueFlagBits::eCompute))
        return i;
    }
    return QUEUE_INDEX_MAX_VALUE;
  }
  // finds the first queue which supports presenting. returns
  // QUEUE_INDEX_MAX_VALUE if none is found
  uint32_t get_present_queue_index(vk::PhysicalDevice const phys_device,
                                   vk::SurfaceKHR const surface) const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); i++) {
      VkBool32 presentSupport = false;
      if (surface) {
        vk::Result res =
            phys_device.getSurfaceSupportKHR(i, surface, &presentSupport);
        if (res != vk::Result::eSuccess)
          return QUEUE_INDEX_MAX_VALUE; // Upstream note: determine if this should fail
                                        // another way
      }
      if (presentSupport == VK_TRUE)
        return i;
    }
    return QUEUE_INDEX_MAX_VALUE;
  }

  std::vector<vk::QueueFamilyProperties> families;
};

struct PhysicalDevice : Agent<vk::PhysicalDevice>{
  vk::SurfaceKHR surface;

  vk::PhysicalDeviceFeatures features;
  vk::PhysicalDeviceProperties properties;
  vk::PhysicalDeviceMemoryProperties memory_properties;

  // Has a queue family that supports compute operations but not graphics nor
  // transfer.
  bool has_dedicated_compute_queue() const {
    return queue_families.get_dedicated_compute_queue_index() !=
           QUEUE_INDEX_MAX_VALUE;
  }
  // Has a queue family that supports transfer operations but not graphics nor
  // compute.
  bool has_dedicated_transfer_queue() const {
    return queue_families.get_dedicated_transfer_queue_index() !=
           QUEUE_INDEX_MAX_VALUE;
  }

  // Has a queue family that supports transfer operations but not graphics.
  bool has_separate_compute_queue() const {
    return queue_families.get_separate_compute_queue_index() !=
           QUEUE_INDEX_MAX_VALUE;
  }
  // Has a queue family that supports transfer operations but not graphics.
  bool has_separate_transfer_queue() const {
    return queue_families.get_separate_transfer_queue_index() !=
           QUEUE_INDEX_MAX_VALUE;
  }

  // Advanced: Get the VkQueueFamilyProperties of the device if special queue
  // setup is needed
  QueueFamilies get_queue_families() const { return queue_families; }

  uint32_t findMemoryTypeIndex(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties = instance.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) && 
          (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }

    throw std::runtime_error("failed to find suitable memory type!");
    return 0;
  }

  /// Logical device creation requires a selected PhysicalDevice.
  DeviceBuilder createDevice() const;

private:
  std::vector<std::string> extensions_to_enable;
  QueueFamilies queue_families;
  bool defer_surface_initialization = false;
  friend class PhysicalDeviceSelector;
  friend class DeviceBuilder;
};

enum class PreferredDeviceType {
  other = 0,
  integrated = 1,
  discrete = 2,
  virtual_gpu = 3,
  cpu = 4
};

class PhysicalDeviceSelector {
public:
  // Requires a Instance to construct, needed to pass instance creation
  // info.
  PhysicalDeviceSelector(Instance const &instance) {
    system_info.instance = instance.instance;
    system_info.headless = instance.headless;
    criteria.require_present = !instance.headless;
    criteria.required_version = instance.instance_version;
    criteria.desired_version = instance.instance_version;
  }

  PhysicalDevice select() const {
    if (!system_info.headless && !criteria.defer_surface_initialization) {
      if (!system_info.surface)
        throw std::runtime_error("no_surface_provided");
    }

    auto physical_devices = system_info.instance.enumeratePhysicalDevices();
    if (physical_devices.empty()) {
      throw std::runtime_error("failed_enumerate_physical_devices");
    }
    if (physical_devices.size() == 0) {
      throw std::runtime_error("no_physical_devices_found");
    }

    std::vector<PhysicalDeviceDesc> phys_device_descriptions;
    for (auto &phys_device : physical_devices) {
      phys_device_descriptions.push_back(populate_device_details(phys_device));
    }

    PhysicalDeviceDesc selected_device{};

    if (criteria.use_first_gpu_unconditionally) {
      selected_device = phys_device_descriptions.at(0);
    } else {
      for (const auto &device : phys_device_descriptions) {
        auto suitable = is_device_suitable(device);
        if (suitable == Suitable::yes) {
          selected_device = device;
          break;
        } else if (suitable == Suitable::partial) {
          selected_device = device;
        }
      }
    }
   
    if (!selected_device.phys_device) {
      throw std::runtime_error("no_suitable_device");
    }
 
    PhysicalDevice out_device;
    out_device.instance = selected_device.phys_device;
    out_device.surface = system_info.surface;
    out_device.features = criteria.required_features;
    out_device.properties = selected_device.device_properties;
    out_device.memory_properties = selected_device.mem_properties;
    out_device.queue_families = selected_device.queue_families;
    out_device.defer_surface_initialization =
        criteria.defer_surface_initialization;

    out_device.extensions_to_enable.insert(
        out_device.extensions_to_enable.end(),
        criteria.required_extensions.begin(),
        criteria.required_extensions.end());
    auto desired_extensions_supported =
        selected_device.check_device_extension_support(
            criteria.desired_extensions);
    out_device.extensions_to_enable.insert(
        out_device.extensions_to_enable.end(),
        desired_extensions_supported.begin(),
        desired_extensions_supported.end());
    return out_device;
  }

  // Set the surface in which the physical device should render to.
  PhysicalDeviceSelector &set_surface(vk::SurfaceKHR surface) {
    system_info.surface = surface;
    system_info.headless = false;
    return *this;
  }

  // Set the desired physical device type to select. Defaults to
  // PreferredDeviceType::discrete.
  PhysicalDeviceSelector &prefer_gpu_device_type(
      PreferredDeviceType type = PreferredDeviceType::discrete) {
    criteria.preferred_type = type;
    return *this;
  }

  // Allow selection of a gpu device type that isn't the preferred physical
  // device type. Defaults to true.
  PhysicalDeviceSelector &
  allow_any_gpu_device_type(bool allow_any_type = true) {
    criteria.allow_any_type = allow_any_type;
    return *this;
  }

  // Require that a physical device supports presentation. Defaults to true.
  PhysicalDeviceSelector &require_present(bool require = true) {
    criteria.require_present = require;
    return *this;
  }

  // Require a queue family that supports compute operations but not graphics
  // nor transfer.
  PhysicalDeviceSelector &require_dedicated_compute_queue() {
    criteria.require_dedicated_compute_queue = true;
    return *this;
  }

  // Require a queue family that supports transfer operations but not graphics
  // nor compute.
  PhysicalDeviceSelector &require_dedicated_transfer_queue() {
    criteria.require_dedicated_transfer_queue = true;
    return *this;
  }

  // Require a queue family that supports compute operations but not graphics.
  PhysicalDeviceSelector &require_separate_compute_queue() {
    criteria.require_separate_compute_queue = true;
    return *this;
  }

  // Require a queue family that supports transfer operations but not graphics.
  PhysicalDeviceSelector &require_separate_transfer_queue() {
    criteria.require_separate_transfer_queue = true;
    return *this;
  }

  // Require a memory heap from VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT with `size`
  // memory available.
  PhysicalDeviceSelector &required_device_memory_size(vk::DeviceSize size) {
    criteria.required_mem_size = size;
    return *this;
  }
  // Prefer a memory heap from VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT with `size`
  // memory available.
  PhysicalDeviceSelector &desired_device_memory_size(vk::DeviceSize size) {
    criteria.desired_mem_size = size;
    return *this;
  }

  // Require a physical device which supports a specific extension.
  PhysicalDeviceSelector &add_required_extension(std::string extension) {
    criteria.required_extensions.push_back(extension);
    return *this;
  }
  // Require a physical device which supports a set of extensions.
  PhysicalDeviceSelector &
  add_required_extensions(std::vector<std::string> extensions) {
    criteria.required_extensions.insert(criteria.required_extensions.end(),
                                        extensions.begin(), extensions.end());
    return *this;
  }

  // Prefer a physical device which supports a specific extension.
  PhysicalDeviceSelector &add_desired_extension(std::string extension) {
    criteria.desired_extensions.push_back(extension);
    return *this;
  }
  // Prefer a physical device which supports a set of extensions.
  PhysicalDeviceSelector &
  add_desired_extensions(std::vector<std::string> extensions) {
    criteria.desired_extensions.insert(criteria.desired_extensions.end(),
                                       extensions.begin(), extensions.end());
    return *this;
  }

  // Prefer a physical device that supports a (major, minor) version of vulkan.
  PhysicalDeviceSelector &set_desired_version(uint32_t major, uint32_t minor) {
    criteria.desired_version = VK_MAKE_VERSION(major, minor, 0);
    return *this;
  }
  // Require a physical device that supports a (major, minor) version of vulkan.
  PhysicalDeviceSelector &set_minimum_version(uint32_t major, uint32_t minor) {
    criteria.required_version = VK_MAKE_VERSION(major, minor, 0);
    return *this;
  }

  // Require a physical device which supports the features in
  // VkPhysicalDeviceFeatures.
  PhysicalDeviceSelector &
  set_required_features(vk::PhysicalDeviceFeatures features) {
    criteria.required_features = features;
    return *this;
  }

  // Used when surface creation happens after physical device selection.
  // Warning: This disables checking if the physical device supports a given
  // surface.
  PhysicalDeviceSelector &defer_surface_initialization() {
    criteria.defer_surface_initialization = true;
    return *this;
  }

  // Ignore all criteria and choose the first physical device that is available.
  // Only use when: The first gpu in the list may be set by global user
  // preferences and an application may wish to respect it.
  PhysicalDeviceSelector &
  select_first_device_unconditionally(bool unconditionally = true) {
    criteria.use_first_gpu_unconditionally = unconditionally;
    return *this;
  }

private:
  struct SystemInfo {
    vk::Instance instance;
    vk::SurfaceKHR surface;
    bool headless = false;
  } system_info;

  struct PhysicalDeviceDesc {
    vk::PhysicalDevice phys_device;
    QueueFamilies queue_families;

    vk::PhysicalDeviceFeatures device_features;
    vk::PhysicalDeviceProperties device_properties;
    vk::PhysicalDeviceMemoryProperties mem_properties;

    std::vector<std::string> check_device_extension_support(
        std::vector<std::string> desired_extensions) {
      auto available_extensions =
          phys_device.enumerateDeviceExtensionProperties();

      std::vector<std::string> extensions_to_enable;
      for (const auto &extension : available_extensions) {
        for (auto &req_ext : desired_extensions) {
          if (req_ext == extension.extensionName) {
            extensions_to_enable.push_back(req_ext);
            break;
          }
        }
      }
      return extensions_to_enable;
    }
  };

  PhysicalDeviceDesc
  populate_device_details(vk::PhysicalDevice phys_device) const {
    PhysicalDeviceSelector::PhysicalDeviceDesc desc{};
    desc.phys_device = phys_device;
    desc.queue_families = QueueFamilies(phys_device.getQueueFamilyProperties());

    desc.device_properties = phys_device.getProperties();
    desc.device_features = phys_device.getFeatures();
    desc.mem_properties = phys_device.getMemoryProperties();
    return desc;
  }

  struct SelectionCriteria {
    PreferredDeviceType preferred_type = PreferredDeviceType::discrete;
    bool allow_any_type = true;
    bool require_present = true;
    bool require_dedicated_transfer_queue = false;
    bool require_dedicated_compute_queue = false;
    bool require_separate_transfer_queue = false;
    bool require_separate_compute_queue = false;
    vk::DeviceSize required_mem_size = 0;
    vk::DeviceSize desired_mem_size = 0;

    std::vector<std::string> required_extensions;
    std::vector<std::string> desired_extensions;

    uint32_t required_version = VK_MAKE_VERSION(1, 0, 0);
    uint32_t desired_version = VK_MAKE_VERSION(1, 0, 0);

    vk::PhysicalDeviceFeatures required_features;

    bool defer_surface_initialization = false;
    bool use_first_gpu_unconditionally = false;
  } criteria;

  enum class Suitable { yes, partial, no };

  Suitable is_device_suitable(PhysicalDeviceDesc pd) const {
    Suitable suitable = Suitable::yes;

    if (criteria.required_version > pd.device_properties.apiVersion)
      return Suitable::no;
    if (criteria.desired_version > pd.device_properties.apiVersion)
      suitable = Suitable::partial;

    bool dedicated_compute =
        pd.queue_families.get_dedicated_compute_queue_index() !=
        QUEUE_INDEX_MAX_VALUE;
    bool dedicated_transfer =
        pd.queue_families.get_dedicated_transfer_queue_index() !=
        QUEUE_INDEX_MAX_VALUE;
    bool separate_compute =
        pd.queue_families.get_separate_compute_queue_index() !=
        QUEUE_INDEX_MAX_VALUE;
    bool separate_transfer =
        pd.queue_families.get_separate_transfer_queue_index() !=
        QUEUE_INDEX_MAX_VALUE;

    bool present_queue =
        pd.queue_families.get_present_queue_index(
            pd.phys_device, system_info.surface) != QUEUE_INDEX_MAX_VALUE;

    if (criteria.require_dedicated_compute_queue && !dedicated_compute)
      return Suitable::no;
    if (criteria.require_dedicated_transfer_queue && !dedicated_transfer)
      return Suitable::no;
    if (criteria.require_separate_compute_queue && !separate_compute)
      return Suitable::no;
    if (criteria.require_separate_transfer_queue && !separate_transfer)
      return Suitable::no;
    if (criteria.require_present && !present_queue &&
        !criteria.defer_surface_initialization)
      return Suitable::no;

    auto required_extensions_supported =
        pd.check_device_extension_support(criteria.required_extensions);
    if (required_extensions_supported.size() !=
        criteria.required_extensions.size())
      return Suitable::no;

    auto desired_extensions_supported =
        pd.check_device_extension_support(criteria.desired_extensions);
    if (desired_extensions_supported.size() !=
        criteria.desired_extensions.size())
      suitable = Suitable::partial;

    bool swapChainAdequate = false;
    if (criteria.defer_surface_initialization) {
      swapChainAdequate = true;
    } else if (!system_info.headless) {

      auto formats = pd.phys_device.getSurfaceFormatsKHR(system_info.surface);
      auto present_modes =
          pd.phys_device.getSurfacePresentModesKHR(system_info.surface);

      if (!formats.empty() && !present_modes.empty()) {
        swapChainAdequate = true;
      }
    }
    if (criteria.require_present && !swapChainAdequate)
      return Suitable::no;

    if (pd.device_properties.deviceType !=
        static_cast<vk::PhysicalDeviceType>(criteria.preferred_type)) {
      if (criteria.allow_any_type)
        suitable = Suitable::partial;
      else
        return Suitable::no;
    }

    bool required_features_supported =
        supports_features(pd.device_features, criteria.required_features);
    if (!required_features_supported)
      return Suitable::no;

    bool has_required_memory = false;
    bool has_preferred_memory = false;
    for (uint32_t i = 0; i < pd.mem_properties.memoryHeapCount; i++) {
      if (pd.mem_properties.memoryHeaps[i].flags &
          vk::MemoryHeapFlagBits::eDeviceLocal) {
        if (pd.mem_properties.memoryHeaps[i].size >
            criteria.required_mem_size) {
          has_required_memory = true;
        }
        if (pd.mem_properties.memoryHeaps[i].size > criteria.desired_mem_size) {
          has_preferred_memory = true;
        }
      }
    }
    if (!has_required_memory)
      return Suitable::no;
    if (!has_preferred_memory)
      suitable = Suitable::partial;

    return suitable;
  }

  static bool supports_features(vk::PhysicalDeviceFeatures supported,
                                vk::PhysicalDeviceFeatures requested) {
    // clang-format off
        if (requested.robustBufferAccess && !supported.robustBufferAccess) return false;
        if (requested.fullDrawIndexUint32 && !supported.fullDrawIndexUint32) return false;
        if (requested.imageCubeArray && !supported.imageCubeArray) return false;
        if (requested.independentBlend && !supported.independentBlend) return false;
        if (requested.geometryShader && !supported.geometryShader) return false;
        if (requested.tessellationShader && !supported.tessellationShader) return false;
        if (requested.sampleRateShading && !supported.sampleRateShading) return false;
        if (requested.dualSrcBlend && !supported.dualSrcBlend) return false;
        if (requested.logicOp && !supported.logicOp) return false;
        if (requested.multiDrawIndirect && !supported.multiDrawIndirect) return false;
        if (requested.drawIndirectFirstInstance && !supported.drawIndirectFirstInstance) return false;
        if (requested.depthClamp && !supported.depthClamp) return false;
        if (requested.depthBiasClamp && !supported.depthBiasClamp) return false;
        if (requested.fillModeNonSolid && !supported.fillModeNonSolid) return false;
        if (requested.depthBounds && !supported.depthBounds) return false;
        if (requested.wideLines && !supported.wideLines) return false;
        if (requested.largePoints && !supported.largePoints) return false;
        if (requested.alphaToOne && !supported.alphaToOne) return false;
        if (requested.multiViewport && !supported.multiViewport) return false;
        if (requested.samplerAnisotropy && !supported.samplerAnisotropy) return false;
        if (requested.textureCompressionETC2 && !supported.textureCompressionETC2) return false;
        if (requested.textureCompressionASTC_LDR && !supported.textureCompressionASTC_LDR) return false;
        if (requested.textureCompressionBC && !supported.textureCompressionBC) return false;
        if (requested.occlusionQueryPrecise && !supported.occlusionQueryPrecise) return false;
        if (requested.pipelineStatisticsQuery && !supported.pipelineStatisticsQuery) return false;
        if (requested.vertexPipelineStoresAndAtomics && !supported.vertexPipelineStoresAndAtomics) return false;
        if (requested.fragmentStoresAndAtomics && !supported.fragmentStoresAndAtomics) return false;
        if (requested.shaderTessellationAndGeometryPointSize && !supported.shaderTessellationAndGeometryPointSize) return false;
        if (requested.shaderImageGatherExtended && !supported.shaderImageGatherExtended) return false;
        if (requested.shaderStorageImageExtendedFormats && !supported.shaderStorageImageExtendedFormats) return false;
        if (requested.shaderStorageImageMultisample && !supported.shaderStorageImageMultisample) return false;
        if (requested.shaderStorageImageReadWithoutFormat && !supported.shaderStorageImageReadWithoutFormat) return false;
        if (requested.shaderStorageImageWriteWithoutFormat && !supported.shaderStorageImageWriteWithoutFormat) return false;
        if (requested.shaderUniformBufferArrayDynamicIndexing && !supported.shaderUniformBufferArrayDynamicIndexing) return false;
        if (requested.shaderSampledImageArrayDynamicIndexing && !supported.shaderSampledImageArrayDynamicIndexing) return false;
        if (requested.shaderStorageBufferArrayDynamicIndexing && !supported.shaderStorageBufferArrayDynamicIndexing) return false;
        if (requested.shaderStorageImageArrayDynamicIndexing && !supported.shaderStorageImageArrayDynamicIndexing) return false;
        if (requested.shaderClipDistance && !supported.shaderClipDistance) return false;
        if (requested.shaderCullDistance && !supported.shaderCullDistance) return false;
        if (requested.shaderFloat64 && !supported.shaderFloat64) return false;
        if (requested.shaderInt64 && !supported.shaderInt64) return false;
        if (requested.shaderInt16 && !supported.shaderInt16) return false;
        if (requested.shaderResourceResidency && !supported.shaderResourceResidency) return false;
        if (requested.shaderResourceMinLod && !supported.shaderResourceMinLod) return false;
        if (requested.sparseBinding && !supported.sparseBinding) return false;
        if (requested.sparseResidencyBuffer && !supported.sparseResidencyBuffer) return false;
        if (requested.sparseResidencyImage2D && !supported.sparseResidencyImage2D) return false;
        if (requested.sparseResidencyImage3D && !supported.sparseResidencyImage3D) return false;
        if (requested.sparseResidency2Samples && !supported.sparseResidency2Samples) return false;
        if (requested.sparseResidency4Samples && !supported.sparseResidency4Samples) return false;
        if (requested.sparseResidency8Samples && !supported.sparseResidency8Samples) return false;
        if (requested.sparseResidency16Samples && !supported.sparseResidency16Samples) return false;
        if (requested.sparseResidencyAliased && !supported.sparseResidencyAliased) return false;
        if (requested.variableMultisampleRate && !supported.variableMultisampleRate) return false;
        if (requested.inheritedQueries && !supported.inheritedQueries) return false;
    // clang-format on
    return true;
  }
};

#pragma endregion

#pragma region Device

enum class QueueType { present, graphics, compute, transfer };

class SwapchainBuilder;
class PresentBuilder;
class PipelineBuilder;
class PipelineLayoutBuilder;
class RenderPassBuilder;
class ColorAttachmentImage;
class DepthTarget;
class DepthArrayImage;
class CubemapRenderTarget;

/// Features actually enabled on this logical device (selector-stage tags).
struct DeviceCaps {
  bool samplerAnisotropy = false;
  bool swapchain = false;
  float maxSamplerAnisotropy = 1.f;
};

struct Device : Agent<vk::Device> {
  PhysicalDevice physical_device;
  vk::SurfaceKHR surface;
  QueueFamilies queue_families;
  DeviceCaps caps;
#if defined(VKB_ENABLE_VMA)
  // Non-owning. The application must keep the allocator alive until every
  // VKBuilder resource created from this Device has been destroyed.
  VmaAllocator vma_allocator = VK_NULL_HANDLE;

  bool hasVmaAllocator() const { return vma_allocator != VK_NULL_HANDLE; }

  /// Attach an application-owned allocator after the logical device exists.
  /// It must outlive every VKBuilder resource created from this Device.
  void attachVmaAllocator(VmaAllocator allocator) { vma_allocator = allocator; }
#endif

  bool hasAnisotropy() const { return caps.samplerAnisotropy; }
  bool hasSwapchain() const { return caps.swapchain; }

  uint32_t get_queue_index(QueueType type) const {
    uint32_t index = QUEUE_INDEX_MAX_VALUE;
    switch (type) {
    case QueueType::present:
      index = queue_families.get_present_queue_index(
          physical_device, surface);
      break;
    case QueueType::graphics:
      index = queue_families.get_graphics_queue_index();
      break;
    case QueueType::compute:
      index = queue_families.get_separate_compute_queue_index();
      break;
    case QueueType::transfer:
      index = queue_families.get_separate_transfer_queue_index();
      break;
    default:
      break;
    }
    return index;
  }
  // Only a compute or transfer queue type is valid. All other queue types do
  // not support a 'dedicated' queue index
  uint32_t get_dedicated_queue_index(QueueType type) const {
    uint32_t index = QUEUE_INDEX_MAX_VALUE;
    switch (type) {
    case QueueType::compute:
      index = queue_families.get_dedicated_compute_queue_index();
      break;
    case QueueType::transfer:
      index = queue_families.get_dedicated_transfer_queue_index();
      break;
    default:
      break;
    }
    return index;
  }
  vk::Queue getQueue(uint32_t family, uint32_t queueIndex = 0) const {
    return instance.getQueue(family, queueIndex);
  }

  // This function will return a empty Queue if the type is invalid.
  vk::Queue getQueue(QueueType type, uint32_t queueIndex = 0) const {
    uint32_t family_index = get_queue_index(type);
    if (family_index == QUEUE_INDEX_MAX_VALUE)
      return vk::Queue();
    return getQueue(family_index, queueIndex);
  }
  // Only a compute or transfer queue type is valid. All other queue types do
  // not support a 'dedicated' queue
  vk::Queue get_dedicated_queue(QueueType type, uint32_t queueIndex = 0) const {
    uint32_t family_index = get_dedicated_queue_index(type);
    if (family_index == QUEUE_INDEX_MAX_VALUE)
      return vk::Queue();
    return getQueue(family_index, queueIndex);
  }

  vk::CommandPool createCommandPool(QueueType qt = QueueType::graphics) {
    vk::CommandPoolCreateInfo pool_info = {};
    pool_info.queueFamilyIndex = get_queue_index(qt);
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    return instance.createCommandPool(pool_info, allocation_callbacks);
  }

  SwapchainBuilder createSwapchain();
  PresentBuilder createPresent(struct Swapchain &swapchain);
  RenderPassBuilder createRenderPass() const;
  PipelineBuilder createPipeline() const;
  PipelineBuilder createPipeline(const struct Swapchain &swapchain) const;
  PipelineLayoutBuilder createPipelineLayout() const;
  ColorAttachmentImage createColorTarget(uint32_t width, uint32_t height,
                                         vk::Format format = vk::Format::eR8G8B8A8Unorm,
                                         vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
                                         vk::ImageUsageFlags extraUsage = {});
  /// Color attachment that can also be a blit/copy destination (scene color, Canvas).
  ColorAttachmentImage createCopyableColorTarget(uint32_t width, uint32_t height,
                                                 vk::Format format = vk::Format::eR8G8B8A8Unorm);
  DepthTarget createDepthTarget(uint32_t width, uint32_t height,
                                vk::Format format = vk::Format::eD32Sfloat,
                                bool sampled = false,
                                vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
  DepthArrayImage createDepthArray(uint32_t width, uint32_t height, uint32_t layers,
                                   vk::Format format = vk::Format::eD32Sfloat);
  CubemapRenderTarget createCubemapRenderTarget(
      uint32_t size, uint32_t mipLevels = 1,
      vk::Format format = vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled |
                                  vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eStorage);

  std::vector<vk::CommandBuffer> createCommandBuffers(vk::CommandPool pool, uint32_t count = 1) {
    vk::CommandBufferAllocateInfo allocInfo = {};
    allocInfo.commandPool                 = pool;
    allocInfo.level                       = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount          = count;

    return instance.allocateCommandBuffers(allocInfo);
  }

  vk::Semaphore createSemaphore() {
    vk::SemaphoreCreateInfo semaphore_info = {};
    return instance.createSemaphore(semaphore_info);
  }

  std::vector<vk::Semaphore> createSemaphores(uint32_t count) {
    std::vector<vk::Semaphore> semaphores(count);
    for (uint32_t i = 0; i < count; ++i) {
      semaphores[i] = createSemaphore();
    }
    return semaphores;
  }

  vk::Fence createFence() {
    vk::FenceCreateInfo fence_info = {};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;
    return instance.createFence(fence_info);
  }

  std::vector<vk::Fence> createFences(uint32_t count) {
    std::vector<vk::Fence> fences(count);
    for (uint32_t i = 0; i < count; ++i) {
      fences[i] = createFence();
    }
    return fences;
  }
  
  void destroy() { instance.destroy(allocation_callbacks); }
};

// For advanced device queue setup
struct CustomQueueDescription {
  explicit CustomQueueDescription(uint32_t index, uint32_t count,
                                  std::vector<float> priorities)
      : index(index), count(count), priorities(priorities) {
    assert(count == priorities.size());
  }
  uint32_t index = 0;
  uint32_t count = 0;
  std::vector<float> priorities;
};

class DeviceBuilder {
public:
  // Any features and extensions that are requested/required in
  // PhysicalDeviceSelector are automatically enabled.
  DeviceBuilder(PhysicalDevice phys_device) {
    info.physical_device = phys_device;
    info.surface = phys_device.surface;
    info.queue_families = phys_device.queue_families;
    info.features = phys_device.features;
    info.extensions_to_enable = phys_device.extensions_to_enable;
    info.defer_surface_initialization =
        phys_device.defer_surface_initialization;
  }

  // For Advanced Users: specify the exact list of VkDeviceQueueCreateInfo's
  // needed for the application. If a custom queue setup is provided, getting
  // the queues and queue indexes is up to the application.
  DeviceBuilder &
  custom_queue_setup(std::vector<CustomQueueDescription> queue_descriptions) {
    info.queue_descriptions = queue_descriptions;
    return *this;
  }

  // Add a structure to the pNext chain of VkDeviceCreateInfo.
  // The structure must be valid when DeviceBuilder::build() is called.
  template <typename T> DeviceBuilder &add_pNext(T *structure) {
    info.pNext_chain.push_back(
        reinterpret_cast<vk::BaseOutStructure *>(structure));
    return *this;
  }

  // Provide custom allocation callbacks.
  DeviceBuilder &set_allocation_callbacks(vk::Optional<const vk::AllocationCallbacks> callbacks) {
    info.allocation_callbacks = callbacks;
    return *this;
  }

  Device build() const {

    std::vector<CustomQueueDescription> queue_descriptions;
    queue_descriptions.insert(queue_descriptions.end(),
                              info.queue_descriptions.begin(),
                              info.queue_descriptions.end());

    if (queue_descriptions.size() == 0) {
      for (uint32_t i = 0; i < info.queue_families.families.size(); i++) {
        queue_descriptions.push_back(
            CustomQueueDescription{i, 1, std::vector<float>{1.0f}});
      }
    }

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (auto &desc : queue_descriptions) {
      vk::DeviceQueueCreateInfo queue_create_info = {};
      queue_create_info.queueFamilyIndex = desc.index;
      queue_create_info.queueCount = desc.count;
      queue_create_info.pQueuePriorities = desc.priorities.data();
      queueCreateInfos.push_back(queue_create_info);
    }

    std::vector<const char *> extensions =
        helper::strvec2c_str(info.extensions_to_enable);
    if (info.surface || info.defer_surface_initialization)
      extensions.push_back({VK_KHR_SWAPCHAIN_EXTENSION_NAME});

    // VUID-VkDeviceCreateInfo-pNext-00373 - don't add pEnabledFeatures if the
    // phys_dev_features_2 is present
    bool has_phys_dev_features_2 = false;
    for (auto &pNext_struct : info.pNext_chain) {
      if (pNext_struct->sType == vk::StructureType::ePhysicalDeviceFeatures2) {
        has_phys_dev_features_2 = true;
      }
    }

    vk::DeviceCreateInfo device_create_info = {};
    helper::setup_pNext_chain<vk::DeviceCreateInfo>(device_create_info,
                                                    info.pNext_chain);
    device_create_info.flags = info.flags;
    device_create_info.queueCreateInfoCount =
        static_cast<uint32_t>(queueCreateInfos.size());
    device_create_info.pQueueCreateInfos = queueCreateInfos.data();
    device_create_info.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    device_create_info.ppEnabledExtensionNames = extensions.data();
    if (!has_phys_dev_features_2) {
      device_create_info.pEnabledFeatures = &info.features;
    }
    Device device;
    auto vkdev = info.physical_device->createDevice(
        device_create_info, info.allocation_callbacks);
    if (!vkdev) {
      throw std::runtime_error("Device create failed");
    }
    device.instance = vkdev;
    device.physical_device = info.physical_device;
    device.surface = info.surface;
    device.queue_families = info.queue_families;
    device.allocation_callbacks = info.allocation_callbacks;
    device.caps.samplerAnisotropy = info.features.samplerAnisotropy == VK_TRUE;
    device.caps.swapchain = bool(info.surface) || info.defer_surface_initialization;
    device.caps.maxSamplerAnisotropy =
        info.physical_device.properties.limits.maxSamplerAnisotropy;
    if (device.caps.maxSamplerAnisotropy < 1.f)
      device.caps.maxSamplerAnisotropy = 1.f;
    return device;
  }

private:
  struct DeviceInfo {
    vk::DeviceCreateFlags flags;
    std::vector<vk::BaseOutStructure *> pNext_chain;
    PhysicalDevice physical_device;
    vk::SurfaceKHR surface;
    bool defer_surface_initialization = false;
    QueueFamilies queue_families;
    vk::PhysicalDeviceFeatures features;
    std::vector<std::string> extensions_to_enable;
    std::vector<CustomQueueDescription> queue_descriptions;
    vk::Optional<const vk::AllocationCallbacks> allocation_callbacks = nullptr;
  } info;
};

#pragma endregion

#pragma region Swapchain

struct Swapchain : Agent<vk::SwapchainKHR> {
  vk::Device device;
  uint32_t image_count = 0;
  vk::Format image_format;
  vk::Extent2D extent = {0, 0};

  std::vector<vk::Image> swapchain_images;
  std::vector<vk::ImageView> swapchain_imageviews;
  size_t current_frame = 0;

  vk::Image getCurrentImage() {
    return swapchain_images[current_frame];
  }
  vk::ImageView getCurrentImageView() {
    return swapchain_imageviews[current_frame];
  }

  // Returns a vector of VkImage handles to the swapchain.
  std::vector<vk::Image>& get_images() {
    if (swapchain_images.empty()) {
      swapchain_images = device.getSwapchainImagesKHR(instance);
    }
    return swapchain_images;
  }

  // Returns a vector of VkImageView's to the VkImage's of the swapchain.
  // VkImageViews must be destroyed.
  std::vector<vk::ImageView>& get_image_views() {
    if (!swapchain_imageviews.empty()) return swapchain_imageviews;
    
    auto& images = get_images();
    if (images.empty())
      throw std::runtime_error("cannot get swapchain images");

    swapchain_imageviews.resize(images.size());

    for (size_t i = 0; i < images.size(); i++) {
      vk::ImageViewCreateInfo createInfo;
      createInfo.image = images[i];
      createInfo.viewType = vk::ImageViewType::e2D;
      createInfo.format = image_format;
      createInfo.components.r = vk::ComponentSwizzle::eIdentity;
      createInfo.components.g = vk::ComponentSwizzle::eIdentity;
      createInfo.components.b = vk::ComponentSwizzle::eIdentity;
      createInfo.components.a = vk::ComponentSwizzle::eIdentity;
      createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = 1;
      swapchain_imageviews[i] = device.createImageView(createInfo, allocation_callbacks);
      if (!swapchain_imageviews[i])
        throw std::runtime_error("failed_create_swapchain_image_views");
    }
    return swapchain_imageviews;
  }

  void destroy_imageviews() {
    for (auto &imageview : swapchain_imageviews) {
      device.destroyImageView(imageview, allocation_callbacks);
    }
  }

  void destroy() {
    if (device && instance) {
      if (!swapchain_imageviews.empty()) destroy_imageviews();
      device.destroySwapchainKHR(instance, allocation_callbacks);
    }
  }

  std::vector<vk::Framebuffer> createFramebuffers(vk::RenderPass render_pass,
                                                   vk::ImageView depth_view = {}) {
    auto& image_views = get_image_views();
    if (image_views.empty())
      throw std::runtime_error("cannot get swapchain image views");

    std::vector<vk::Framebuffer> swapchain_framebuffers(image_views.size());

    for (size_t i = 0; i < image_views.size(); i++) {
      vk::ImageView attachments[2] = {image_views[i], depth_view};

      vk::FramebufferCreateInfo framebuffer_info = {};
      framebuffer_info.renderPass              = render_pass;
      framebuffer_info.attachmentCount         = depth_view ? 2u : 1u;
      framebuffer_info.pAttachments            = attachments;
      framebuffer_info.width                   = extent.width;
      framebuffer_info.height                  = extent.height;
      framebuffer_info.layers                  = 1;

      swapchain_framebuffers[i] = device.createFramebuffer(framebuffer_info, allocation_callbacks);
    }
    return swapchain_framebuffers;
  }
};

class SwapchainBuilder {
public:
  explicit SwapchainBuilder(Device &device) {
    info.device = device;
    info.physical_device = device.physical_device;
    info.surface = device.surface;
    auto present = device.get_queue_index(QueueType::present);
    auto graphics = device.get_queue_index(QueueType::graphics);
    info.graphics_queue_index = graphics;
    info.present_queue_index = present;
    info.allocation_callbacks = device.allocation_callbacks;
  }
  explicit SwapchainBuilder(Device &device,
                            vk::SurfaceKHR const surface) {
    info.device = device;
    info.physical_device = device.physical_device;
    info.surface = surface;
    Device temp_device = device;
    temp_device.surface = surface;
    auto present = temp_device.get_queue_index(QueueType::present);
    auto graphics = temp_device.get_queue_index(QueueType::graphics);
    info.graphics_queue_index = graphics;
    info.present_queue_index = present;
    info.allocation_callbacks = device.allocation_callbacks;
  }
  explicit SwapchainBuilder(
      vk::PhysicalDevice const physical_device, vk::Device const device,
      vk::SurfaceKHR const surface,
      uint32_t graphics_queue_index = QUEUE_INDEX_MAX_VALUE,
      uint32_t present_queue_index = QUEUE_INDEX_MAX_VALUE) {
    info.physical_device = physical_device;
    info.device = device;
    info.surface = surface;
    info.graphics_queue_index = graphics_queue_index;
    info.present_queue_index = present_queue_index;
    if (graphics_queue_index == QUEUE_INDEX_MAX_VALUE ||
        present_queue_index == QUEUE_INDEX_MAX_VALUE) {
      QueueFamilies queue_families(physical_device.getQueueFamilyProperties());
      if (graphics_queue_index == QUEUE_INDEX_MAX_VALUE)
        info.graphics_queue_index = queue_families.get_graphics_queue_index();
      if (present_queue_index == QUEUE_INDEX_MAX_VALUE)
        info.present_queue_index =
            queue_families.get_present_queue_index(physical_device, surface);
    }
  }
  Swapchain build() const {
    if (!info.surface) {
      throw std::runtime_error("surface_handle_not_provided");
    }

    auto desired_formats = info.desired_formats;
    if (desired_formats.size() == 0)
      add_desired_formats(desired_formats);
    auto desired_present_modes = info.desired_present_modes;
    if (desired_present_modes.size() == 0)
      add_desired_present_modes(desired_present_modes);

    auto surface_support =
        query_surface_support_details(info.physical_device, info.surface);

    uint32_t image_count = surface_support.capabilities.minImageCount + 1;
    if (surface_support.capabilities.maxImageCount > 0 &&
        image_count > surface_support.capabilities.maxImageCount) {
      image_count = surface_support.capabilities.maxImageCount;
    }
    vk::SurfaceFormatKHR surface_format =
        find_surface_format(surface_support.formats, desired_formats);

    vk::Extent2D extent = find_extent(surface_support.capabilities,
                                      info.desired_width, info.desired_height);

    uint32_t image_array_layers = info.array_layer_count;
    if (surface_support.capabilities.maxImageArrayLayers <
        info.array_layer_count)
      image_array_layers = surface_support.capabilities.maxImageArrayLayers;
    if (info.array_layer_count == 0)
      image_array_layers = 1;

    uint32_t queue_family_indices[] = {info.graphics_queue_index,
                                       info.present_queue_index};

    vk::PresentModeKHR present_mode =
        find_present_mode(surface_support.present_modes, desired_present_modes);

    vk::SurfaceTransformFlagBitsKHR pre_transform = info.pre_transform;
    if (info.pre_transform == static_cast<vk::SurfaceTransformFlagBitsKHR>(0))
      pre_transform = surface_support.capabilities.currentTransform;

    vk::SwapchainCreateInfoKHR swapchain_create_info;
    helper::setup_pNext_chain<vk::SwapchainCreateInfoKHR>(swapchain_create_info,
                                                          info.pNext_chain);
    swapchain_create_info.flags = info.create_flags;
    swapchain_create_info.surface = info.surface;
    swapchain_create_info.minImageCount = image_count;
    swapchain_create_info.imageFormat = surface_format.format;
    swapchain_create_info.imageColorSpace = surface_format.colorSpace;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = image_array_layers;
    swapchain_create_info.imageUsage = info.image_usage_flags;

    if (info.graphics_queue_index != info.present_queue_index) {
      swapchain_create_info.imageSharingMode = vk::SharingMode::eConcurrent;
      swapchain_create_info.queueFamilyIndexCount = 2;
      swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
      swapchain_create_info.imageSharingMode = vk::SharingMode::eExclusive;
    }

    swapchain_create_info.preTransform = pre_transform;
    swapchain_create_info.compositeAlpha = info.composite_alpha;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = info.clipped;
    swapchain_create_info.oldSwapchain = info.old_swapchain;
    Swapchain swapchain;
    swapchain.instance = info.device.createSwapchainKHR(
        swapchain_create_info, info.allocation_callbacks);

    swapchain.device = info.device;
    swapchain.image_format = surface_format.format;
    swapchain.extent = extent;
    auto images = swapchain.get_images();
    if (images.empty()) {
      throw std::runtime_error("failed_get_swapchain_images");
    }
    swapchain.image_count = static_cast<uint32_t>(images.size());
    swapchain.allocation_callbacks = info.allocation_callbacks;
    return swapchain;
  }

  // Set the oldSwapchain member of VkSwapchainCreateInfoKHR.
  // For use in rebuilding a swapchain.
  SwapchainBuilder &set_old_swapchain(vk::SwapchainKHR old_swapchain) {
    info.old_swapchain = old_swapchain;
    return *this;
  }
  SwapchainBuilder &set_old_swapchain(Swapchain &swapchain) {
    info.old_swapchain = swapchain;
    return *this;
  }
  // Desired size of the swapchain. By default, the swapchain will use the size
  // of the window being drawn to.
  SwapchainBuilder &set_desired_extent(uint32_t width, uint32_t height) {
    info.desired_width = width;
    info.desired_height = height;
    return *this;
  }
  // When determining the surface format, make this the first to be used if
  // supported.
  SwapchainBuilder &set_desired_format(vk::SurfaceFormatKHR format) {
    info.desired_formats.insert(info.desired_formats.begin(), format);
    return *this;
  }
  // Add this swapchain format to the end of the list of formats selected from.
  SwapchainBuilder &add_fallback_format(vk::SurfaceFormatKHR format) {
    info.desired_formats.push_back(format);
    return *this;
  }
  // Use the default swapchain formats. This is done if no formats are provided.
  // Default surface format is {VK_FORMAT_B8G8R8A8_SRGB,
  // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
  SwapchainBuilder &use_default_format_selection() {
    info.desired_formats.clear();
    add_desired_formats(info.desired_formats);
    return *this;
  }
  // When determining the present mode, make this the first to be used if
  // supported.
  SwapchainBuilder &set_desired_present_mode(vk::PresentModeKHR present_mode) {
    info.desired_present_modes.insert(info.desired_present_modes.begin(),
                                      present_mode);
    return *this;
  }
  // Add this present mode to the end of the list of present modes selected
  // from.
  SwapchainBuilder &add_fallback_present_mode(vk::PresentModeKHR present_mode) {
    info.desired_present_modes.push_back(present_mode);
    return *this;
  }
  // Use the default presentation mode. This is done if no present modes are
  // provided. Default present modes: VK_PRESENT_MODE_MAILBOX_KHR with fallback
  // VK_PRESENT_MODE_FIFO_KHR
  SwapchainBuilder &use_default_present_mode_selection() {
    info.desired_present_modes.clear();
    add_desired_present_modes(info.desired_present_modes);
    return *this;
  }

  // Set the bitmask of the image usage for acquired swapchain images.
  SwapchainBuilder &set_image_usage_flags(vk::ImageUsageFlags usage_flags) {
    info.image_usage_flags = usage_flags;
    return *this;
  }
  // Add a image usage to the bitmask for acquired swapchain images.
  SwapchainBuilder &add_image_usage_flags(vk::ImageUsageFlags usage_flags) {
    info.image_usage_flags = info.image_usage_flags | usage_flags;
    return *this;
  }
  // Use the default image usage bitmask values. This is the default if no image
  // usages are provided. The default is VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
  SwapchainBuilder &use_default_image_usage_flags() {
    info.image_usage_flags = vk::ImageUsageFlagBits::eColorAttachment;
    return *this;
  }

  // Set the number of views in for multiview/stereo surface
  SwapchainBuilder &set_image_array_layer_count(uint32_t array_layer_count) {
    info.array_layer_count = array_layer_count;
    return *this;
  }

  // Set whether the Vulkan implementation is allowed to discard rendering
  // operations that affect regions of the surface that are not visible. Default
  // is true. Note: Applications should use the default of true if they do not
  // expect to read back the content of presentable images before presenting
  // them or after reacquiring them, and if their fragment shaders do not have
  // any side effects that require them to run for all pixels in the presentable
  // image.
  SwapchainBuilder &set_clipped(bool clipped = true) {
    info.clipped = clipped;
    return *this;
  }

  // Set the VkSwapchainCreateFlagBitsKHR.
  SwapchainBuilder &
  set_create_flags(vk::SwapchainCreateFlagBitsKHR create_flags) {
    info.create_flags = create_flags;
    return *this;
  }
  // Set the transform to be applied, like a 90 degree rotation. Default is no
  // transform.
  SwapchainBuilder &
  set_pre_transform_flags(vk::SurfaceTransformFlagBitsKHR pre_transform_flags) {
    info.pre_transform = pre_transform_flags;
    return *this;
  }
  // Set the alpha channel to be used with other windows in on the system.
  // Default is VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR.
  SwapchainBuilder &set_composite_alpha_flags(
      vk::CompositeAlphaFlagBitsKHR composite_alpha_flags) {
    info.composite_alpha = composite_alpha_flags;
    return *this;
  }

  // Add a structure to the pNext chain of VkSwapchainCreateInfoKHR.
  // The structure must be valid when SwapchainBuilder::build() is called.
  template <typename T> SwapchainBuilder &add_pNext(T *structure) {
    info.pNext_chain.push_back(
        reinterpret_cast<vk::BaseOutStructure *>(structure));
    return *this;
  }

  // Provide custom allocation callbacks.
  SwapchainBuilder &
  set_allocation_callbacks(vk::Optional<const vk::AllocationCallbacks> callbacks) {
    info.allocation_callbacks = callbacks;
    return *this;
  }

private:
  void add_desired_formats(std::vector<vk::SurfaceFormatKHR> &formats) const {
    formats.push_back(
        {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
    formats.push_back(
        {vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear});
  }
  void add_desired_present_modes(std::vector<vk::PresentModeKHR> &modes) const {
    modes.push_back(vk::PresentModeKHR::eMailbox);
    modes.push_back(vk::PresentModeKHR::eFifo);
  }
  struct SwapchainInfo {
    vk::PhysicalDevice physical_device;
    vk::Device device;
    std::vector<vk::BaseOutStructure *> pNext_chain;
    vk::SwapchainCreateFlagBitsKHR create_flags = 
        static_cast<vk::SwapchainCreateFlagBitsKHR>(0);
    vk::SurfaceKHR surface;
    std::vector<vk::SurfaceFormatKHR> desired_formats;
    uint32_t desired_width = 256;
    uint32_t desired_height = 256;
    uint32_t array_layer_count = 1;
    vk::ImageUsageFlags image_usage_flags =
        vk::ImageUsageFlagBits::eColorAttachment;
    uint32_t graphics_queue_index = 0;
    uint32_t present_queue_index = 0;
    vk::SurfaceTransformFlagBitsKHR pre_transform =
        static_cast<vk::SurfaceTransformFlagBitsKHR>(0);
    vk::CompositeAlphaFlagBitsKHR composite_alpha =
        vk::CompositeAlphaFlagBitsKHR::eOpaque;
    std::vector<vk::PresentModeKHR> desired_present_modes;
    bool clipped = true;
    vk::SwapchainKHR old_swapchain;
    vk::Optional<const vk::AllocationCallbacks> allocation_callbacks = nullptr;
  } info;

  vk::PresentModeKHR find_present_mode(
      std::vector<vk::PresentModeKHR> const &available_resent_modes,
      std::vector<vk::PresentModeKHR> const &desired_present_modes) const {
    for (auto const &desired_pm : desired_present_modes) {
      for (auto const &available_pm : available_resent_modes) {
        // finds the first present mode that is desired and available
        if (desired_pm == available_pm)
          return desired_pm;
      }
    }
    // only present mode required, use as a fallback
    return vk::PresentModeKHR::eFifo;
  }

  struct SurfaceSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
  };

  SurfaceSupportDetails
  query_surface_support_details(vk::PhysicalDevice phys_device,
                                vk::SurfaceKHR surface) const {
    if (!surface)
      throw std::runtime_error("surface_handle_null");

    vk::SurfaceCapabilitiesKHR capabilities =
        phys_device.getSurfaceCapabilitiesKHR(surface);

    auto formats = phys_device.getSurfaceFormatsKHR(surface);
    if (formats.empty())
      throw std::runtime_error("failed_enumerate_surface_formats");
    auto present_modes = phys_device.getSurfacePresentModesKHR(surface);
    if (present_modes.empty())
      throw std::runtime_error("failed_enumerate_present_modes");

    return SurfaceSupportDetails{capabilities, formats, present_modes};
  }

  template <typename T> T minimum(T a, T b) const { return a < b ? a : b; }
  template <typename T> T maximum(T a, T b) const { return a > b ? a : b; }

  vk::Extent2D find_extent(vk::SurfaceCapabilitiesKHR const &capabilities,
                           uint32_t desired_width,
                           uint32_t desired_height) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
      return capabilities.currentExtent;
    } else {
      vk::Extent2D actualExtent = {desired_width, desired_height};

      actualExtent.width = maximum(
          capabilities.minImageExtent.width,
          minimum(capabilities.maxImageExtent.width, actualExtent.width));
      actualExtent.height = maximum(
          capabilities.minImageExtent.height,
          minimum(capabilities.maxImageExtent.height, actualExtent.height));

      return actualExtent;
    }
  }

  vk::SurfaceFormatKHR find_surface_format(
      std::vector<vk::SurfaceFormatKHR> const &available_formats,
      std::vector<vk::SurfaceFormatKHR> const &desired_formats) const {
    for (auto const &desired_format : desired_formats) {
      for (auto const &available_format : available_formats) {
        // finds the first format that is desired and available
        if (desired_format.format == available_format.format &&
            desired_format.colorSpace == available_format.colorSpace) {
          return desired_format;
        }
      }
    }

    // use the first available one if any desired formats aren't found
    return available_formats[0];
  }
};

#pragma endregion

#pragma region RenderPass

/// Attachment view handed to a framebuffer / render pass. Distinct from SampledImage.
struct AttachmentView {
  vk::ImageView view{};
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
  vk::ImageLayout layout = vk::ImageLayout::eColorAttachmentOptimal;
};

/// Render pass plus the attachment signature pipelines and framebuffers must match.
struct BuiltRenderPass {
  vk::RenderPass handle{};
  /// Subpass color attachments (not including resolve targets). Pipelines must
  /// match this count, not the total VkAttachmentDescription count.
  uint32_t colorAttachmentCount = 0;
  /// 1x resolve targets paired with multisampled color attachments.
  uint32_t resolveAttachmentCount = 0;
  bool hasDepth = false;
  bool colorsSampledAfter = false;
  bool depthSampledAfter = false;
  vk::ImageLayout colorFinalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  vk::ImageLayout depthFinalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  operator vk::RenderPass() const { return handle; }
  explicit operator bool() const { return static_cast<bool>(handle); }

  uint32_t attachmentCount() const {
    return colorAttachmentCount + resolveAttachmentCount + (hasDepth ? 1u : 0u);
  }

  vk::Framebuffer createFramebuffer(const Device &device, uint32_t width, uint32_t height,
                                    const std::vector<vk::ImageView> &views) const {
    if (uint32_t(views.size()) != attachmentCount())
      throw std::runtime_error("framebuffer attachment count does not match render pass");
    vk::FramebufferCreateInfo framebuffer_info{};
    framebuffer_info.renderPass = handle;
    framebuffer_info.attachmentCount = uint32_t(views.size());
    framebuffer_info.pAttachments = views.data();
    framebuffer_info.width = width;
    framebuffer_info.height = height;
    framebuffer_info.layers = 1;
    return device->createFramebuffer(framebuffer_info, device.allocation_callbacks);
  }

  vk::Framebuffer createFramebuffer(const Device &device, uint32_t width, uint32_t height,
                                    const std::vector<AttachmentView> &atts) const {
    if (uint32_t(atts.size()) != attachmentCount())
      throw std::runtime_error("framebuffer attachment count does not match render pass");
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
      if (atts[i].aspect & vk::ImageAspectFlagBits::eDepth)
        throw std::runtime_error("color attachment slot received a depth view");
    }
    if (hasDepth) {
      const AttachmentView &depth = atts.back();
      if (!(depth.aspect & vk::ImageAspectFlagBits::eDepth))
        throw std::runtime_error("depth attachment slot missing depth aspect");
    }
    std::vector<vk::ImageView> views;
    views.reserve(atts.size());
    for (const auto &a : atts) views.push_back(a.view);
    return createFramebuffer(device, width, height, views);
  }
};

class RenderPassBuilder;
class SubpassBuilder {
public:
  SubpassBuilder& addAttachmentRef(int index, vk::ImageLayout layout);
  SubpassBuilder& addResolveAttachment(int index, vk::ImageLayout layout) {
    hasResolve = true;
    resolveIdx = index;
    resolveLayout = layout;
    return *this;
  }
  SubpassBuilder& setDepthStencilAttachment(int index, vk::ImageLayout layout) {
    hasDepth = true;
    depthIdx = index;
    depthLayout = layout;
    return *this;
  }
  vk::SubpassDescription build(RenderPassBuilder& rpb, std::vector<vk::AttachmentReference>& refs);
protected:
  std::vector<int> idx;
  std::vector<vk::ImageLayout> layouts;
  bool hasDepth = false;
  int depthIdx = -1;
  vk::ImageLayout depthLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  bool hasResolve = false;
  int resolveIdx = -1;
  vk::ImageLayout resolveLayout = vk::ImageLayout::eColorAttachmentOptimal;
};

class RenderPassBuilder {
public:
  RenderPassBuilder(Device const &device) 
    : device(device) {
  }
  BuiltRenderPass build() {
    vk::RenderPassCreateInfo info;
    info.attachmentCount = uint32_t(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = uint32_t(subpass.size());
    info.pSubpasses = subpass.data();
    info.dependencyCount = uint32_t(dependencies.size());
    info.pDependencies = dependencies.data();
    BuiltRenderPass out;
    out.handle = device->createRenderPass(info, device.allocation_callbacks);
    out.colorAttachmentCount = colorCount;
    out.resolveAttachmentCount = resolveCount;
    out.hasDepth = hasDepthAtt;
    out.colorsSampledAfter = colorsSampledAfter;
    out.depthSampledAfter = depthSampledAfter;
    out.colorFinalLayout = colorFinalLayout;
    out.depthFinalLayout = depthFinalLayout;
    return out;
  }
   
  RenderPassBuilder& addAttachment(const vk::AttachmentDescription& ad) {
    attachments.push_back(ad);
    return *this;
  }

  RenderPassBuilder& addColorAttachment(vk::Format image_format, 
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eDontCare,
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eDontCare,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
  ) {
    attachments.push_back(
      vk::AttachmentDescription() 
        .setFormat(image_format) 
        .setSamples(samples)
        .setLoadOp(loadOp)
        .setStoreOp(storeOp)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
    );
    ++colorCount;
    colorFinalLayout = vk::ImageLayout::eColorAttachmentOptimal;
    return *this;
  }

  /// Color attachment stored and left in SHADER_READ_ONLY so it can become a SampledImage.
  RenderPassBuilder& addSampledColorAttachment(
      vk::Format image_format,
      vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
      vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
    addColorAttachment(image_format, loadOp, vk::AttachmentStoreOp::eStore, samples);
    attachments.back().setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    colorsSampledAfter = true;
    colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return *this;
  }

  /// 1x resolved color attachment for a multisampled render pass. Becomes a SampledImage.
  /// loadOp defaults to DONT_CARE: the resolve overwrites the image, and CLEAR
  /// on the resolve target crashes MoltenVK/Metal.
  RenderPassBuilder& addResolveColorAttachment(
      vk::Format image_format,
      vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eDontCare) {
    attachments.push_back(
      vk::AttachmentDescription()
        .setFormat(image_format)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(loadOp)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
    );
    ++resolveCount;
    colorsSampledAfter = true;
    colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return *this;
  }

  RenderPassBuilder& addPresentAttachment(vk::Format image_format, 
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eLoad,
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore
  ) {
    attachments.push_back(
      vk::AttachmentDescription() 
        .setFormat(image_format) 
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(loadOp)
        .setStoreOp(storeOp)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::ePresentSrcKHR)
    );
    ++colorCount;
    colorFinalLayout = vk::ImageLayout::ePresentSrcKHR;
    return *this;
  }

  RenderPassBuilder& addDepthAttachment(vk::Format image_format,
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
    vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eDontCare,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
  ) {
    attachments.push_back(
      vk::AttachmentDescription()
        .setFormat(image_format)
        .setSamples(samples)
        .setLoadOp(loadOp)
        .setStoreOp(storeOp)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    );
    hasDepthAtt = true;
    depthFinalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    return *this;
  }

  /// Depth attachment stored and left in SHADER_READ_ONLY (e.g. GBuffer hwDepth, CSM).
  RenderPassBuilder& addSampledDepthAttachment(
      vk::Format image_format,
      vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
      vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
    addDepthAttachment(image_format, loadOp, vk::AttachmentStoreOp::eStore, samples);
    attachments.back().setFinalLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    depthSampledAfter = true;
    depthFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return *this;
  }

  /// EXTERNAL→subpass 0 and 0→EXTERNAL barriers for write-then-sample in the same CB.
  RenderPassBuilder& addExternalShaderReadDependencies() {
    vk::PipelineStageFlags dstWrite = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::AccessFlags dstAccess = vk::AccessFlagBits::eColorAttachmentWrite;
    vk::PipelineStageFlags srcWrite = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::AccessFlags srcAccess = vk::AccessFlagBits::eColorAttachmentWrite;
    if (hasDepthAtt) {
      dstWrite |= vk::PipelineStageFlagBits::eEarlyFragmentTests;
      dstAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
      srcWrite |= vk::PipelineStageFlagBits::eLateFragmentTests;
      srcAccess |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    }
    addDependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader, dstWrite,
                  vk::AccessFlagBits::eShaderRead, dstAccess);
    addDependency(0, VK_SUBPASS_EXTERNAL, srcWrite, vk::PipelineStageFlagBits::eFragmentShader,
                  srcAccess, vk::AccessFlagBits::eShaderRead);
    return *this;
  }

  RenderPassBuilder& addSubpass(const vk::SubpassDescription& sd) {
    subpass.push_back(sd);
    return *this;
  }

  RenderPassBuilder& addSubpass(SubpassBuilder& sb) {
    refs.push_back({});
    subpass.push_back(sb.build(*this, refs.back()));
    return *this;
  }

  RenderPassBuilder& addDependency(uint32_t src, uint32_t dst, 
    vk::PipelineStageFlags srcFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput, 
    vk::PipelineStageFlags dstFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput, 
    vk::AccessFlags srcAFlags = {}, 
    vk::AccessFlags dstAFlags = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
    vk::DependencyFlags depFlags = {}
  ) {
    vk::SubpassDependency sd(src, dst, srcFlags, dstFlags, srcAFlags, dstAFlags, depFlags);
    dependencies.push_back(sd);
    return *this;
  }

  friend class SubpassBuilder;
private:
  Device const& device;
  std::vector<vk::AttachmentDescription> attachments;
  std::vector<vk::SubpassDescription> subpass;
  std::vector<std::vector<vk::AttachmentReference> > refs;
  std::vector<vk::AttachmentReference> depthRefs;
  std::vector<vk::AttachmentReference> resolveRefs;
  std::vector<vk::SubpassDependency> dependencies;
  uint32_t colorCount = 0;
  uint32_t resolveCount = 0;
  bool hasDepthAtt = false;
  bool colorsSampledAfter = false;
  bool depthSampledAfter = false;
  vk::ImageLayout colorFinalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  vk::ImageLayout depthFinalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
};


inline SubpassBuilder& SubpassBuilder::addAttachmentRef(int index, vk::ImageLayout layout) {
  idx.push_back(index);
  layouts.push_back(layout);
  return *this;
}
inline vk::SubpassDescription SubpassBuilder::build(RenderPassBuilder& rpb, std::vector<vk::AttachmentReference>& refs) {
  for (int i = 0; i < idx.size(); ++i) {
    vk::AttachmentReference ar;
    ar.attachment = idx[i];
    ar.layout = layouts[i];
    refs.push_back(ar);
  }

  vk::SubpassDescription sd;
  sd.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  sd.colorAttachmentCount = refs.size();
  sd.pColorAttachments = refs.data();
  if (hasResolve) {
    rpb.resolveRefs.push_back(vk::AttachmentReference{uint32_t(resolveIdx), resolveLayout});
    sd.pResolveAttachments = &rpb.resolveRefs.back();
  }
  if (hasDepth) {
    rpb.depthRefs.push_back(
        vk::AttachmentReference{uint32_t(depthIdx), depthLayout});
    sd.pDepthStencilAttachment = &rpb.depthRefs.back();
  }
  return sd;
}

#pragma endregion

#pragma region Pipeline


class VertexInputStateBuilder {
private:
  friend class PipelineBuilder;
  vk::PipelineVertexInputStateCreateInfo build() {
    vk::PipelineVertexInputStateCreateInfo info = {};
    info.flags = flags;
    info.vertexAttributeDescriptionCount = static_cast<uint32_t>(input_attributes.size());
    info.pVertexAttributeDescriptions = input_attributes.data();
    info.vertexBindingDescriptionCount = static_cast<uint32_t>(input_bindings.size());
    info.pVertexBindingDescriptions = input_bindings.data();
    return info;
  }
public:
  template<class T>
  VertexInputStateBuilder& addInputBinding() {
    uint32_t binding = input_bindings.size();
    input_bindings.push_back(T::getBindingDescription(binding));
    return *this;
  }

  template<class T>
  VertexInputStateBuilder& addAttributeDescription(int binding = -1) {
    if (binding == -1) binding = input_bindings.size();
    const auto& vec = T::getAttributeDescription(binding-1);
    input_attributes.insert(input_attributes.end(), vec.begin(), vec.end());
    return *this;
  }

  vk::PipelineVertexInputStateCreateFlags flags;
  std::vector< vk::VertexInputBindingDescription > input_bindings;
  std::vector< vk::VertexInputAttributeDescription > input_attributes;
};

class PipelineLayoutBuilder {
public:
  explicit PipelineLayoutBuilder(const Device &device) : device(device) {}

  PipelineLayoutBuilder &set(vk::DescriptorSetLayout layout) {
    layouts.push_back(layout);
    return *this;
  }
  PipelineLayoutBuilder &push(vk::ShaderStageFlags stages, uint32_t size, uint32_t offset = 0) {
    ranges.push_back(vk::PushConstantRange{stages, offset, size});
    return *this;
  }
  template <class T>
  PipelineLayoutBuilder &push(vk::ShaderStageFlags stages, uint32_t offset = 0) {
    return push(stages, uint32_t(sizeof(T)), offset);
  }
  vk::PipelineLayout build() const {
    vk::PipelineLayoutCreateInfo info{};
    info.setLayoutCount = uint32_t(layouts.size());
    info.pSetLayouts = layouts.data();
    info.pushConstantRangeCount = uint32_t(ranges.size());
    info.pPushConstantRanges = ranges.data();
    return device->createPipelineLayout(info, device.allocation_callbacks);
  }

private:
  const Device &device;
  std::vector<vk::DescriptorSetLayout> layouts;
  std::vector<vk::PushConstantRange> ranges;
};

class PipelineBuilder {
public:
  explicit PipelineBuilder(const Device &device) : device(device) {}
  PipelineBuilder(const Device &device, const Swapchain &swapchain)
    : device(device), swapchain(&swapchain), default_extent(swapchain.extent) {
  }

  PipelineBuilder &setPipelineLayout(vk::PipelineLayout layout) {
    pipeline_layout = layout;
    return *this;
  }
  PipelineBuilder &setExtent(vk::Extent2D extent) {
    default_extent = extent;
    return *this;
  }
  PipelineBuilder &setDepthStencil(bool testEnable, bool writeEnable,
                                   vk::CompareOp compareOp = vk::CompareOp::eLess) {
    depth_stencil_set = true;
    depth_stencil.depthTestEnable = testEnable;
    depth_stencil.depthWriteEnable = writeEnable;
    depth_stencil.depthCompareOp = compareOp;
    depth_stencil.depthBoundsTestEnable = false;
    depth_stencil.stencilTestEnable = false;
    return *this;
  }
  PipelineBuilder &setDepthStencil(vk::PipelineDepthStencilStateCreateInfo ds) {
    depth_stencil_set = true;
    depth_stencil = ds;
    return *this;
  }
  PipelineBuilder &setColorAttachmentCount(uint32_t count) {
    use_default_color_blending = false;
    fillOpaqueBlend(count);
    return *this;
  }
  PipelineBuilder &setDepthBias(float constantFactor, float slopeFactor, float clamp = 0.f) {
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = constantFactor;
    rasterizer.depthBiasSlopeFactor = slopeFactor;
    rasterizer.depthBiasClamp = clamp;
    use_default_rasterizer = false;
    return *this;
  }

  vk::Pipeline build(const BuiltRenderPass &rp, uint32_t subpass = 0) {
    if (use_default_color_blending)
      fillOpaqueBlend(rp.colorAttachmentCount);
    if (rp.hasDepth && !depth_stencil_set)
      setDepthStencil(true, true, vk::CompareOp::eLess);
    return buildHandle(rp.handle, subpass);
  }

  PipelineBuilder& setAlphaBlending(uint32_t count = 1) {
    use_default_color_blending = false;
    vk::PipelineColorBlendAttachmentState att{};
    att.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    att.blendEnable = true;
    att.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    att.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    att.colorBlendOp = vk::BlendOp::eAdd;
    att.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    att.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    att.alphaBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachments.assign(count, att);
    return *this;
  }

private:
  vk::Pipeline buildHandle(vk::RenderPass render_pass, uint32_t subpass = 0) {
    if (use_default_input_state) setVertexInputState(VertexInputStateBuilder().build());
    else if (use_input_state_builder) setVertexInputState(input_state_builder.build());
    
    if (use_default_input_assembly) setInputAssemblyState();
    if (use_default_viewport) addViewport();
    if (use_default_scissor) addScissor();
    if (use_default_rasterizer) setRasterizer();
    if (use_default_multisampling) setMultisampler();
    if (use_default_color_blending) fillOpaqueBlend(1);

    vk::PipelineDynamicStateCreateInfo dynamic_info;
    dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_info.pDynamicStates    = dynamic_states.data();

    if (!pipeline_layout)
      throw std::runtime_error("PipelineBuilder: setPipelineLayout is required");
    vk::PipelineLayout layout = pipeline_layout;

    if (dynamic_states.size() && viewports.empty()) {
      viewports.push_back(vk::Viewport{0.f, 0.f, 1.f, 1.f, 0.f, 1.f});
    }
    if (dynamic_states.size() && scissors.empty()) {
      scissors.push_back(vk::Rect2D{{0, 0}, {1, 1}});
    }

    vk::PipelineViewportStateCreateInfo viewport_state;
    viewport_state.viewportCount = uint32_t(viewports.size());
    viewport_state.pViewports    = viewports.data();
    viewport_state.scissorCount  = uint32_t(scissors.size());
    viewport_state.pScissors     = scissors.data();

    color_blending.logicOpEnable     = false;
    color_blending.logicOp           = vk::LogicOp::eCopy;
    color_blending.attachmentCount   = uint32_t(colorBlendAttachments.size());
    color_blending.pAttachments      = colorBlendAttachments.data();

    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.stageCount          = uint32_t(shader_stages.size());
    pipeline_info.pStages             = shader_stages.data();
    pipeline_info.pVertexInputState   = &input_state;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_info;
    pipeline_info.layout              = layout;
    pipeline_info.renderPass          = render_pass;
    pipeline_info.subpass             = subpass;

    auto result = device->createGraphicsPipeline(nullptr, pipeline_info);
    if (result.result != vk::Result::eSuccess)
      throw std::runtime_error("failed_create_graphics_pipeline");
    return result.value;
  }

public:
  PipelineBuilder& useClassicPipeline(const std::vector<uint32_t>& vert, const std::vector<uint32_t>& frag) {
    vk::ShaderModule vert_module = createShaderModule(device, vert);
    vk::ShaderModule frag_module = createShaderModule(device, frag);
    return useClassicPipeline(vert_module, frag_module);
  }

  PipelineBuilder& useClassicPipeline(vk::ShaderModule vert_module, vk::ShaderModule frag_module) {
    addVertexStage(vert_module);
    addFragmentStage(frag_module);
    return *this;
  }

  PipelineBuilder& addVertexStage(vk::ShaderModule vert_module, std::string pName = "main") {
    shader_entry_names.push_back(pName);

    vk::PipelineShaderStageCreateInfo stage_info = {};
    stage_info.stage  = vk::ShaderStageFlagBits::eVertex;
    stage_info.module = vert_module;
    stage_info.pName  = shader_entry_names.back().c_str();

    shader_stages.push_back(stage_info);

    return *this;
  }

  PipelineBuilder& addFragmentStage(vk::ShaderModule frag_module, std::string pName = "main") {
    shader_entry_names.push_back(pName);

    vk::PipelineShaderStageCreateInfo stage_info = {};
    stage_info.stage  = vk::ShaderStageFlagBits::eFragment;
    stage_info.module = frag_module;
    stage_info.pName  = shader_entry_names.back().c_str();
    shader_stages.push_back(stage_info);
    return *this;
  }

  PipelineBuilder& addTessellationControlStage(vk::ShaderModule tess_module, std::string pName = "main") {
    shader_entry_names.push_back(pName);

    vk::PipelineShaderStageCreateInfo stage_info = {};
    stage_info.stage  = vk::ShaderStageFlagBits::eTessellationControl;
    stage_info.module = tess_module;
    stage_info.pName  = shader_entry_names.back().c_str();
    shader_stages.push_back(stage_info);
    return *this;
  }

  PipelineBuilder& addTessellationEvaluationStage(vk::ShaderModule tess_module, std::string pName = "main") {
    shader_entry_names.push_back(pName);

    vk::PipelineShaderStageCreateInfo stage_info = {};
    stage_info.stage  = vk::ShaderStageFlagBits::eTessellationEvaluation;
    stage_info.module = tess_module;
    stage_info.pName  = shader_entry_names.back().c_str();
    shader_stages.push_back(stage_info);
    return *this;
  }

  PipelineBuilder& addComputeStage(vk::ShaderModule compute_module, std::string pName = "main") {
    shader_entry_names.push_back(pName);

    vk::PipelineShaderStageCreateInfo stage_info = {};
    stage_info.stage  = vk::ShaderStageFlagBits::eCompute;
    stage_info.module = compute_module;
    stage_info.pName  = shader_entry_names.back().c_str();
    shader_stages.push_back(stage_info);
    return *this;
  }

  PipelineBuilder& setVertexInputState(VertexInputStateBuilder& input_state) {
    use_default_input_state = false;
    use_input_state_builder = true;
    this->input_state_builder = input_state;
    return *this;
  }

  PipelineBuilder& setVertexInputState(vk::PipelineVertexInputStateCreateInfo input_state) {
    use_default_input_state = false;
    use_input_state_builder = false;
    this->input_state = input_state;
    return *this;
  }

  PipelineBuilder& setInputAssemblyState(vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList, bool primitiveRestartEnable = false) {
    use_default_input_assembly = false;
    vk::PipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.topology               = topology;
    input_assembly.primitiveRestartEnable = primitiveRestartEnable;
    this->input_assembly = input_assembly;
    return *this;
  }

  PipelineBuilder& setInputAssemblyState(vk::PipelineInputAssemblyStateCreateInfo input_assembly) {
    use_default_input_assembly = false;
    this->input_assembly = input_assembly;
    return *this;
  }

  PipelineBuilder& addScissor(vk::Rect2D scissor = {}) {
    use_default_scissor = false;
    if (scissor.extent == vk::Extent2D())
      scissor.extent = default_extent.width ? default_extent : vk::Extent2D{1, 1};
    scissors.push_back(scissor);
    return *this;
  }

  PipelineBuilder& addViewport( float width = -1.0f, float height = -1.0f, float x = 0.0f, float y = 0.0f, float minDepth = 0.0f, float maxDepth = 1.0f) {
    vk::Viewport viewport = {};
    viewport.x          = x;
    viewport.y          = y;
    const float dw = default_extent.width ? float(default_extent.width) : 1.f;
    const float dh = default_extent.height ? float(default_extent.height) : 1.f;
    viewport.width      = width == -1.0f ? dw : width;
    viewport.height     = height == -1.0f ? dh : height;
    viewport.minDepth   = minDepth;
    viewport.maxDepth   = maxDepth;

    addViewport(viewport);
    return *this;
  } 

  PipelineBuilder& addViewport(vk::Viewport viewport) {
    use_default_viewport = false;
    viewports.push_back(viewport);
    return *this;
  }

  PipelineBuilder& setRasterizer(vk::PolygonMode polygonMode = vk::PolygonMode::eFill,
    bool depthClampEnable = false, bool rasterizerDiscardEnable = false, 
    float lineWidth = 1.0f, vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack, 
    vk::FrontFace frontFace = vk::FrontFace::eClockwise, bool depthBiasEnable = false
    ) {
    use_default_rasterizer = false;
    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.depthClampEnable        = depthClampEnable;
    rasterizer.rasterizerDiscardEnable = rasterizerDiscardEnable;
    rasterizer.polygonMode             = polygonMode;
    rasterizer.lineWidth               = lineWidth;
    rasterizer.cullMode                = cullMode;
    rasterizer.frontFace               = frontFace;
    rasterizer.depthBiasEnable         = depthBiasEnable;
    this->rasterizer = rasterizer;
    return *this;
  }

  PipelineBuilder& setRasterizer(vk::PipelineRasterizationStateCreateInfo rasterizer) {
    use_default_rasterizer = false;
    this->rasterizer = rasterizer;
    return *this;
  }

  PipelineBuilder& setMultisampler(bool sampleShadingEnable = false, 
    vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1) {
    use_default_multisampling = false;
    multisampling.sampleShadingEnable  = sampleShadingEnable;
    multisampling.rasterizationSamples = rasterizationSamples;
    return *this;
  }


  PipelineBuilder& setMultisampler(vk::PipelineMultisampleStateCreateInfo multisampling) {
    use_default_multisampling = false;
    this->multisampling = multisampling;
    return *this;
  }

  PipelineBuilder& setColorBlending() {
    fillOpaqueBlend(1);
    return *this;
  }

  PipelineBuilder& setColorBlending(vk::PipelineColorBlendStateCreateInfo color_blending) {
    use_default_color_blending = false;
    this->color_blending = color_blending;
    return *this;
  }

  PipelineBuilder& setDynamicStatesViewportScissor() {
    use_default_dynamic_states = false;
    dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    return *this;
  }


  PipelineBuilder& setDynamicStates(std::vector<vk::DynamicState> ds) {
    use_default_dynamic_states = false;
    dynamic_states = ds;
    return *this;
  }

  PipelineBuilder& setDynamicStates(const std::initializer_list<vk::DynamicState>& v) { 
    use_default_dynamic_states = false;
    dynamic_states = v;
    return *this;
  }

  static vk::ShaderModule createShaderModule(const vk::Device& d, const std::vector<uint32_t>& code) {
    vk::ShaderModuleCreateInfo create_info = {};
    create_info.codeSize = code.size() * 4;
    create_info.pCode    = code.data();
    return d.createShaderModule(create_info);
  }

private:
  void fillOpaqueBlend(uint32_t count) {
    use_default_color_blending = false;
    vk::PipelineColorBlendAttachmentState att{};
    att.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    att.blendEnable = false;
    colorBlendAttachments.assign(count, att);
  }

  Device const& device;
  const Swapchain* swapchain = nullptr;
  vk::Extent2D default_extent{};
  vk::PipelineLayout pipeline_layout{};

  std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
  // Cannot use vector, since the string object will deconstruct during pushing back.
  std::deque<std::string> shader_entry_names; 

  bool use_default_dynamic_states = true;
  std::vector<vk::DynamicState> dynamic_states;

  bool use_default_input_state = true;
  vk::PipelineVertexInputStateCreateInfo input_state;
  bool use_input_state_builder = true;
  VertexInputStateBuilder input_state_builder;

  bool use_default_input_assembly = true;
  vk::PipelineInputAssemblyStateCreateInfo input_assembly;

  bool use_default_viewport = true;
  std::vector<vk::Viewport> viewports;

  bool use_default_scissor = true;
  std::vector<vk::Rect2D> scissors;

  bool use_default_rasterizer = true;
  vk::PipelineRasterizationStateCreateInfo rasterizer;

  bool use_default_multisampling = true;
  vk::PipelineMultisampleStateCreateInfo multisampling;

  bool depth_stencil_set = false;
  vk::PipelineDepthStencilStateCreateInfo depth_stencil{};

  bool use_default_color_blending = true;
  std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
  vk::PipelineColorBlendStateCreateInfo color_blending;
};


#pragma endregion

#pragma region Present

/// In-flight slot after a successful acquire. Index host-visible resources with
/// this (UBO/VBO ping-pong) so CPU never overwrites a buffer GPU still reads.
struct FrameSlot {
  uint32_t index = 0;
  /// CPU-sync path: executeImmediately / waitIdle / one-shot mesh upload.
  static FrameSlot gpuIdle() { return FrameSlot{~0u}; }
  bool isGpuIdle() const { return index == ~0u; }
};

struct Present;
struct RecordingCmd;
struct InRenderPass;
struct RecordedCmd;
struct AcquiredFrame;
struct BoundSet;

// Each thread should have a single struct for commands recording
struct Present {
  Swapchain* swapchain = nullptr;
  Device* device = nullptr;

  Present() = default;
  Present(Device& device, Swapchain& swapchain)
    : device(&device), swapchain(&swapchain) {}
  Present(const Present &) = delete;
  Present &operator=(const Present &) = delete;
  Present(Present &&o) noexcept { moveFrom(o); }
  Present &operator=(Present &&o) noexcept {
    if (this != &o) {
      destroy();
      moveFrom(o);
    }
    return *this;
  }
  ~Present() {
    if (command_pool || !in_flight_fences.empty() || !framebuffers.empty())
      destroy();
  }

  vk::Queue graphics_queue;
  vk::Queue present_queue;

  vk::CommandPool                command_pool;
  std::vector<vk::CommandBuffer> command_buffers;
  std::vector<vk::Framebuffer>   framebuffers;

  std::vector<vk::Fence> in_flight_fences;
  std::vector<vk::Fence> image_in_flight;

  std::vector<vk::Semaphore> available_semaphores;
  std::vector<vk::Semaphore> finished_semaphore;
  vk::RenderPass render_pass;
  vk::ImageView depth_attachment{};
  uint32_t last_presented_image_index = 0;
  uint32_t acquired_image_index = 0;
  bool has_acquired_image = false;
  // Set when acquire/present reports the swapchain is out-of-date/suboptimal.
  // The owner is responsible for recreating the swapchain at a safe time
  // (important on Android, where recreating mid-rotation crashes the driver).
  bool needs_recreate = false;
  // When true (legacy default), drawFrame() blocks on this frame's fence
  // before presenting, so callers may free per-frame GPU resources (vertex
  // buffers etc.) as soon as drawFrame() returns.
  // Set to false for real frames-in-flight: drawFrame() submits and presents
  // without waiting; the only stall is the per-slot fence wait in
  // acquireForFrame(). Callers must then keep per-frame resources alive until
  // that slot's fence has signaled (see waitForFrameSlot / waitForAllFrames).
  bool synchronous_frames = true;
  // Independent of swapchain image_count. Available (acquire) semaphores are
  // indexed by this count; submit-finished semaphores are indexed by the
  // acquired swapchain image instead (see getFinishedSemaphore), so the WSI
  // engine never observes a reused present-wait semaphore.
  uint32_t frames_in_flight = 2;
  // Invoked after this frame's submission has completed on the GPU, before
  // presentKHR. Setting a hook forces a fence wait for that frame even when
  // synchronous_frames is false — leave it unset on hot paths.
  std::function<void(uint32_t renderedImageIndex)> after_render_before_present;

  FrameSlot currentSlot() const {
    return FrameSlot{swapchain ? uint32_t(swapchain->current_frame) : 0u};
  }

  friend struct RecordingCmd;
  friend struct InRenderPass;
  friend struct RecordedCmd;
  friend struct AcquiredFrame;

private:
  vk::CommandBuffer& getCurrentCommandBuffer() {
    return command_buffers[swapchain->current_frame];
  }

  vk::Framebuffer& getCurrentFrameBuffer() {
    uint32_t idx = has_acquired_image ? acquired_image_index
                                      : static_cast<uint32_t>(swapchain->current_frame);
    return framebuffers[idx];
  }

public:

  void acquireForFrame() {
    if (has_acquired_image) return;
    if (frames_in_flight > 0 && swapchain->current_frame >= frames_in_flight)
      swapchain->current_frame = 0;
    auto dev = *device;
    (void)dev->waitForFences(1, &getInFlightFence(), VK_TRUE, UINT64_MAX);

    vk::Result result = dev->acquireNextImageKHR(
        *swapchain, UINT64_MAX, getAvailableSemaphore(), vk::Fence{}, &acquired_image_index);
    if (result == vk::Result::eErrorOutOfDateKHR) {
      // Defer swapchain recreation to the owner instead of rebuilding inline.
      // On Android the surface can be mid-rotation here, and rebuilding the
      // swapchain/image-views against a not-ready surface crashes the driver.
      needs_recreate = true;
      return;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
      throw std::runtime_error("failed to acquire swapchain image.");
    has_acquired_image = true;
  }

  AcquiredFrame acquire();
  RecordingCmd begin();
  RecordedCmd end();
  InRenderPass beginRenderPass(vk::RenderPass render_pass,
                               const vk::ClearValue *clearValues,
                               uint32_t clearCount);
  InRenderPass beginRenderPass(vk::RenderPass render_pass,
    vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));
  RecordingCmd endRenderPass();

  vk::Fence& getInFlightFence() {
    return in_flight_fences[swapchain->current_frame];
  }
  vk::Fence& getImageInFlight(uint32_t idx) {
    return image_in_flight[idx];
  }
  vk::Semaphore& getAvailableSemaphore() {
    return available_semaphores[swapchain->current_frame];
  }

  // Submit-finished semaphores are waited on by vkQueuePresentKHR, which
  // signals nothing the host can wait on: a vkQueueSubmit fence does NOT cover
  // the presentation engine's use, so reusing them by frame slot violates
  // VUID-vkQueueSubmit-pSignalSemaphores-00067 whenever frames_in_flight <
  // image_count (a classic driver-TDR source). Index by the ACQUIRED IMAGE
  // instead: re-acquiring image i guarantees the previous presentation of i
  // completed, so finished_semaphore[i] is safe to reuse. See
  // https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
  vk::Semaphore& getFinishedSemaphore() {
    return finished_semaphore[acquired_image_index];
  }

  /// Block until the given frame slot's last submission has completed.
  /// Use for reclaiming per-frame resources when synchronous_frames == false.
  void waitForFrameSlot(size_t slot) {
    if (!device || slot >= in_flight_fences.size()) return;
    (void)(*device)->waitForFences(1, &in_flight_fences[slot], VK_TRUE, UINT64_MAX);
  }

  /// Block until every in-flight frame has completed. Scoped alternative to a
  /// device-wide waitIdle before destroying swapchain-dependent resources.
  void waitForAllFrames() {
    if (!device || in_flight_fences.empty()) return;
    (void)(*device)->waitForFences(uint32_t(in_flight_fences.size()),
                                   in_flight_fences.data(), VK_TRUE, UINT64_MAX);
  }

  void moveFrom(Present &o) noexcept {
    swapchain = o.swapchain; o.swapchain = nullptr;
    device = o.device; o.device = nullptr;
    graphics_queue = o.graphics_queue;
    present_queue = o.present_queue;
    command_pool = o.command_pool; o.command_pool = vk::CommandPool{};
    command_buffers = std::move(o.command_buffers);
    framebuffers = std::move(o.framebuffers);
    in_flight_fences = std::move(o.in_flight_fences);
    image_in_flight = std::move(o.image_in_flight);
    available_semaphores = std::move(o.available_semaphores);
    finished_semaphore = std::move(o.finished_semaphore);
    render_pass = o.render_pass;
    depth_attachment = o.depth_attachment;
    last_presented_image_index = o.last_presented_image_index;
    acquired_image_index = o.acquired_image_index;
    has_acquired_image = o.has_acquired_image; o.has_acquired_image = false;
    needs_recreate = o.needs_recreate;
    synchronous_frames = o.synchronous_frames;
    frames_in_flight = o.frames_in_flight;
    after_render_before_present = std::move(o.after_render_before_present);
  }

  /// Destroy all owned Vulkan objects. Safe to call more than once; the
  /// destructor invokes this if fences / command pool are still live.
  void destroy() {
    if (!device || !device->instance) return;
    // Full idle: semaphores below may still be referenced by an outstanding
    // presentKHR, which no fence covers. destroy() is a teardown-only path.
    (void)(*device)->waitIdle();
    auto& dev = *device;
    for (auto f : in_flight_fences)
      if (f) dev->destroyFence(f);
    in_flight_fences.clear();
    // image_in_flight holds non-owning aliases of in_flight_fences.
    image_in_flight.clear();
    for (auto s : available_semaphores)
      if (s) dev->destroySemaphore(s);
    available_semaphores.clear();
    for (auto s : finished_semaphore)
      if (s) dev->destroySemaphore(s);
    finished_semaphore.clear();
    for (auto fb : framebuffers)
      if (fb) dev->destroyFramebuffer(fb, device->allocation_callbacks);
    framebuffers.clear();
    command_buffers.clear();
    if (command_pool) {
      dev->destroyCommandPool(command_pool, device->allocation_callbacks);
      command_pool = vk::CommandPool{};
    }
    has_acquired_image = false;
  }

  void create_swapchain() {
    vkb::SwapchainBuilder swapchain_builder{*device};
    auto swap_ret = swapchain_builder.set_old_swapchain(*swapchain).build();
    swapchain->destroy();
    *swapchain = swap_ret;
  }

  void recreate_swapchain() {
    // Rare event; full idle is fine here and also covers outstanding
    // presentKHR waits on the semaphores destroyed below.
    (void)(*device)->waitIdle();
    (*device)->destroyCommandPool(command_pool, device->allocation_callbacks);
    for (auto framebuffer : framebuffers) {
      (*device)->destroyFramebuffer(framebuffer, device->allocation_callbacks);
    }

    swapchain->destroy_imageviews();
    create_swapchain();
    framebuffers = swapchain->createFramebuffers(this->render_pass, depth_attachment);
    command_pool = device->createCommandPool();
    frames_in_flight = std::min(2u, std::max(1u, swapchain->image_count));
    if (swapchain->current_frame >= frames_in_flight)
      swapchain->current_frame = 0;
    command_buffers = device->createCommandBuffers(
                         command_pool, frames_in_flight);
    // Sync objects follow frames_in_flight, not swapchain image_count.
    if (in_flight_fences.size() != frames_in_flight) {
      for (auto f : in_flight_fences)
        if (f) (*device)->destroyFence(f);
      in_flight_fences = device->createFences(frames_in_flight);
    }
    if (available_semaphores.size() != frames_in_flight) {
      for (auto s : available_semaphores)
        if (s) (*device)->destroySemaphore(s);
      available_semaphores = device->createSemaphores(frames_in_flight);
    }
    if (finished_semaphore.size() != swapchain->image_count) {
      for (auto s : finished_semaphore)
        if (s) (*device)->destroySemaphore(s);
      finished_semaphore = device->createSemaphores(swapchain->image_count);
    }
    // Old aliases point at fences of retired swapchain images.
    image_in_flight.assign(swapchain->image_count, vk::Fence{});
    has_acquired_image = false;
  }

  void drawFrame() {
    auto dev = *device;
    if (!has_acquired_image)
      acquireForFrame();
    // Acquire failed with out-of-date (e.g. Android rotation/resume). Skip the
    // frame; the owner recreates the swapchain at a safe point.
    if (!has_acquired_image)
      return;

    if (getImageInFlight(acquired_image_index)) {
      (void)dev->waitForFences(1, &getImageInFlight(acquired_image_index), VK_TRUE, UINT64_MAX);
    }
    // This slot's fence may still be aliased on a previous image index from
    // an earlier use of the slot. Clear stale aliases before rebinding so a
    // later acquire of that old image does not wait on the wrong submission.
    const vk::Fence slotFence = getInFlightFence();
    for (auto &f : image_in_flight)
      if (f == slotFence) f = vk::Fence{};
    getImageInFlight(acquired_image_index) = slotFence;

    vk::Semaphore          wait_semaphores[] = { getAvailableSemaphore() };
    vk::PipelineStageFlags wait_stages[]     = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores    = wait_semaphores;
    submitInfo.pWaitDstStageMask  = wait_stages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &getCurrentCommandBuffer();

    vk::Semaphore signal_semaphores[] = { getFinishedSemaphore() };
    submitInfo.signalSemaphoreCount   = 1;
    submitInfo.pSignalSemaphores      = signal_semaphores;

    (void)dev->resetFences(1, &getInFlightFence());
    (void)graphics_queue.submit(1, &submitInfo, getInFlightFence());

    last_presented_image_index = acquired_image_index;
    // Wait only on this frame's fence — never a device-wide waitIdle, which
    // would also drain unrelated queues/frames and serialize CPU and GPU.
    // Needed in synchronous mode (legacy resource-lifetime contract) and
    // whenever a capture hook must observe the completed frame.
    if (synchronous_frames || after_render_before_present)
      (void)dev->waitForFences(1, &getInFlightFence(), VK_TRUE, UINT64_MAX);
    if (after_render_before_present)
      after_render_before_present(last_presented_image_index);

    vk::PresentInfoKHR present_info = {};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = signal_semaphores;

    vk::SwapchainKHR swapChains[] = {swapchain->instance};
    present_info.swapchainCount   = 1;
    present_info.pSwapchains      = swapChains;
    present_info.pImageIndices    = &acquired_image_index;

    vk::Result result = present_queue.presentKHR(&present_info);
    has_acquired_image = false;
    if (result == vk::Result::eErrorOutOfDateKHR
         || result == vk::Result::eSuboptimalKHR) {
      // Defer to the owner (see acquireForFrame) rather than rebuilding inline.
      needs_recreate = true;
    } else if (result != vk::Result::eSuccess) {
      throw std::runtime_error("failed to present swapchain image");
    }
    if (frames_in_flight > 0)
      swapchain->current_frame = (swapchain->current_frame + 1) % frames_in_flight;
  }
};


class PresentBuilder {
public:
  PresentBuilder(Device& device, Swapchain& swapchain) 
    : device(device), swapchain(swapchain) {}
  virtual ~PresentBuilder() {}

  Present build(const BuiltRenderPass &render_pass, vk::ImageView depth_view = {}) {
    if (render_pass.colorAttachmentCount != 1)
      throw std::runtime_error("Present render pass must have exactly 1 color attachment");
    if (render_pass.colorFinalLayout != vk::ImageLayout::ePresentSrcKHR)
      throw std::runtime_error("Present render pass finalLayout must be PresentSrcKHR");
    if (render_pass.hasDepth && !depth_view)
      throw std::runtime_error("Present render pass has depth but no depth view was provided");
    if (!render_pass.hasDepth && depth_view)
      throw std::runtime_error("Present depth view provided but render pass has no depth attachment");
    return buildHandle(render_pass.handle, depth_view);
  }

private:
  Present buildHandle(vk::RenderPass render_pass, vk::ImageView depth_view) {
    Present cb{device, swapchain};
    // Keep the frame-slot count small (2): it bounds CPU/GPU overlap and the
    // number of command buffers / fences / acquire semaphores. Present-wait
    // semaphores are sized by image_count and indexed by the acquired image,
    // so they are never reused while the WSI engine still holds them.
    cb.frames_in_flight = std::min(2u, std::max(1u, swapchain.image_count));
    if (swapchain.current_frame >= cb.frames_in_flight)
      swapchain.current_frame = 0;
    cb.command_pool = device.createCommandPool();
    cb.command_buffers = device.createCommandBuffers(
                         cb.command_pool, cb.frames_in_flight);
    cb.depth_attachment = depth_view;
    cb.framebuffers = swapchain.createFramebuffers(render_pass, depth_view);

    cb.in_flight_fences = device.createFences(cb.frames_in_flight);
    // Non-owning aliases of in_flight_fences, keyed by swapchain image index;
    // null until an image is first used (creating real fences here would leak
    // them on first overwrite and adds pointless first-frame waits).
    cb.image_in_flight.assign(swapchain.image_count, vk::Fence{});
    cb.available_semaphores = device.createSemaphores(cb.frames_in_flight);
    // Present-wait semaphores follow the swapchain image count (see
    // getFinishedSemaphore): reuse is keyed on re-acquiring the image.
    cb.finished_semaphore = device.createSemaphores(swapchain.image_count);

    cb.graphics_queue = device.getQueue(QueueType::graphics);
    cb.present_queue = device.getQueue(QueueType::present);
    return cb;
  }

protected:
  Device& device;
  Swapchain& swapchain;
};

struct RecordingCmd {
  Present *present = nullptr;
  FrameSlot slot_{};

  explicit operator bool() const { return present && present->has_acquired_image; }
  FrameSlot slot() const { return slot_; }
  vk::CommandBuffer &commandBuffer() { return present->getCurrentCommandBuffer(); }

  InRenderPass beginRenderPass(const BuiltRenderPass &rp, const vk::ClearValue *clears, uint32_t count);
  InRenderPass beginRenderPass(
      const BuiltRenderPass &rp,
      vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.f, 0.f, 0.f, 1.f}));
  RecordedCmd end();
  void bindGraphics(vk::PipelineLayout layout, const BoundSet &set, uint32_t firstSet = 0);
};

struct InRenderPass {
  Present *present = nullptr;
  FrameSlot slot_{};

  explicit operator bool() const { return present != nullptr; }
  FrameSlot slot() const { return slot_; }
  vk::CommandBuffer &commandBuffer() { return present->getCurrentCommandBuffer(); }
  RecordingCmd endRenderPass();
  void bindGraphics(vk::PipelineLayout layout, const BoundSet &set, uint32_t firstSet = 0);
};

struct RecordedCmd {
  Present *present = nullptr;
  FrameSlot slot_{};
  void submitAndPresent() {
    if (present) present->drawFrame();
  }
};

struct AcquiredFrame {
  Present *present = nullptr;
  FrameSlot slot_{};
  uint32_t imageIndex = 0;
  explicit operator bool() const { return present && present->has_acquired_image; }
  FrameSlot slot() const { return slot_; }
  RecordingCmd begin();
};

inline AcquiredFrame Present::acquire() {
  acquireForFrame();
  if (!has_acquired_image) return {};
  return AcquiredFrame{this, currentSlot(), acquired_image_index};
}

inline RecordingCmd Present::begin() {
  acquireForFrame();
  if (!has_acquired_image) return {};
  vk::CommandBufferBeginInfo begin_info;
  auto buffer = getCurrentCommandBuffer();
  buffer.reset();
  buffer.begin(begin_info);
  return RecordingCmd{this, currentSlot()};
}

inline RecordedCmd Present::end() {
  getCurrentCommandBuffer().end();
  return RecordedCmd{this, currentSlot()};
}

inline InRenderPass Present::beginRenderPass(vk::RenderPass rp, const vk::ClearValue *clearValues,
                                             uint32_t clearCount) {
  this->render_pass = rp;
  vk::RenderPassBeginInfo render_pass_info{};
  render_pass_info.renderPass = rp;
  render_pass_info.framebuffer = getCurrentFrameBuffer();
  render_pass_info.renderArea.offset = vk::Offset2D{0, 0};
  render_pass_info.renderArea.extent = swapchain->extent;
  render_pass_info.clearValueCount = clearCount;
  render_pass_info.pClearValues = clearValues;
  getCurrentCommandBuffer().beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
  return InRenderPass{this, currentSlot()};
}

inline InRenderPass Present::beginRenderPass(vk::RenderPass rp, vk::ClearValue clearColor) {
  return beginRenderPass(rp, &clearColor, 1);
}

inline RecordingCmd Present::endRenderPass() {
  getCurrentCommandBuffer().endRenderPass();
  return RecordingCmd{this, currentSlot()};
}

inline InRenderPass RecordingCmd::beginRenderPass(const BuiltRenderPass &rp, const vk::ClearValue *clears,
                                                  uint32_t count) {
  return present->beginRenderPass(rp.handle, clears, count);
}
inline InRenderPass RecordingCmd::beginRenderPass(const BuiltRenderPass &rp, vk::ClearValue clearColor) {
  return present->beginRenderPass(rp.handle, clearColor);
}
inline RecordedCmd RecordingCmd::end() { return present->end(); }

inline RecordingCmd InRenderPass::endRenderPass() { return present->endRenderPass(); }

inline RecordingCmd AcquiredFrame::begin() { return present->begin(); }


#pragma endregion 


#pragma region Buffer


inline static /// Execute commands immediately and wait for this submission to finish.
void executeImmediately(vk::Device device, vk::CommandPool commandPool, vk::Queue queue, const std::function<void (vk::CommandBuffer cb)> &func) {
  vk::CommandBufferAllocateInfo cbai{ commandPool, vk::CommandBufferLevel::ePrimary, 1 };

  auto cbs = device.allocateCommandBuffers(cbai);
  cbs[0].begin(vk::CommandBufferBeginInfo{});
  func(cbs[0]);
  cbs[0].end();

  vk::SubmitInfo submit;
  submit.commandBufferCount = (uint32_t)cbs.size();
  submit.pCommandBuffers = cbs.data();
  // Wait on a fence scoped to this submission only. A device-wide waitIdle
  // here would also drain in-flight frames, serializing every caller (texture
  // uploads, shadow/G-buffer/offscreen passes) against the whole GPU.
  vk::Fence fence = device.createFence(vk::FenceCreateInfo{});
  queue.submit(submit, fence);
  (void)device.waitForFences(1, &fence, VK_TRUE, UINT64_MAX);
  device.destroyFence(fence);

  device.freeCommandBuffers(commandPool, cbs);
}


/// Scale a value by mip level, but do not reduce to zero.
inline uint32_t mipScale(uint32_t value, uint32_t mipLevel) {
  return std::max(value >> mipLevel, (uint32_t)1);
}

/// Description of blocks for compressed formats.
struct BlockParams {
  uint8_t blockWidth;
  uint8_t blockHeight;
  uint8_t bytesPerBlock;
};
/// Get the details of vulkan texture formats.
inline BlockParams getBlockParams(vk::Format format) {
  switch (format) {
    case vk::Format::eR4G4UnormPack8: return BlockParams{1, 1, 1};
    case vk::Format::eR4G4B4A4UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eB4G4R4A4UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eR5G6B5UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eB5G6R5UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eR5G5B5A1UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eB5G5R5A1UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eA1R5G5B5UnormPack16: return BlockParams{1, 1, 2};
    case vk::Format::eR8Unorm: return BlockParams{1, 1, 1};
    case vk::Format::eR8Snorm: return BlockParams{1, 1, 1};
    case vk::Format::eR8Uscaled: return BlockParams{1, 1, 1};
    case vk::Format::eR8Sscaled: return BlockParams{1, 1, 1};
    case vk::Format::eR8Uint: return BlockParams{1, 1, 1};
    case vk::Format::eR8Sint: return BlockParams{1, 1, 1};
    case vk::Format::eR8Srgb: return BlockParams{1, 1, 1};
    case vk::Format::eR8G8Unorm: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Snorm: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Uscaled: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Sscaled: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Uint: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Sint: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8Srgb: return BlockParams{1, 1, 2};
    case vk::Format::eR8G8B8Unorm: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Snorm: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Uscaled: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Sscaled: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Uint: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Sint: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8Srgb: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Unorm: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Snorm: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Uscaled: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Sscaled: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Uint: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Sint: return BlockParams{1, 1, 3};
    case vk::Format::eB8G8R8Srgb: return BlockParams{1, 1, 3};
    case vk::Format::eR8G8B8A8Unorm: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Snorm: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Uscaled: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Sscaled: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Uint: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Sint: return BlockParams{1, 1, 4};
    case vk::Format::eR8G8B8A8Srgb: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Unorm: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Snorm: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Uscaled: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Sscaled: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Uint: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Sint: return BlockParams{1, 1, 4};
    case vk::Format::eB8G8R8A8Srgb: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8UnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8SnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8UscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8SscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8UintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8SintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA8B8G8R8SrgbPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10UnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10SnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10UscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10SscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10UintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2R10G10B10SintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10UnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10SnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10UscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10SscaledPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10UintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eA2B10G10R10SintPack32: return BlockParams{1, 1, 4};
    case vk::Format::eR16Unorm: return BlockParams{1, 1, 2};
    case vk::Format::eR16Snorm: return BlockParams{1, 1, 2};
    case vk::Format::eR16Uscaled: return BlockParams{1, 1, 2};
    case vk::Format::eR16Sscaled: return BlockParams{1, 1, 2};
    case vk::Format::eR16Uint: return BlockParams{1, 1, 2};
    case vk::Format::eR16Sint: return BlockParams{1, 1, 2};
    case vk::Format::eR16Sfloat: return BlockParams{1, 1, 2};
    case vk::Format::eR16G16Unorm: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Snorm: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Uscaled: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Sscaled: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Uint: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Sint: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16Sfloat: return BlockParams{1, 1, 4};
    case vk::Format::eR16G16B16Unorm: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Snorm: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Uscaled: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Sscaled: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Uint: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Sint: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16Sfloat: return BlockParams{1, 1, 6};
    case vk::Format::eR16G16B16A16Unorm: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Snorm: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Uscaled: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Sscaled: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Uint: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Sint: return BlockParams{1, 1, 8};
    case vk::Format::eR16G16B16A16Sfloat: return BlockParams{1, 1, 8};
    case vk::Format::eR32Uint: return BlockParams{1, 1, 4};
    case vk::Format::eR32Sint: return BlockParams{1, 1, 4};
    case vk::Format::eR32Sfloat: return BlockParams{1, 1, 4};
    case vk::Format::eR32G32Uint: return BlockParams{1, 1, 8};
    case vk::Format::eR32G32Sint: return BlockParams{1, 1, 8};
    case vk::Format::eR32G32Sfloat: return BlockParams{1, 1, 8};
    case vk::Format::eR32G32B32Uint: return BlockParams{1, 1, 12};
    case vk::Format::eR32G32B32Sint: return BlockParams{1, 1, 12};
    case vk::Format::eR32G32B32Sfloat: return BlockParams{1, 1, 12};
    case vk::Format::eR32G32B32A32Uint: return BlockParams{1, 1, 16};
    case vk::Format::eR32G32B32A32Sint: return BlockParams{1, 1, 16};
    case vk::Format::eR32G32B32A32Sfloat: return BlockParams{1, 1, 16};
    case vk::Format::eR64Uint: return BlockParams{1, 1, 8};
    case vk::Format::eR64Sint: return BlockParams{1, 1, 8};
    case vk::Format::eR64Sfloat: return BlockParams{1, 1, 8};
    case vk::Format::eR64G64Uint: return BlockParams{1, 1, 16};
    case vk::Format::eR64G64Sint: return BlockParams{1, 1, 16};
    case vk::Format::eR64G64Sfloat: return BlockParams{1, 1, 16};
    case vk::Format::eR64G64B64Uint: return BlockParams{1, 1, 24};
    case vk::Format::eR64G64B64Sint: return BlockParams{1, 1, 24};
    case vk::Format::eR64G64B64Sfloat: return BlockParams{1, 1, 24};
    case vk::Format::eR64G64B64A64Uint: return BlockParams{1, 1, 32};
    case vk::Format::eR64G64B64A64Sint: return BlockParams{1, 1, 32};
    case vk::Format::eR64G64B64A64Sfloat: return BlockParams{1, 1, 32};
    case vk::Format::eB10G11R11UfloatPack32: return BlockParams{1, 1, 4};
    case vk::Format::eE5B9G9R9UfloatPack32: return BlockParams{1, 1, 4};
    case vk::Format::eD16Unorm: return BlockParams{1, 1, 4};
    case vk::Format::eX8D24UnormPack32: return BlockParams{1, 1, 4};
    case vk::Format::eD32Sfloat: return BlockParams{1, 1, 4};
    case vk::Format::eS8Uint: return BlockParams{1, 1, 1};
    case vk::Format::eD16UnormS8Uint: return BlockParams{1, 1, 3};
    case vk::Format::eD24UnormS8Uint: return BlockParams{1, 1, 4};
    case vk::Format::eD32SfloatS8Uint: return BlockParams{0, 0, 0};
    case vk::Format::eBc1RgbUnormBlock: return BlockParams{4, 4, 8};
    case vk::Format::eBc1RgbSrgbBlock: return BlockParams{4, 4, 8};
    case vk::Format::eBc1RgbaUnormBlock: return BlockParams{4, 4, 8};
    case vk::Format::eBc1RgbaSrgbBlock: return BlockParams{4, 4, 8};
    case vk::Format::eBc2UnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc2SrgbBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc3UnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc3SrgbBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc4UnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc4SnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc5UnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc5SnormBlock: return BlockParams{4, 4, 16};
    case vk::Format::eBc6HUfloatBlock: return BlockParams{0, 0, 0};
    case vk::Format::eBc6HSfloatBlock: return BlockParams{0, 0, 0};
    case vk::Format::eBc7UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eBc7SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8A1UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8A1SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8A8UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEtc2R8G8B8A8SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEacR11UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEacR11SnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEacR11G11UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eEacR11G11SnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc4x4UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc4x4SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc5x4UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc5x4SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc5x5UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc5x5SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc6x5UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc6x5SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc6x6UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc6x6SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x5UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x5SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x6UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x6SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x8UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc8x8SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x5UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x5SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x6UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x6SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x8UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x8SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x10UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc10x10SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc12x10UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc12x10SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc12x12UnormBlock: return BlockParams{0, 0, 0};
    case vk::Format::eAstc12x12SrgbBlock: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc12BppUnormBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc14BppUnormBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc22BppUnormBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc24BppUnormBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc12BppSrgbBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc14BppSrgbBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc22BppSrgbBlockIMG: return BlockParams{0, 0, 0};
    case vk::Format::ePvrtc24BppSrgbBlockIMG: return BlockParams{0, 0, 0};
  }
  return BlockParams{0, 0, 0};
}


/// A generic buffer that may be used as a vertex buffer, uniform buffer or other kinds of memory resident data.
/// Buffers require memory objects which represent GPU and CPU resources.
struct GenericBuffer {
  vk::Buffer buffer;
  vk::DeviceMemory memory;
  vk::DeviceSize size = 0;
  vkb::Device* device = nullptr;
  // Size the buffer was created with; allocate() reuses the allocation for
  // any request that fits and matches usage/memflags.
  vk::DeviceSize capacity = 0;
  vk::BufferUsageFlags usage_flags{};
  vk::MemoryPropertyFlags memory_flags{};
#if defined(VKB_ENABLE_VMA)
  VmaAllocation vma_allocation = VK_NULL_HANDLE;
#endif

  GenericBuffer() {}

  GenericBuffer(vkb::Device& device, vk::BufferUsageFlags usage, vk::DeviceSize size, vk::MemoryPropertyFlags memflags = vk::MemoryPropertyFlagBits::eDeviceLocal) 
  {
    allocate(FrameSlot::gpuIdle(), device, usage, size, memflags);
  }

  ~GenericBuffer() { release(); }
  GenericBuffer(const GenericBuffer &) = delete;
  GenericBuffer &operator=(const GenericBuffer &) = delete;
  GenericBuffer(GenericBuffer &&o) noexcept { steal(o); }
  GenericBuffer &operator=(GenericBuffer &&o) noexcept {
    if (this != &o) {
      release();
      steal(o);
    }
    return *this;
  }

  /// Relinquish handles without destroying them (caller takes ownership).
  void detach() {
    buffer = vk::Buffer{};
    memory = vk::DeviceMemory{};
    size = 0;
    capacity = 0;
    device = nullptr;
#if defined(VKB_ENABLE_VMA)
    vma_allocation = VK_NULL_HANDLE;
#endif
  }

  void allocate(FrameSlot slot, vkb::Device& device, vk::BufferUsageFlags usage, vk::DeviceSize size, vk::MemoryPropertyFlags memflags = vk::MemoryPropertyFlagBits::eDeviceLocal)
  {
    (void)slot;
    // Reuse the current allocation when it is compatible and large enough.
    // Keeps the vk::Buffer handle stable (descriptor sets stay valid) and
    // avoids vkCreateBuffer/vkAllocateMemory on repeated per-frame calls.
    if (buffer && this->device == &device && usage_flags == usage &&
        memory_flags == memflags && size <= capacity) {
      this->size = size;
      return;
    }
    if (buffer) release();

    this->size = size;
    this->device = &device;
    usage_flags = usage;
    memory_flags = memflags;
    // Create the buffer object without memory.
    vk::BufferCreateInfo ci{};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = vk::SharingMode::eExclusive;
#if defined(VKB_ENABLE_VMA)
    if (device.hasVmaAllocator()) {
      VmaAllocationCreateInfo allocationInfo{};
      allocationInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(memflags);
      VkBuffer rawBuffer = VK_NULL_HANDLE;
      VmaAllocationInfo allocatedInfo{};
      const VkResult result = vmaCreateBuffer(
          device.vma_allocator,
          reinterpret_cast<const VkBufferCreateInfo *>(&ci), &allocationInfo,
          &rawBuffer, &vma_allocation, &allocatedInfo);
      if (result != VK_SUCCESS)
        throw std::runtime_error("VMA buffer allocation failed");
      buffer = rawBuffer;
      memory = allocatedInfo.deviceMemory;
      capacity = size;
      return;
    }
#endif
    buffer = device->createBuffer(ci, device.allocation_callbacks);
    capacity = size;

    // Find out how much memory and which heap to allocate from.
    auto memreq = device->getBufferMemoryRequirements(buffer);

    // Create a memory object to bind to the buffer.
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memreq.size;
    allocInfo.memoryTypeIndex = device.physical_device.findMemoryTypeIndex(memreq.memoryTypeBits, memflags);
    memory = device->allocateMemory(allocInfo, device.allocation_callbacks);

    device->bindBufferMemory(buffer, memory, 0);
  }

  void steal(GenericBuffer &o) noexcept {
    buffer = o.buffer;
    memory = o.memory;
#if defined(VKB_ENABLE_VMA)
    vma_allocation = o.vma_allocation;
#endif
    size = o.size;
    device = o.device;
    capacity = o.capacity;
    usage_flags = o.usage_flags;
    memory_flags = o.memory_flags;
    o.detach();
  }

  void release() {
    if (!device) return;
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      vmaDestroyBuffer(device->vma_allocator, static_cast<VkBuffer>(buffer),
                       vma_allocation);
      detach();
      return;
    }
#endif
    if (buffer) (*device)->destroyBuffer(buffer, (*device).allocation_callbacks);
    if (memory) (*device)->freeMemory(memory, (*device).allocation_callbacks);
    detach();
  }

  inline static /// Utility function for finding memory types for uniforms and images.
  int findMemoryTypeIndex(const vk::PhysicalDeviceMemoryProperties &memprops, uint32_t memoryTypeBits, vk::MemoryPropertyFlags search) {
    for (int i = 0; i != memprops.memoryTypeCount; ++i, memoryTypeBits >>= 1) 
      if (memoryTypeBits & 1) 
        if ((memprops.memoryTypes[i].propertyFlags & search) == search) 
          return i;
    return -1;
  }

  /// For a purely device local buffer, copy memory to the buffer object immediately.
  /// Note that this will stall the pipeline!
  void upload(vk::CommandPool commandPool, vk::Queue queue, const void *value, vk::DeviceSize size) const {
    if (size == 0) return;
    using buf = vk::BufferUsageFlagBits;
    using pfb = vk::MemoryPropertyFlagBits;
    auto tmp = GenericBuffer(*device, buf::eTransferSrc, size, pfb::eHostVisible | pfb::eHostCoherent);
    tmp.updateLocal(FrameSlot::gpuIdle(), value, size);

    executeImmediately(device->instance, commandPool, queue, [&](vk::CommandBuffer cb) {
      vk::BufferCopy bc{0, 0, size};
      cb.copyBuffer(tmp.buffer, buffer, bc);
    });
    // executeImmediately is fence-synchronous, so the staging copy is done.
    tmp.release();
  }

  template<typename T>
  void upload(vk::CommandPool commandPool, vk::Queue queue, const std::vector<T> &value) const {
    upload(commandPool, queue, value.data(), value.size() * sizeof(T));
  }

  template<typename T>
  void upload(vk::CommandPool commandPool, vk::Queue queue, const T &value) const {
    upload(commandPool, queue, &value, sizeof(value));
  }

  void barrier(vk::CommandBuffer cb, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, vk::AccessFlags srcAccessMask, vk::AccessFlags dstAccessMask, uint32_t srcQueueFamilyIndex, uint32_t dstQueueFamilyIndex) const {
    vk::BufferMemoryBarrier bmb{srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex, buffer, 0, size};
    cb.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, nullptr, bmb, nullptr);
  }

  /// Host-visible write. `slot` must be this frame's FrameSlot (or gpuIdle after waitIdle).
  void updateLocal(FrameSlot slot, const void *value, vk::DeviceSize size) const {
    (void)slot;
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      void *ptr = nullptr;
      const VkResult result =
          vmaMapMemory(device->vma_allocator, vma_allocation, &ptr);
      if (result != VK_SUCCESS)
        throw std::runtime_error("VMA buffer map failed");
      memcpy(ptr, value, static_cast<size_t>(size));
      vmaUnmapMemory(device->vma_allocator, vma_allocation);
      return;
    }
#endif
    void *ptr = (*device)->mapMemory(memory, 0, size, vk::MemoryMapFlags{});
    memcpy(ptr, value, (size_t)size);
    (*device)->unmapMemory(memory);
  }

  template<class Type, class Allocator>
  void updateLocal(FrameSlot slot, const std::vector<Type, Allocator> &value) const {
    updateLocal(slot, (void*)value.data(), vk::DeviceSize(value.size() * sizeof(Type)));
  }

  template<class Type>
  void updateLocal(FrameSlot slot, const Type &value) const {
    updateLocal(slot, (void*)&value, vk::DeviceSize(sizeof(Type)));
  }

  void *map() const {
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      void *ptr = nullptr;
      const VkResult result =
          vmaMapMemory(device->vma_allocator, vma_allocation, &ptr);
      if (result != VK_SUCCESS)
        throw std::runtime_error("VMA buffer map failed");
      return ptr;
    }
#endif
    return (*device)->mapMemory(memory, 0, size, vk::MemoryMapFlags{});
  };
  void unmap() const {
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      vmaUnmapMemory(device->vma_allocator, vma_allocation);
      return;
    }
#endif
    (*device)->unmapMemory(memory);
  };

  void flush() const {
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      vmaFlushAllocation(device->vma_allocator, vma_allocation, 0,
                         VK_WHOLE_SIZE);
      return;
    }
#endif
    vk::MappedMemoryRange mr{memory, 0, VK_WHOLE_SIZE};
    return (*device)->flushMappedMemoryRanges(mr);
  }

  void invalidate() const {
#if defined(VKB_ENABLE_VMA)
    if (vma_allocation) {
      vmaInvalidateAllocation(device->vma_allocator, vma_allocation, 0,
                              VK_WHOLE_SIZE);
      return;
    }
#endif
    vk::MappedMemoryRange mr{memory, 0, VK_WHOLE_SIZE};
    return (*device)->invalidateMappedMemoryRanges(mr);
  }

  operator vk::Buffer* () {
    return &buffer;
  }

  operator vk::Buffer& () {
    return buffer;
  }
};

/// This class is a specialisation of GenericBuffer for high performance vertex buffers on the GPU.
/// You must upload the contents before use.
struct VertexBuffer : public GenericBuffer {
  VertexBuffer() {}
  VertexBuffer(vkb::Device& device, size_t size) 
    : GenericBuffer(device, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
      size, vk::MemoryPropertyFlagBits::eDeviceLocal) {}

  void allocate(vkb::Device& device, size_t size) {
    GenericBuffer::allocate(FrameSlot::gpuIdle(), device, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, size, vk::MemoryPropertyFlagBits::eDeviceLocal);
  }
};

/// This class is a specialisation of GenericBuffer for low performance vertex buffers on the host.
struct HostVertexBuffer : public GenericBuffer {
  HostVertexBuffer() {}
  template<class Type, class Allocator = std::allocator<Type> >
  HostVertexBuffer(vkb::Device& device, const std::vector<Type, Allocator> &value) 
    : GenericBuffer(device, vk::BufferUsageFlagBits::eVertexBuffer, value.size() * sizeof(Type),
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent) {
    updateLocal(FrameSlot::gpuIdle(), value);
  }

  template<class Type, class Allocator = std::allocator<Type> >
  void allocate(FrameSlot slot, vkb::Device& device, const std::vector<Type, Allocator> &value) {
    GenericBuffer::allocate(slot, device, vk::BufferUsageFlagBits::eVertexBuffer, value.size() * sizeof(Type),
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
    updateLocal(slot, value);
  }
};

/// This class is a specialisation of GenericBuffer for high performance index buffers.
/// You must upload the contents before use.
struct IndexBuffer : public GenericBuffer {
  IndexBuffer() {}
  IndexBuffer(vkb::Device& device, vk::DeviceSize size) 
    : GenericBuffer(device, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
                    size, vk::MemoryPropertyFlagBits::eDeviceLocal) {}

  void allocate(vkb::Device& device, vk::DeviceSize size) {
    GenericBuffer::allocate(FrameSlot::gpuIdle(), device, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, 
                    size, vk::MemoryPropertyFlagBits::eDeviceLocal);
  }
};

/// This class is a specialisation of GenericBuffer for low performance vertex buffers in CPU memory.
struct HostIndexBuffer : public GenericBuffer {
  HostIndexBuffer() {}
  template<class Type, class Allocator = std::allocator<Type> >
  HostIndexBuffer(vkb::Device& device, const std::vector<Type, Allocator> &value) 
    : GenericBuffer(device, vk::BufferUsageFlagBits::eIndexBuffer, value.size() * sizeof(Type),
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent) {
    updateLocal(FrameSlot::gpuIdle(), value);
  }

  template<class Type, class Allocator = std::allocator<Type> >
  void allocate(vkb::Device& device, const std::vector<Type, Allocator> &value) {
    GenericBuffer::allocate(FrameSlot::gpuIdle(), device, vk::BufferUsageFlagBits::eIndexBuffer, value.size() * sizeof(Type),
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
    updateLocal(FrameSlot::gpuIdle(), value);
  }
};

/// This class is a specialisation of GenericBuffer for uniform buffers.
struct UniformBuffer : public GenericBuffer {
  UniformBuffer() {}
  /// Device local uniform buffer.
  UniformBuffer(vkb::Device& device, size_t size) 
    : GenericBuffer(device, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, 
                    (vk::DeviceSize)size, vk::MemoryPropertyFlagBits::eDeviceLocal) {}

  void allocate(vkb::Device& device, size_t size) {
    GenericBuffer::allocate(FrameSlot::gpuIdle(), device, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, 
                    (vk::DeviceSize)size, vk::MemoryPropertyFlagBits::eDeviceLocal);
  }
};

#pragma endregion


#pragma region Image

struct SampledImage {
  vk::Sampler sampler{};
  vk::ImageView view{};
  vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;

  SampledImage() = default;
  SampledImage(vk::Sampler s, vk::ImageView v,
               vk::ImageLayout l = vk::ImageLayout::eShaderReadOnlyOptimal)
      : sampler(s), view(v), layout(l) {}

  /// Descriptor write before the first pass (layout will be shader-read when sampled).
  static SampledImage forLaterSample(vk::Sampler s, vk::ImageView v) {
    return SampledImage(s, v, vk::ImageLayout::eShaderReadOnlyOptimal);
  }
};

/// Nearest-only sampler for depth images. Linear filtering of raw D32 is not valid PCF.
struct DepthSampler {
  vk::Sampler handle{};
  operator vk::Sampler() const { return handle; }
  explicit operator bool() const { return static_cast<bool>(handle); }
};

/// Generic image with a view and memory object.
/// Vulkan images need a memory object to hold the data and a view object for the GPU to access the data.
class GenericImage {
public:
  GenericImage() {}

  GenericImage(vkb::Device& device, const vk::ImageCreateInfo &info, vk::ImageViewType viewType, vk::ImageAspectFlags aspectMask, bool makeHostImage) {
    create(device, info, viewType, aspectMask, makeHostImage);
  }

  vk::Image image() const { return *s.image; }
  vk::ImageView imageView() const { return *s.imageView; }
  vk::DeviceMemory mem() const { return *s.mem; }

  /// Clear the colour of an image.
  void clear(vk::CommandBuffer cb, const std::array<float,4> colour = {1, 1, 1, 1}) {
    setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
    vk::ClearColorValue ccv(colour);
    vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    cb.clearColorImage(*s.image, vk::ImageLayout::eTransferDstOptimal, ccv, range);
  }

  /// Update the image with an array of pixels. (Currently 2D only)
  void update(const void *data, vk::DeviceSize bytesPerPixel) {
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t mipLevel = 0; mipLevel != info().mipLevels; ++mipLevel) {
      // Array images are layed out horizontally. eg. [left][front][right] etc.
      for (uint32_t arrayLayer = 0; arrayLayer != info().arrayLayers; ++arrayLayer) {
        vk::ImageSubresource subresource{vk::ImageAspectFlagBits::eColor, mipLevel, arrayLayer};
        auto srlayout = (*device)->getImageSubresourceLayout(*s.image, subresource);
        uint8_t *dest = (uint8_t *)(*device)->mapMemory(*s.mem, 0, s.size, vk::MemoryMapFlags{}) + srlayout.offset;
        size_t bytesPerLine = s.info.extent.width * bytesPerPixel;
        size_t srcStride = bytesPerLine * info().arrayLayers;
        for (int y = 0; y != s.info.extent.height; ++y) {
          memcpy(dest, src + arrayLayer * bytesPerLine, bytesPerLine);
          src += srcStride;
          dest += srlayout.rowPitch;
        }
      }
    }
    (*device)->unmapMemory(*s.mem);
  }

  /// Copy another image to this one. This also changes the layout.
  void copy(vk::CommandBuffer cb, GenericImage &srcImage) {
    srcImage.setLayout(cb, vk::ImageLayout::eTransferSrcOptimal);
    setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
    for (uint32_t mipLevel = 0; mipLevel != info().mipLevels; ++mipLevel) {
      vk::ImageCopy region{};
      region.srcSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1};
      region.dstSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, 0, 1};
      region.extent = s.info.extent;
      cb.copyImage(srcImage.image(), vk::ImageLayout::eTransferSrcOptimal, *s.image, vk::ImageLayout::eTransferDstOptimal, region);
    }
  }

  /// Copy a subimage in a buffer to this image.
  void copy(vk::CommandBuffer cb, vk::Buffer buffer, uint32_t mipLevel, uint32_t arrayLayer, uint32_t width, uint32_t height, uint32_t depth, uint32_t offset) {
    setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
    vk::BufferImageCopy region{};
    region.bufferOffset = offset;
    vk::Extent3D extent;
    extent.width = width;
    extent.height = height;
    extent.depth = depth;
    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, mipLevel, arrayLayer, 1};
    region.imageExtent = extent;
    cb.copyBufferToImage(buffer, *s.image, vk::ImageLayout::eTransferDstOptimal, region);
  }

  void upload(vk::CommandPool commandPool, vk::Queue queue, const std::vector<uint8_t> &bytes) {
    upload(commandPool, queue, bytes.data(), bytes.size());
  }

  void upload(vk::CommandPool commandPool, vk::Queue queue, const void *data, vk::DeviceSize sizeInBytes) {
    GenericBuffer stagingBuffer(*device, (vk::BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc, sizeInBytes,
                                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    stagingBuffer.updateLocal(FrameSlot::gpuIdle(), data, sizeInBytes);

    // Copy the staging buffer to the GPU texture and set the layout.
    executeImmediately(device->instance, commandPool, queue, [&](vk::CommandBuffer cb) {
      auto bp = getBlockParams(s.info.format);
      vk::Buffer buf = stagingBuffer.buffer;
      uint32_t offset = 0;
      for (uint32_t mipLevel = 0; mipLevel != s.info.mipLevels; ++mipLevel) {
        auto width = mipScale(s.info.extent.width, mipLevel);
        auto height = mipScale(s.info.extent.height, mipLevel);
        auto depth = mipScale(s.info.extent.depth, mipLevel);
        for (uint32_t face = 0; face != s.info.arrayLayers; ++face) {
          copy(cb, buf, mipLevel, face, width, height, depth, offset);
          offset += ((bp.bytesPerBlock + 3) & ~3) * (width * height);
        }
      }
      setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal);
    });
  }

  /// Change the layout of this image using a memory barrier.
  void setLayout(vk::CommandBuffer cb, vk::ImageLayout newLayout, vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor) {
    if (newLayout == s.currentLayout) return;
    vk::ImageLayout oldLayout = s.currentLayout;
    s.currentLayout = newLayout;

    vk::ImageMemoryBarrier imageMemoryBarriers = {};
    imageMemoryBarriers.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarriers.oldLayout = oldLayout;
    imageMemoryBarriers.newLayout = newLayout;
    imageMemoryBarriers.image = *s.image;
    imageMemoryBarriers.subresourceRange = {aspectMask, 0, s.info.mipLevels, 0, s.info.arrayLayers};

    // Put barrier on top
    // https://www.khronos.org/registry/vulkan/specs/1.2/html/chap7.html#synchronization-access-types-supported
    vk::PipelineStageFlags srcStageMask{vk::PipelineStageFlagBits::eTopOfPipe};
    vk::PipelineStageFlags dstStageMask{vk::PipelineStageFlagBits::eTopOfPipe};
    vk::DependencyFlags dependencyFlags{};
    vk::AccessFlags srcMask{};
    vk::AccessFlags dstMask{};

    typedef vk::ImageLayout il;
    typedef vk::AccessFlagBits afb;

    // Is it me, or are these the same?
    switch (oldLayout) {
      case il::eUndefined: break;
      case il::eGeneral: srcMask = afb::eTransferWrite; srcStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::eColorAttachmentOptimal: srcMask = afb::eColorAttachmentWrite; srcStageMask=vk::PipelineStageFlagBits::eColorAttachmentOutput; break;
      case il::eDepthStencilAttachmentOptimal: srcMask = afb::eDepthStencilAttachmentWrite; srcStageMask=vk::PipelineStageFlagBits::eEarlyFragmentTests; break;
      case il::eDepthStencilReadOnlyOptimal: srcMask = afb::eDepthStencilAttachmentRead; srcStageMask=vk::PipelineStageFlagBits::eEarlyFragmentTests; break;
      case il::eShaderReadOnlyOptimal: srcMask = afb::eShaderRead; srcStageMask=vk::PipelineStageFlagBits::eVertexShader; break;
      case il::eTransferSrcOptimal: srcMask = afb::eTransferRead; srcStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::eTransferDstOptimal: srcMask = afb::eTransferWrite; srcStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::ePreinitialized: srcMask = afb::eTransferWrite|afb::eHostWrite; srcStageMask=vk::PipelineStageFlagBits::eTransfer|vk::PipelineStageFlagBits::eHost; break;
      case il::ePresentSrcKHR: srcMask = afb::eMemoryRead; break;
    }

    switch (newLayout) {
      case il::eUndefined: break;
      case il::eGeneral: dstMask = afb::eTransferWrite; dstStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::eColorAttachmentOptimal: dstMask = afb::eColorAttachmentWrite; dstStageMask=vk::PipelineStageFlagBits::eColorAttachmentOutput; break;
      case il::eDepthStencilAttachmentOptimal: dstMask = afb::eDepthStencilAttachmentWrite; dstStageMask=vk::PipelineStageFlagBits::eEarlyFragmentTests; break;
      case il::eDepthStencilReadOnlyOptimal: dstMask = afb::eDepthStencilAttachmentRead; dstStageMask=vk::PipelineStageFlagBits::eEarlyFragmentTests; break;
      case il::eShaderReadOnlyOptimal: dstMask = afb::eShaderRead; dstStageMask=vk::PipelineStageFlagBits::eVertexShader; break;
      case il::eTransferSrcOptimal: dstMask = afb::eTransferRead; dstStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::eTransferDstOptimal: dstMask = afb::eTransferWrite; dstStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::ePreinitialized: dstMask = afb::eTransferWrite; dstStageMask=vk::PipelineStageFlagBits::eTransfer; break;
      case il::ePresentSrcKHR: dstMask = afb::eMemoryRead; break;
    }
//printf("%08x %08x\n", (VkFlags)srcMask, (VkFlags)dstMask);

    imageMemoryBarriers.srcAccessMask = srcMask;
    imageMemoryBarriers.dstAccessMask = dstMask;
    auto memoryBarriers = nullptr;
    auto bufferMemoryBarriers = nullptr;
    cb.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers);
  }

  /// Set what the image thinks is its current layout (ie. the old layout in an image barrier).
  void setCurrentLayout(vk::ImageLayout oldLayout) {
    s.currentLayout = oldLayout;
  }

  void beginColorAttachment() { s.currentLayout = vk::ImageLayout::eColorAttachmentOptimal; }
  void beginDepthAttachment() { s.currentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal; }
  void endSampledLayout() { s.currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal; }

  vk::Format format() const { return s.info.format; }
  vk::Extent3D extent() const { return s.info.extent; }
  const vk::ImageCreateInfo &info() const { return s.info; }
  vk::ImageLayout currentLayout() const { return s.currentLayout; }

  AttachmentView asAttachment() const {
    const bool depth = bool(s.aspectMask & vk::ImageAspectFlagBits::eDepth);
    return AttachmentView{
        imageView(), s.aspectMask,
        depth ? vk::ImageLayout::eDepthStencilAttachmentOptimal
              : vk::ImageLayout::eColorAttachmentOptimal};
  }

  /// Only valid when the GPU layout is already shader-readable (not a current color/depth attachment).
  SampledImage sampled(vk::Sampler sampler = {}) const {
    const vk::ImageLayout layout = s.currentLayout;
    if (layout == vk::ImageLayout::eColorAttachmentOptimal ||
        layout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
      throw std::runtime_error("cannot sample an image that is a current render-pass attachment");
    if (layout != vk::ImageLayout::eShaderReadOnlyOptimal &&
        layout != vk::ImageLayout::eDepthStencilReadOnlyOptimal)
      throw std::runtime_error("image is not in a sampled layout");
    return SampledImage(sampler, imageView(), layout);
  }

protected:
  void create(vkb::Device& device, const vk::ImageCreateInfo &info, vk::ImageViewType viewType, vk::ImageAspectFlags aspectMask, bool hostImage) {
    this->device = &device;
    s.currentLayout = info.initialLayout;
    s.info = info;
    s.aspectMask = aspectMask;
    s.image = device->createImageUnique(info);

    // Find out how much memory and which heap to allocate from.
    auto memreq = device->getImageMemoryRequirements(*s.image);
    vk::MemoryPropertyFlags search{};
    if (hostImage) search = vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible;

    // Create a memory object to bind to the buffer.
    // Note: we don't expect to be able to map the buffer.
    vk::MemoryAllocateInfo mai{};
    mai.allocationSize = s.size = memreq.size;
    mai.memoryTypeIndex = device.physical_device.findMemoryTypeIndex(memreq.memoryTypeBits, search);
    s.mem = device->allocateMemoryUnique(mai);

    device->bindImageMemory(*s.image, *s.mem, 0);

    if (!hostImage) {
      vk::ImageViewCreateInfo viewInfo{};
      viewInfo.image = *s.image;
      viewInfo.viewType = viewType;
      viewInfo.format = info.format;
      viewInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
      viewInfo.subresourceRange = vk::ImageSubresourceRange{aspectMask, 0, info.mipLevels, 0, info.arrayLayers};
      s.imageView = device->createImageViewUnique(viewInfo);
    }
  }

  struct State {
    vk::UniqueImage image;
    vk::UniqueImageView imageView;
    vk::UniqueDeviceMemory mem;
    vk::DeviceSize size;
    vk::ImageLayout currentLayout;
    vk::ImageCreateInfo info;
    vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
  };

  State s;
  vkb::Device* device;  
};


/// A 2D texture image living on the GPU or a staging buffer visible to the CPU.
class TextureImage2D : public GenericImage {
public:
  TextureImage2D() {}

  TextureImage2D(Device& device, uint32_t width, uint32_t height, uint32_t mipLevels=1, vk::Format format = vk::Format::eR8G8B8A8Unorm, bool hostImage = false) {
    vk::ImageCreateInfo info;
    info.flags = {};
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = hostImage ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices = nullptr;
    info.initialLayout = hostImage ? vk::ImageLayout::ePreinitialized : vk::ImageLayout::eUndefined;
    create(device, info, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, hostImage);
  }
private:
};

/// A cube map texture image living on the GPU or a staging buffer visible to the CPU.
class TextureImageCube : public GenericImage {
public:
  TextureImageCube() {}

  TextureImageCube(Device& device, const vk::PhysicalDeviceMemoryProperties &memprops, uint32_t width, uint32_t height, uint32_t mipLevels=1, vk::Format format = vk::Format::eR8G8B8A8Unorm, bool hostImage = false) {
    vk::ImageCreateInfo info;
    info.flags = {vk::ImageCreateFlagBits::eCubeCompatible};
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = mipLevels;
    info.arrayLayers = 6;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = hostImage ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices = nullptr;
    //info.initialLayout = hostImage ? vk::ImageLayout::ePreinitialized : vk::ImageLayout::eUndefined;
    info.initialLayout = vk::ImageLayout::ePreinitialized;
    create(device, info, vk::ImageViewType::eCube, vk::ImageAspectFlagBits::eColor, hostImage);
  }
};

/// An image to use as a depth buffer on a renderpass.
class DepthStencilImage : public GenericImage {
public:
  DepthStencilImage() {}

  DepthStencilImage(Device& device, uint32_t width, uint32_t height, vk::Format format = vk::Format::eD32Sfloat) {
    vk::ImageCreateInfo info;
    info.flags = {};

    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices = nullptr;
    info.initialLayout = vk::ImageLayout::eUndefined;
    typedef vk::ImageAspectFlagBits iafb;
    create(device, info, vk::ImageViewType::e2D, iafb::eDepth, false);
  }
};

/// An image to use as a colour buffer on a renderpass.
///
/// Single-sample targets also get `eSampled | eTransferSrc` so they can leave
/// the pass as a shader-readable texture. Pass `extraUsage` (typically
/// `eTransferDst` and/or `eStorage`) when the image is also a copy destination
/// or a compute storage image — scene-color ping-pong and offscreen Canvas.
class ColorAttachmentImage : public GenericImage {
public:
  ColorAttachmentImage() {}

  ColorAttachmentImage(Device& device, uint32_t width, uint32_t height, vk::Format format = vk::Format::eR8G8B8A8Unorm,
                       vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
                       vk::ImageUsageFlags extraUsage = {}) {
    vk::ImageCreateInfo info;
    info.flags = {};

    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = vk::ImageTiling::eOptimal;
    // MSAA targets are resolve sources only. SAMPLED/TRANSFER on a multisample
    // image is rejected or crashes later on MoltenVK/Metal.
    info.usage = vk::ImageUsageFlagBits::eColorAttachment;
    if (samples == vk::SampleCountFlagBits::e1)
      info.usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
    info.usage |= extraUsage;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices = nullptr;
    info.initialLayout = vk::ImageLayout::eUndefined;
    typedef vk::ImageAspectFlagBits iafb;
    create(device, info, vk::ImageViewType::e2D, iafb::eColor, false);
  }
};

using ColorTarget = ColorAttachmentImage;

/// Depth (and optional sampled) attachment. `sampled=true` is required to produce a SampledImage.
class DepthTarget : public GenericImage {
public:
  DepthTarget() {}

  DepthTarget(Device& device, uint32_t width, uint32_t height,
              vk::Format format = vk::Format::eD32Sfloat, bool sampled = false,
              vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1) {
    vk::ImageCreateInfo info;
    info.flags = {};
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (sampled)
      info.usage |= vk::ImageUsageFlagBits::eSampled;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.initialLayout = vk::ImageLayout::eUndefined;
    create(device, info, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eDepth, false);
  }

  SampledImage sampled(const DepthSampler &sampler) const {
    return GenericImage::sampled(sampler.handle);
  }

private:
  using GenericImage::sampled;
};

/// Depth 2D array (CSM cascades). Array view is for sampling; layer views are attachments.
class DepthArrayImage : public GenericImage {
public:
  DepthArrayImage() {}

  DepthArrayImage(Device& device, uint32_t width, uint32_t height, uint32_t layers,
                  vk::Format format = vk::Format::eD32Sfloat) {
    vk::ImageCreateInfo info;
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{ width, height, 1U };
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.initialLayout = vk::ImageLayout::eUndefined;
    create(device, info, vk::ImageViewType::e2DArray, vk::ImageAspectFlagBits::eDepth, false);

    layerViews.resize(layers);
    for (uint32_t i = 0; i < layers; ++i) {
      vk::ImageViewCreateInfo layerInfo{};
      layerInfo.image = image();
      layerInfo.viewType = vk::ImageViewType::e2D;
      layerInfo.format = format;
      layerInfo.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, i, 1};
      layerViews[i] = device->createImageViewUnique(layerInfo);
    }
  }

  vk::ImageView arrayView() const { return imageView(); }
  uint32_t layerCount() const { return uint32_t(layerViews.size()); }
  vk::ImageView layerView(uint32_t i) const { return *layerViews[i]; }
  AttachmentView layerAttachment(uint32_t i) const {
    return AttachmentView{layerView(i), vk::ImageAspectFlagBits::eDepth,
                          vk::ImageLayout::eDepthStencilAttachmentOptimal};
  }

  SampledImage sampled(const DepthSampler &sampler) const {
    return GenericImage::sampled(sampler.handle);
  }

private:
  using GenericImage::sampled;
  std::vector<vk::UniqueImageView> layerViews;
};

using Image2DArray = DepthArrayImage;

/// Cube-compatible render / storage target (HDR reflection probes, IBL bake).
///
/// Default usage covers sampled + color-attachment + storage + transfer so a
/// host can render a face, blit into a layer, and run a compute filter without
/// falling back to raw `vk::UniqueImage` allocation. The primary view is a
/// cube view over every mip/layer; `layerView` / `layerAttachment` expose
/// single-face mip-0 views for framebuffer attachments.
class CubemapRenderTarget : public GenericImage {
public:
  CubemapRenderTarget() {}

  CubemapRenderTarget(
      Device &device, uint32_t size, uint32_t mipLevels = 1,
      vk::Format format = vk::Format::eR16G16B16A16Sfloat,
      vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eStorage) {
    if (size == 0 || mipLevels == 0)
      throw std::runtime_error("CubemapRenderTarget: size and mipLevels must be non-zero");
    vk::ImageCreateInfo info{};
    info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{size, size, 1U};
    info.mipLevels = mipLevels;
    info.arrayLayers = 6;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = usage;
    info.sharingMode = vk::SharingMode::eExclusive;
    info.initialLayout = vk::ImageLayout::eUndefined;
    create(device, info, vk::ImageViewType::eCube, vk::ImageAspectFlagBits::eColor, false);

    layerViews.resize(6);
    for (uint32_t face = 0; face < 6; ++face) {
      vk::ImageViewCreateInfo layerInfo{};
      layerInfo.image = image();
      layerInfo.viewType = vk::ImageViewType::e2D;
      layerInfo.format = format;
      layerInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, face, 1};
      layerViews[face] = device->createImageViewUnique(layerInfo);
    }
  }

  bool valid() const { return static_cast<bool>(s.image); }
  vk::ImageView cubeView() const { return imageView(); }
  uint32_t faceCount() const { return uint32_t(layerViews.size()); }
  uint32_t mipCount() const { return info().mipLevels; }
  vk::Format format() const { return info().format; }
  uint32_t faceSize() const { return info().extent.width; }

  vk::ImageView layerView(uint32_t face) const {
    if (face >= layerViews.size())
      throw std::runtime_error("CubemapRenderTarget::layerView: face out of range");
    return *layerViews[face];
  }

  AttachmentView layerAttachment(uint32_t face) const {
    return AttachmentView{layerView(face), vk::ImageAspectFlagBits::eColor,
                          vk::ImageLayout::eColorAttachmentOptimal};
  }

  /// Temporary cube view covering only mip 0 (compute filter sample source).
  vk::UniqueImageView createBaseCubeView() const {
    if (!device) throw std::runtime_error("CubemapRenderTarget::createBaseCubeView: empty");
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image();
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = info().format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6};
    return (*device)->createImageViewUnique(viewInfo);
  }

  /// Temporary 2D-array view for one mip (compute storage destination).
  vk::UniqueImageView createMipArrayView(uint32_t mip) const {
    if (!device) throw std::runtime_error("CubemapRenderTarget::createMipArrayView: empty");
    if (mip >= info().mipLevels)
      throw std::runtime_error("CubemapRenderTarget::createMipArrayView: mip out of range");
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image();
    viewInfo.viewType = vk::ImageViewType::e2DArray;
    viewInfo.format = info().format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, mip, 1, 0, 6};
    return (*device)->createImageViewUnique(viewInfo);
  }

private:
  std::vector<vk::UniqueImageView> layerViews;
};

/// A class to help build samplers.
/// Samplers tell the shader stages how to sample an image.
/// They are used in combination with an image to make a combined image sampler
/// used by texture() calls in shaders.
/// They can also be passed to shaders directly for use on multiple image sources.
class SamplerBuilder {
public:
  /// Default to a very basic sampler.
  SamplerBuilder() {
    s.info.magFilter = vk::Filter::eNearest;
    s.info.minFilter = vk::Filter::eNearest;
    s.info.mipmapMode = vk::SamplerMipmapMode::eNearest;
    s.info.addressModeU = vk::SamplerAddressMode::eRepeat;
    s.info.addressModeV = vk::SamplerAddressMode::eRepeat;
    s.info.addressModeW = vk::SamplerAddressMode::eRepeat;
    s.info.mipLodBias = 0.0f;
    s.info.anisotropyEnable = 0;
    s.info.maxAnisotropy = 0.0f;
    s.info.compareEnable = 0;
    s.info.compareOp = vk::CompareOp::eNever;
    s.info.minLod = 0;
    s.info.maxLod = 0;
    s.info.borderColor = vk::BorderColor{};
    s.info.unnormalizedCoordinates = 0;
  }

  ////////////////////
  //
  // Setters
  //
  SamplerBuilder &flags(vk::SamplerCreateFlags value) { s.info.flags = value; return *this; }

  /// Set the magnify filter value. (for close textures)
  /// Options are: vk::Filter::eLinear and vk::Filter::eNearest
  SamplerBuilder &magFilter(vk::Filter value) { s.info.magFilter = value; return *this; }

  /// Set the minnify filter value. (for far away textures)
  /// Options are: vk::Filter::eLinear and vk::Filter::eNearest
  SamplerBuilder &minFilter(vk::Filter value) { s.info.minFilter = value; return *this; }

  /// Set the minnify filter value. (for far away textures)
  /// Options are: vk::SamplerMipmapMode::eLinear and vk::SamplerMipmapMode::eNearest
  SamplerBuilder &mipmapMode(vk::SamplerMipmapMode value) { s.info.mipmapMode = value; return *this; }
  SamplerBuilder &addressModeU(vk::SamplerAddressMode value) { s.info.addressModeU = value; return *this; }
  SamplerBuilder &addressModeV(vk::SamplerAddressMode value) { s.info.addressModeV = value; return *this; }
  SamplerBuilder &addressModeW(vk::SamplerAddressMode value) { s.info.addressModeW = value; return *this; }
  SamplerBuilder &mipLodBias(float value) { s.info.mipLodBias = value; return *this; }
  SamplerBuilder &anisotropyEnable(vk::Bool32 value) { s.info.anisotropyEnable = value; return *this; }
  SamplerBuilder &maxAnisotropy(float value) { s.info.maxAnisotropy = value; return *this; }
  SamplerBuilder &compareEnable(vk::Bool32 value) { s.info.compareEnable = value; return *this; }
  SamplerBuilder &compareOp(vk::CompareOp value) { s.info.compareOp = value; return *this; }
  SamplerBuilder &minLod(float value) { s.info.minLod = value; return *this; }
  SamplerBuilder &maxLod(float value) { s.info.maxLod = value; return *this; }
  SamplerBuilder &borderColor(vk::BorderColor value) { s.info.borderColor = value; return *this; }
  SamplerBuilder &unnormalizedCoordinates(vk::Bool32 value) { s.info.unnormalizedCoordinates = value; return *this; }

  /// Nearest + clamp. Required for raw D32 depth compares (linear is not valid PCF).
  SamplerBuilder &nearestClamp() {
    magFilter(vk::Filter::eNearest);
    minFilter(vk::Filter::eNearest);
    mipmapMode(vk::SamplerMipmapMode::eNearest);
    addressModeU(vk::SamplerAddressMode::eClampToEdge);
    addressModeV(vk::SamplerAddressMode::eClampToEdge);
    addressModeW(vk::SamplerAddressMode::eClampToEdge);
    return *this;
  }

  /// Linear + clamp. For shadow-map PCF sampling (see buildDepthPcf).
  SamplerBuilder &linearClamp() {
    magFilter(vk::Filter::eLinear);
    minFilter(vk::Filter::eLinear);
    mipmapMode(vk::SamplerMipmapMode::eNearest);
    addressModeU(vk::SamplerAddressMode::eClampToEdge);
    addressModeV(vk::SamplerAddressMode::eClampToEdge);
    addressModeW(vk::SamplerAddressMode::eClampToEdge);
    return *this;
  }

  /// Allocate a self-deleting image.
  vk::UniqueSampler buildUnique(vk::Device device) const {
    return device.createSamplerUnique(s.info);
  }

  /// Allocate a non self-deleting Sampler. Does not check device feature tags.
  vk::Sampler build(vk::Device device) const {
    return device.createSampler(s.info);
  }

  /// Checks DeviceCaps::samplerAnisotropy before enabling anisotropic filtering.
  vk::Sampler build(const Device &device) const {
    if (s.info.anisotropyEnable && !device.hasAnisotropy())
      throw std::runtime_error("anisotropic filtering requires DeviceCaps::samplerAnisotropy");
    return device->createSampler(s.info, device.allocation_callbacks);
  }

  /// Depth sampler: nearest filter is required (linear D32 is not valid PCF).
  DepthSampler buildDepth(const Device &device) const {
    if (s.info.magFilter != vk::Filter::eNearest || s.info.minFilter != vk::Filter::eNearest)
      throw std::runtime_error("depth sampling requires nearest filter (linear D32 is not valid PCF)");
    return DepthSampler{build(device)};
  }

  /// Hardware-PCF depth sampler: linear filtering + depth compare. The compare
  /// result of each 2x2 filtered depth sample is blended, giving hardware PCF.
  /// Requires linear mag/min filter and compareEnable/compareOp to be set.
  DepthSampler buildDepthPcf(const Device &device) const {
    if (s.info.magFilter != vk::Filter::eLinear || s.info.minFilter != vk::Filter::eLinear)
      throw std::runtime_error("depth PCF requires linear filter (see linearClamp)");
    if (!s.info.compareEnable)
      throw std::runtime_error("depth PCF requires compareEnable (see compareEnable(true))");
    return DepthSampler{build(device)};
  }

private:
  struct State {
    vk::SamplerCreateInfo info;
  };

  State s;
};


#pragma endregion


#pragma region DesciptorSet

struct BoundSet;

/// Newly allocated (or reopened after GPU idle). May be written with DescriptorSetUpdater.
struct UnboundSet {
  vk::DescriptorSet handle{};
  UnboundSet() = default;
  explicit UnboundSet(vk::DescriptorSet s) : handle(s) {}
  UnboundSet(const UnboundSet &) = delete;
  UnboundSet &operator=(const UnboundSet &) = delete;
  UnboundSet(UnboundSet &&o) noexcept : handle(o.handle) { o.handle = vk::DescriptorSet{}; }
  UnboundSet &operator=(UnboundSet &&o) noexcept {
    handle = o.handle;
    o.handle = vk::DescriptorSet{};
    return *this;
  }
  explicit operator bool() const { return static_cast<bool>(handle); }
  BoundSet publish() &&;
  /// After waitIdle / slot fence, a bound set may be updated again.
  static UnboundSet reopenAfterIdle(const BoundSet &bound);
};

/// Published set. May be bound; must not be updated while in-flight.
struct BoundSet {
  vk::DescriptorSet handle{};
  BoundSet() = default;
  explicit BoundSet(vk::DescriptorSet s) : handle(s) {}
  operator vk::DescriptorSet() const { return handle; }
  explicit operator bool() const { return static_cast<bool>(handle); }
  const vk::DescriptorSet *ptr() const { return &handle; }
};

inline BoundSet UnboundSet::publish() && { return BoundSet{handle}; }
inline UnboundSet UnboundSet::reopenAfterIdle(const BoundSet &bound) {
  return UnboundSet{bound.handle};
}

inline void RecordingCmd::bindGraphics(vk::PipelineLayout layout, const BoundSet &set,
                                       uint32_t firstSet) {
  vk::DescriptorSet h = set;
  commandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, firstSet, 1, &h, 0,
                                     nullptr);
}
inline void InRenderPass::bindGraphics(vk::PipelineLayout layout, const BoundSet &set,
                                       uint32_t firstSet) {
  vk::DescriptorSet h = set;
  commandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, firstSet, 1, &h, 0,
                                     nullptr);
}

/// Convenience class for updating descriptor sets (uniforms)
class DescriptorSetUpdater {
public:
  DescriptorSetUpdater(int maxBuffers = 10, int maxImages = 10, int maxBufferViews = 0) {
    // we must pre-size these buffers as we take pointers to their members.
    bufferInfo_.resize(maxBuffers);
    imageInfo_.resize(maxImages);
    bufferViews_.resize(maxBufferViews);
  }

  /// Call this to begin a new descriptor set.
  DescriptorSetUpdater& beginDescriptorSet(UnboundSet &dstSet) {
    dstSet_ = dstSet.handle;
    return *this;
  }

  /// Call this to begin a new set of images.
  DescriptorSetUpdater& beginImages(uint32_t dstBinding, uint32_t dstArrayElement, vk::DescriptorType descriptorType) {
    vk::WriteDescriptorSet wdesc{};
    wdesc.dstSet = dstSet_;
    wdesc.dstBinding = dstBinding;
    wdesc.dstArrayElement = dstArrayElement;
    wdesc.descriptorCount = 0;
    wdesc.descriptorType = descriptorType;
    wdesc.pImageInfo = imageInfo_.data() + numImages_;
    descriptorWrites_.push_back(wdesc);
    return *this;
  }

  /// Call this to add a combined image sampler. Layout must come from SampledImage.
  DescriptorSetUpdater& image(const SampledImage &sampled) {
    return image(sampled.sampler, sampled.view, sampled.layout);
  }

private:
  /// Call this to add a combined image sampler.
  DescriptorSetUpdater& image(vk::Sampler sampler, vk::ImageView imageView, vk::ImageLayout imageLayout) {
    if (!descriptorWrites_.empty() && numImages_ != imageInfo_.size() && descriptorWrites_.back().pImageInfo) {
      descriptorWrites_.back().descriptorCount++;
      imageInfo_[numImages_++] = vk::DescriptorImageInfo{sampler, imageView, imageLayout};
    } else {
      ok_ = false;
    }
    return *this;
  }

public:
  /// Call this to start defining buffers.
  DescriptorSetUpdater& beginBuffers(uint32_t dstBinding, uint32_t dstArrayElement, vk::DescriptorType descriptorType) {
    vk::WriteDescriptorSet wdesc{};
    wdesc.dstSet = dstSet_;
    wdesc.dstBinding = dstBinding;
    wdesc.dstArrayElement = dstArrayElement;
    wdesc.descriptorCount = 0;
    wdesc.descriptorType = descriptorType;
    wdesc.pBufferInfo = bufferInfo_.data() + numBuffers_;
    descriptorWrites_.push_back(wdesc);
    return *this;
  }

  /// Call this to add a new buffer.
  DescriptorSetUpdater& buffer(vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range) {
    if (!descriptorWrites_.empty() && numBuffers_ != bufferInfo_.size() && descriptorWrites_.back().pBufferInfo) {
      descriptorWrites_.back().descriptorCount++;
      bufferInfo_[numBuffers_++] = vk::DescriptorBufferInfo{buffer, offset, range};
    } else {
      ok_ = false;
    }
    return *this;
  }

  /// Call this to start adding buffer views. (for example, writable images).
  void beginBufferViews(uint32_t dstBinding, uint32_t dstArrayElement, vk::DescriptorType descriptorType) {
    vk::WriteDescriptorSet wdesc{};
    wdesc.dstSet = dstSet_;
    wdesc.dstBinding = dstBinding;
    wdesc.dstArrayElement = dstArrayElement;
    wdesc.descriptorCount = 0;
    wdesc.descriptorType = descriptorType;
    wdesc.pTexelBufferView = bufferViews_.data() + numBufferViews_;
    descriptorWrites_.push_back(wdesc);
  }

  /// Call this to add a buffer view. (Texel images)
  void bufferView(vk::BufferView view) {
    if (!descriptorWrites_.empty() && numBufferViews_ != bufferViews_.size() && descriptorWrites_.back().pImageInfo) {
      descriptorWrites_.back().descriptorCount++;
      bufferViews_[numBufferViews_++] = view;
    } else {
      ok_ = false;
    }
  }

  /// Copy an existing descriptor.
  void copy(vk::DescriptorSet srcSet, uint32_t srcBinding, uint32_t srcArrayElement, vk::DescriptorSet dstSet, uint32_t dstBinding, uint32_t dstArrayElement, uint32_t descriptorCount) {
    descriptorCopies_.emplace_back(srcSet, srcBinding, srcArrayElement, dstSet, dstBinding, dstArrayElement, descriptorCount);
  }

  /// Call this to update the descriptor sets with their pointers (but not data).
  void update(const vk::Device &device) const {
    device.updateDescriptorSets( descriptorWrites_, descriptorCopies_ );
  }

  /// Returns true if the updater is error free.
  bool ok() const { return ok_; }
private:
  std::vector<vk::DescriptorBufferInfo> bufferInfo_;
  std::vector<vk::DescriptorImageInfo> imageInfo_;
  std::vector<vk::WriteDescriptorSet> descriptorWrites_;
  std::vector<vk::CopyDescriptorSet> descriptorCopies_;
  std::vector<vk::BufferView> bufferViews_;
  vk::DescriptorSet dstSet_;
  int numBuffers_ = 0;
  int numImages_ = 0;
  int numBufferViews_ = 0;
  bool ok_ = true;
};

/// A factory class for descriptor set layouts. (An interface to the shaders)
class DescriptorSetLayoutBuilder {
public:
  DescriptorSetLayoutBuilder() {
  }

  DescriptorSetLayoutBuilder& buffer(uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags, uint32_t descriptorCount) {
    s.bindings.emplace_back(binding, descriptorType, descriptorCount, stageFlags, nullptr);
    return *this;
  }

  DescriptorSetLayoutBuilder& image(uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags, uint32_t descriptorCount) {
    s.bindings.emplace_back(binding, descriptorType, descriptorCount, stageFlags, nullptr);
    return *this;
  }

  DescriptorSetLayoutBuilder& samplers(uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags, const std::vector<vk::Sampler> immutableSamplers) {
    s.samplers.push_back(immutableSamplers);
    auto pImmutableSamplers = s.samplers.back().data();
    s.bindings.emplace_back(binding, descriptorType, (uint32_t)immutableSamplers.size(), stageFlags, pImmutableSamplers);
    return *this;
  }

  DescriptorSetLayoutBuilder& bufferView(uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags, uint32_t descriptorCount) {
    s.bindings.emplace_back(binding, descriptorType, descriptorCount, stageFlags, nullptr);
    return *this;
  }

  /// Create a self-deleting descriptor set object.
  vk::UniqueDescriptorSetLayout createUnique(vk::Device device) const {
    vk::DescriptorSetLayoutCreateInfo dsci{};
    dsci.bindingCount = (uint32_t)s.bindings.size();
    dsci.pBindings = s.bindings.data();
    return device.createDescriptorSetLayoutUnique(dsci);
  }

private:
  struct State {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    std::vector<std::vector<vk::Sampler> > samplers;
    int numSamplers = 0;
  };

  State s;
};

/// A factory class for descriptor sets (A set of uniform bindings)
class DescriptorSetBuilder {
public:
  // Construct a new, empty DescriptorSetBuilder.
  DescriptorSetBuilder() {}

  /// Add another layout describing a descriptor set.
  DescriptorSetBuilder &layout(vk::DescriptorSetLayout layout) {
    s.layouts.push_back(layout);
    return *this;
  }

  /// Allocate a vector of non-self-deleting descriptor sets
  /// Note: descriptor sets get freed with the pool, so this is the better choice.
  std::vector<vk::DescriptorSet> build(vk::Device device, vk::DescriptorPool descriptorPool) const {
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = (uint32_t)s.layouts.size();
    dsai.pSetLayouts = s.layouts.data();
    return device.allocateDescriptorSets(dsai);
  }

  /// Allocate a vector of self-deleting descriptor sets.
  std::vector<vk::UniqueDescriptorSet> buildUnique(vk::Device device, vk::DescriptorPool descriptorPool) const {
    vk::DescriptorSetAllocateInfo dsai{};
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = (uint32_t)s.layouts.size();
    dsai.pSetLayouts = s.layouts.data();
    return device.allocateDescriptorSetsUnique(dsai);
  }

private:
  struct State {
    std::vector<vk::DescriptorSetLayout> layouts;
  };

  State s;
};

#pragma endregion

inline PhysicalDeviceSelector Instance::selectPhysicalDevice() const {
  return PhysicalDeviceSelector(*this);
}

inline DeviceBuilder PhysicalDevice::createDevice() const {
  return DeviceBuilder(*this);
}

inline SwapchainBuilder Device::createSwapchain() { return SwapchainBuilder(*this); }
inline PresentBuilder Device::createPresent(Swapchain &swapchain) {
  return PresentBuilder(*this, swapchain);
}
inline RenderPassBuilder Device::createRenderPass() const { return RenderPassBuilder(*this); }
inline PipelineBuilder Device::createPipeline() const { return PipelineBuilder(*this); }
inline PipelineBuilder Device::createPipeline(const Swapchain &swapchain) const {
  return PipelineBuilder(*this, swapchain);
}
inline PipelineLayoutBuilder Device::createPipelineLayout() const {
  return PipelineLayoutBuilder(*this);
}
inline ColorAttachmentImage Device::createColorTarget(uint32_t width, uint32_t height,
                                                      vk::Format format,
                                                      vk::SampleCountFlagBits samples,
                                                      vk::ImageUsageFlags extraUsage) {
  return ColorAttachmentImage(*this, width, height, format, samples, extraUsage);
}
inline ColorAttachmentImage Device::createCopyableColorTarget(uint32_t width, uint32_t height,
                                                              vk::Format format) {
  return ColorAttachmentImage(*this, width, height, format, vk::SampleCountFlagBits::e1,
                              vk::ImageUsageFlagBits::eTransferDst);
}
inline DepthTarget Device::createDepthTarget(uint32_t width, uint32_t height, vk::Format format,
                                             bool sampled, vk::SampleCountFlagBits samples) {
  return DepthTarget(*this, width, height, format, sampled, samples);
}
inline DepthArrayImage Device::createDepthArray(uint32_t width, uint32_t height, uint32_t layers,
                                                vk::Format format) {
  return DepthArrayImage(*this, width, height, layers, format);
}
inline CubemapRenderTarget Device::createCubemapRenderTarget(uint32_t size, uint32_t mipLevels,
                                                             vk::Format format,
                                                             vk::ImageUsageFlags usage) {
  return CubemapRenderTarget(*this, size, mipLevels, format, usage);
}

/// Record a color/depth image → host-visible buffer copy with layout restore.
/// The destination buffer must already be large enough for `extent` at the
/// image's texel size; a host-read barrier is inserted after the copy.
inline void recordImageToBufferReadback(vk::CommandBuffer cb, vk::Image source, vk::Buffer destination,
                                        vk::Extent2D extent, vk::ImageAspectFlags aspect, uint32_t layer,
                                        vk::ImageLayout originalLayout) {
  vk::ImageMemoryBarrier barrier{};
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = source;
  barrier.subresourceRange = {aspect, 0, 1, layer, 1};
  barrier.oldLayout = originalLayout;
  barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  const bool present = originalLayout == vk::ImageLayout::ePresentSrcKHR;
  barrier.srcAccessMask =
      present ? vk::AccessFlagBits::eColorAttachmentWrite
              : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
  cb.pipelineBarrier(present ? vk::PipelineStageFlagBits::eColorAttachmentOutput
                             : vk::PipelineStageFlagBits::eAllCommands,
                     vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1, &barrier);
  vk::BufferImageCopy region{};
  region.imageSubresource = {aspect, 0, layer, 1};
  region.imageExtent = vk::Extent3D{extent.width, extent.height, 1};
  cb.copyImageToBuffer(source, vk::ImageLayout::eTransferSrcOptimal, destination, region);
  vk::BufferMemoryBarrier hostRead{};
  hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hostRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  hostRead.dstAccessMask = vk::AccessFlagBits::eHostRead;
  hostRead.buffer = destination;
  hostRead.offset = 0;
  hostRead.size = VK_WHOLE_SIZE;
  cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, 0,
                     nullptr, 1, &hostRead, 0, nullptr);
  std::swap(barrier.oldLayout, barrier.newLayout);
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
  barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                     present ? vk::PipelineStageFlagBits::eBottomOfPipe
                             : vk::PipelineStageFlagBits::eAllCommands,
                     {}, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace vkb

#if defined(VKB_IMPL)
namespace vkb {

bool InstanceBuilder::loaded = false;

namespace helper { 
VKAPI_ATTR VkBool32 VKAPI_CALL default_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {
  auto ms = to_string_message_severity(messageSeverity);
  auto mt = to_string_message_type(messageType);
  printf("[%s: %s]\n%s\n", ms, mt, pCallbackData->pMessage);
  return VK_FALSE;
}
}
}
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif
