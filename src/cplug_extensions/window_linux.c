/* Released into the public domain by Tré Dudman - 2026
 * For licensing and more info see https://github.com/Tremus/CPLUG */

// This is a stub
// X11 only, through XCB. Wayland desktops run it through XWayland

#ifdef PW_VULKAN
/*
Minimal Vulkan loader. dlopens libvulkan.so.1 at runtime so nothing links -lvulkan, and a missing driver is a readable
error instead of a plugin that silently fails to load.

sokol_gfx's Vulkan backend calls Vulkan functions directly by name, so it's built in the same compile unit, after this
file (xplug does this in src/platform_linux.c). Every function it and this file use is a static function pointer with the normal Vulkan
name, so those direct calls go through these unchanged.

If sokol starts calling a function that isn't listed, the build fails with an implicit declaration error (all
prototypes are disabled with VK_NO_PROTOTYPES). Add it to the right list:
- PWVK_GLOBAL_FUNCS:   loaded with vkGetInstanceProcAddr(NULL, ...)
- PWVK_INSTANCE_FUNCS: first parameter is VkInstance or VkPhysicalDevice
- PWVK_DEVICE_FUNCS:   first parameter is VkDevice, VkQueue or VkCommandBuffer
Device functions are loaded straight from the driver with vkGetDeviceProcAddr. That's fine as globals because there is
only one VkDevice per process (see below).

Extension functions sokol looks up itself (VK_EXT_descriptor_buffer, vkSetDebugUtilsObjectNameEXT) aren't listed.
vkCreateXcbSurfaceKHR needs platform headers, so look it up with vkGetInstanceProcAddr.
*/
#if defined(VULKAN_CORE_H_) && !defined(VK_NO_PROTOTYPES)
#error window_linux.c must be included before any Vulkan header
#endif

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <dlfcn.h>
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

// clang-format off
#define PWVK_GLOBAL_FUNCS(X)                       \
    X(vkCreateInstance)                            \
    X(vkEnumerateInstanceExtensionProperties)      \
    X(vkEnumerateInstanceLayerProperties)          \
    X(vkEnumerateInstanceVersion)

#define PWVK_INSTANCE_FUNCS(X)                     \
    X(vkCreateDevice)                              \
    X(vkDestroyInstance)                           \
    X(vkDestroySurfaceKHR)                         \
    X(vkEnumerateDeviceExtensionProperties)        \
    X(vkEnumeratePhysicalDevices)                  \
    X(vkGetDeviceProcAddr)                         \
    X(vkGetPhysicalDeviceFeatures2)                \
    X(vkGetPhysicalDeviceFormatProperties)         \
    X(vkGetPhysicalDeviceImageFormatProperties2)   \
    X(vkGetPhysicalDeviceMemoryProperties)         \
    X(vkGetPhysicalDeviceProperties)               \
    X(vkGetPhysicalDeviceProperties2)              \
    X(vkGetPhysicalDeviceQueueFamilyProperties)    \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)        \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)   \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)

// NULL unless VK_EXT_debug_utils is enabled
#define PWVK_OPTIONAL_INSTANCE_FUNCS(X)            \
    X(vkCreateDebugUtilsMessengerEXT)              \
    X(vkDestroyDebugUtilsMessengerEXT)

#define PWVK_DEVICE_FUNCS(X)                       \
    X(vkAcquireNextImageKHR)                       \
    X(vkAllocateCommandBuffers)                    \
    X(vkAllocateMemory)                            \
    X(vkBeginCommandBuffer)                        \
    X(vkBindBufferMemory)                          \
    X(vkBindImageMemory)                           \
    X(vkCmdBeginRendering)                         \
    X(vkCmdBindIndexBuffer)                        \
    X(vkCmdBindPipeline)                           \
    X(vkCmdBindVertexBuffers)                      \
    X(vkCmdCopyBuffer)                             \
    X(vkCmdCopyBufferToImage2)                     \
    X(vkCmdCopyImage2)                             \
    X(vkCmdDispatch)                               \
    X(vkCmdDraw)                                   \
    X(vkCmdDrawIndexed)                            \
    X(vkCmdEndRendering)                           \
    X(vkCmdPipelineBarrier2)                       \
    X(vkCmdSetScissor)                             \
    X(vkCmdSetViewport)                            \
    X(vkCreateBuffer)                              \
    X(vkCreateCommandPool)                         \
    X(vkCreateComputePipelines)                    \
    X(vkCreateDescriptorSetLayout)                 \
    X(vkCreateFence)                               \
    X(vkCreateGraphicsPipelines)                   \
    X(vkCreateImage)                               \
    X(vkCreateImageView)                           \
    X(vkCreatePipelineLayout)                      \
    X(vkCreateSampler)                             \
    X(vkCreateSemaphore)                           \
    X(vkCreateShaderModule)                        \
    X(vkCreateSwapchainKHR)                        \
    X(vkDestroyBuffer)                             \
    X(vkDestroyCommandPool)                        \
    X(vkDestroyDescriptorSetLayout)                \
    X(vkDestroyDevice)                             \
    X(vkDestroyFence)                              \
    X(vkDestroyImage)                              \
    X(vkDestroyImageView)                          \
    X(vkDestroyPipeline)                           \
    X(vkDestroyPipelineLayout)                     \
    X(vkDestroySampler)                            \
    X(vkDestroySemaphore)                          \
    X(vkDestroyShaderModule)                       \
    X(vkDestroySwapchainKHR)                       \
    X(vkDeviceWaitIdle)                            \
    X(vkEndCommandBuffer)                          \
    X(vkFreeMemory)                                \
    X(vkGetBufferDeviceAddress)                    \
    X(vkGetBufferMemoryRequirements)               \
    X(vkGetDeviceQueue)                            \
    X(vkGetImageMemoryRequirements)                \
    X(vkGetSwapchainImagesKHR)                     \
    X(vkMapMemory)                                 \
    X(vkQueuePresentKHR)                           \
    X(vkQueueSubmit)                               \
    X(vkQueueWaitIdle)                             \
    X(vkResetCommandBuffer)                        \
    X(vkResetFences)                               \
    X(vkUnmapMemory)                               \
    X(vkWaitForFences)
// clang-format on

#define PWVK_DEFINE(name) static PFN_##name name;
PWVK_DEFINE(vkGetInstanceProcAddr)
PWVK_GLOBAL_FUNCS(PWVK_DEFINE)
PWVK_INSTANCE_FUNCS(PWVK_DEFINE)
PWVK_OPTIONAL_INSTANCE_FUNCS(PWVK_DEFINE)
PWVK_DEVICE_FUNCS(PWVK_DEFINE)
#undef PWVK_DEFINE

static void* g_libvulkan;

static void pwvk_unload(void)
{
#define X(name) name = NULL;
    X(vkGetInstanceProcAddr)
    PWVK_GLOBAL_FUNCS(X)
    PWVK_INSTANCE_FUNCS(X)
    PWVK_OPTIONAL_INSTANCE_FUNCS(X)
    PWVK_DEVICE_FUNCS(X)
#undef X
    if (g_libvulkan)
        dlclose(g_libvulkan);
    g_libvulkan = NULL;
}

// Loads 'name' through 'getter' and remembers the first one that's missing
#define PWVK_LOAD(getter, handle, name)                                                                                \
    name = (PFN_##name)getter(handle, #name);                                                                          \
    if (!name && !missing)                                                                                             \
        missing = #name;

// Each returns NULL on success, otherwise the name of the first thing that couldn't be loaded

// dlopens libvulkan.so.1 and loads PWVK_GLOBAL_FUNCS
static const char* pwvk_load(void)
{
    g_libvulkan = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_libvulkan)
        return "libvulkan.so.1";

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(g_libvulkan, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr)
    {
        pwvk_unload();
        return "vkGetInstanceProcAddr";
    }

    const char* missing = NULL;
#define X(name) PWVK_LOAD(vkGetInstanceProcAddr, NULL, name)
    PWVK_GLOBAL_FUNCS(X)
#undef X
    if (missing)
        pwvk_unload();
    return missing;
}

static const char* pwvk_load_instance(VkInstance instance)
{
    const char* missing = NULL;
#define X(name) PWVK_LOAD(vkGetInstanceProcAddr, instance, name)
    PWVK_INSTANCE_FUNCS(X)
#undef X
#define X(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);
    PWVK_OPTIONAL_INSTANCE_FUNCS(X)
#undef X
    return missing;
}

static const char* pwvk_load_device(VkDevice device)
{
    const char* missing = NULL;
#define X(name) PWVK_LOAD(vkGetDeviceProcAddr, device, name)
    PWVK_DEVICE_FUNCS(X)
#undef X
    return missing;
}
#endif // PW_VULKAN

#include <cplug.h>
#include <cplug_extensions/window.h>
#include <stdio.h>
#include <string.h>

#define PW_ARRLEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef PW_VULKAN
/*
Vulkan device + swapchain

Requirements are set by sokol_gfx's Vulkan backend: Vulkan 1.3 with VK_EXT_descriptor_buffer, plus dual source
blending for xvg's subpixel text. Everything is checked here so a missing driver feature is a readable error instead of
a sokol panic inside the host.

Vulkan is loaded by the loader at the top of this file (dlopen libvulkan.so.1). Nothing links -lvulkan.

One VkInstance + VkDevice + VkQueue is shared by every window in the process (refcounted). The queue needs external
synchronisation, so all calls must be made from the main thread.

Usage in the window code:
    pwvk_context_retain(err, sizeof(err))     // NULL on failure, with err filled in
    create a VkSurfaceKHR with the returned instance
    pwvk_swapchain_init(&swapchain, surface)  // takes ownership of the surface
    every frame:
        if (pwvk_swapchain_acquire(&swapchain, width_px, height_px))
        {
            pw_tick(gui); // sokol renders into swapchain.images[swapchain.image_index]. It must submit a swapchain pass
            pwvk_swapchain_present(&swapchain);
        }
    pwvk_swapchain_deinit(&swapchain)
    pwvk_context_release()

vkCreateXcbSurfaceKHR needs platform headers, so look it up with vkGetInstanceProcAddr(instance, ...)
*/

// TODO: remove once the X11 window code calls these
#define PWVK_FUNC static __attribute__((unused))

typedef struct PWVKContext
{
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family_index;
} PWVKContext;

enum
{
    PWVK_MAX_SWAPCHAIN_IMAGES = 8,
    // Must be more than sokol's SG_NUM_INFLIGHT_FRAMES (2). See PWVKSwapchain
    PWVK_NUM_ACQUIRE_SEMAPHORES = 3,
};

// Vulkan guarantees no surface format, but B8G8R8A8_UNORM is the one every Linux driver offers. Lavapipe under WSLg has
// no R8G8B8A8. Match this with SG_PIXELFORMAT_BGRA8. Not sRGB: blending happens in gamma space on every backend
#define PWVK_SWAPCHAIN_FORMAT VK_FORMAT_B8G8R8A8_UNORM

typedef struct PWVKSwapchain
{
    VkSurfaceKHR   surface;
    VkSwapchainKHR swapchain;
    VkExtent2D     requested_extent; // Size passed to the last recreate, before clamping to the surface caps
    bool           needs_recreate;

    uint32_t    num_images;
    VkImage     images[PWVK_MAX_SWAPCHAIN_IMAGES];
    VkImageView views[PWVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t    image_index; // Valid between acquire and present
    uint32_t    frame_index; // Incremented by every successful acquire

    // A single semaphore pair isn't valid: the next submit can signal render_finished while the last present still
    // waits on it, and the next acquire can signal present_complete while the last submit still waits on it.
    // sokol takes a new pair every frame (it clears them in sg_commit), so:
    // - render_finished is per image. Reacquiring an image means its last present is done
    // - present_complete is per frame, in a ring longer than sokol's frames in flight. sokol waits on the fence of the
    //   submit that used a semaphore before it comes round again. Per image isn't enough: with MAILBOX an image can be
    //   reacquired before sokol has waited on that fence
    // Both survive swapchain recreation
    VkSemaphore present_complete[PWVK_NUM_ACQUIRE_SEMAPHORES]; // Signalled by acquire, waited on by sokol's submit
    VkSemaphore render_finished[PWVK_MAX_SWAPCHAIN_IMAGES];    // Signalled by sokol's submit, waited on by present
} PWVKSwapchain;

static struct
{
    int         refcount;
    PWVKContext ctx;
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT messenger;
#endif
} g_pwvk;
// This frame's semaphores. Valid between acquire and present
static inline VkSemaphore pwvk_swapchain_present_complete(const PWVKSwapchain* sc)
{
    return sc->present_complete[sc->frame_index % PWVK_NUM_ACQUIRE_SEMAPHORES];
}
static inline VkSemaphore pwvk_swapchain_render_finished(const PWVKSwapchain* sc)
{
    return sc->render_finished[sc->image_index];
}

PWVK_FUNC void pwvk_swapchain_deinit(PWVKSwapchain* sc);

#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL pwvk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       user)
{
    const char* level = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning";
    cplug_log("[vulkan %s] %s", level, data->pMessage);
    return VK_FALSE;
}
#endif

static bool pwvk_has_extension(const VkExtensionProperties* exts, uint32_t num_exts, const char* name)
{
    for (uint32_t i = 0; i < num_exts; i++)
        if (strcmp(exts[i].extensionName, name) == 0)
            return true;
    return false;
}

#ifndef NDEBUG
static bool pwvk_has_layer(const char* name)
{
    VkLayerProperties layers[64];
    uint32_t          num_layers = PW_ARRLEN(layers);
    vkEnumerateInstanceLayerProperties(&num_layers, layers);
    for (uint32_t i = 0; i < num_layers; i++)
        if (strcmp(layers[i].layerName, name) == 0)
            return true;
    return false;
}
#endif

static const char* PWVK_DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
};

// Feature structs sokol needs. Used both to query a device and to enable them on the chosen one
typedef struct PWVKFeatures
{
    VkPhysicalDeviceFeatures2                  f2;
    VkPhysicalDeviceVulkan12Features           v12;
    VkPhysicalDeviceVulkan13Features           v13;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer;
} PWVKFeatures;

static void pwvk_features_chain(PWVKFeatures* f)
{
    memset(f, 0, sizeof(*f));
    f->f2.sType                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f->f2.pNext                = &f->v12;
    f->v12.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f->v12.pNext               = &f->v13;
    f->v13.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f->v13.pNext               = &f->descriptor_buffer;
    f->descriptor_buffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
}

// Returns NULL if the device can run sokol, otherwise the first thing it's missing
static const char* pwvk_check_device(VkPhysicalDevice pd, uint32_t* queue_family)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < VK_API_VERSION_1_3)
        return "Vulkan 1.3";

    VkExtensionProperties exts[512];
    uint32_t              num_exts = PW_ARRLEN(exts);
    vkEnumerateDeviceExtensionProperties(pd, NULL, &num_exts, exts);
    for (int i = 0; i < PW_ARRLEN(PWVK_DEVICE_EXTENSIONS); i++)
        if (!pwvk_has_extension(exts, num_exts, PWVK_DEVICE_EXTENSIONS[i]))
            return PWVK_DEVICE_EXTENSIONS[i];

    PWVKFeatures f;
    pwvk_features_chain(&f);
    vkGetPhysicalDeviceFeatures2(pd, &f.f2);
    if (!f.descriptor_buffer.descriptorBuffer)
        return "descriptorBuffer";
    if (!f.v12.bufferDeviceAddress)
        return "bufferDeviceAddress";
    if (!f.v13.dynamicRendering)
        return "dynamicRendering";
    if (!f.v13.synchronization2)
        return "synchronization2";
    // xvg's subpixel text. sokol reports it as supported without checking
    if (!f.f2.features.dualSrcBlend)
        return "dualSrcBlend";

    VkQueueFamilyProperties families[32];
    uint32_t                num_families = PW_ARRLEN(families);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &num_families, families);
    for (uint32_t i = 0; i < num_families; i++)
    {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            *queue_family = i;
            return NULL;
        }
    }
    return "a graphics queue";
}

static int pwvk_device_type_score(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 1;
    default:
        return 0;
    }
}

static void pwvk_context_destroy(void)
{
    PWVKContext* ctx = &g_pwvk.ctx;
    // The destroy functions are NULL if loading failed part way
    if (ctx->device && vkDestroyDevice)
    {
        vkDeviceWaitIdle(ctx->device);
        vkDestroyDevice(ctx->device, NULL);
    }
#ifndef NDEBUG
    if (g_pwvk.messenger)
        vkDestroyDebugUtilsMessengerEXT(ctx->instance, g_pwvk.messenger, NULL);
#endif
    if (ctx->instance && vkDestroyInstance)
        vkDestroyInstance(ctx->instance, NULL);
    pwvk_unload();
    memset(&g_pwvk, 0, sizeof(g_pwvk));
}

static bool pwvk_context_create(char* err, size_t err_size)
{
    static const char* REQUIREMENTS = "A Vulkan 1.3 driver with VK_EXT_descriptor_buffer is required. "
                                      "Update Mesa or the NVIDIA driver.";

    PWVKContext* ctx = &g_pwvk.ctx;
    const char*  missing = pwvk_load();
    if (missing)
    {
        snprintf(err, err_size, "Vulkan not found (%s). %s", missing, REQUIREMENTS);
        return false;
    }
    uint32_t loader_version = 0;
    vkEnumerateInstanceVersion(&loader_version);
    if (loader_version < VK_API_VERSION_1_3)
    {
        snprintf(err, err_size, "The Vulkan loader is older than 1.3. %s", REQUIREMENTS);
        pwvk_unload();
        return false;
    }

    // Instance
    VkExtensionProperties inst_exts[128];
    uint32_t              num_inst_exts = PW_ARRLEN(inst_exts);
    vkEnumerateInstanceExtensionProperties(NULL, &num_inst_exts, inst_exts);

    if (!pwvk_has_extension(inst_exts, num_inst_exts, VK_KHR_SURFACE_EXTENSION_NAME))
    {
        snprintf(err, err_size, "No Vulkan driver found, or it has no " VK_KHR_SURFACE_EXTENSION_NAME ". %s", REQUIREMENTS);
        pwvk_unload();
        return false;
    }

    static const char* OPTIONAL_INSTANCE_EXTENSIONS[] = {
        "VK_KHR_xcb_surface",
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
#ifndef NDEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME, // sokol uses it for object labels in debug builds
#endif
    };
    const char* enabled_exts[8];
    uint32_t    num_enabled_exts       = 0;
    enabled_exts[num_enabled_exts++] = VK_KHR_SURFACE_EXTENSION_NAME;
    for (int i = 0; i < PW_ARRLEN(OPTIONAL_INSTANCE_EXTENSIONS); i++)
        if (pwvk_has_extension(inst_exts, num_inst_exts, OPTIONAL_INSTANCE_EXTENSIONS[i]))
            enabled_exts[num_enabled_exts++] = OPTIONAL_INSTANCE_EXTENSIONS[i];

    const char* layers[1];
    uint32_t    num_layers = 0;
#ifndef NDEBUG
    if (pwvk_has_layer("VK_LAYER_KHRONOS_validation"))
        layers[num_layers++] = "VK_LAYER_KHRONOS_validation";
    else
        cplug_log("[vulkan] VK_LAYER_KHRONOS_validation not found. Install vulkan-validationlayers");
#endif

    VkApplicationInfo app_info = {
        .sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo inst_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = num_layers,
        .ppEnabledLayerNames     = layers,
        .enabledExtensionCount   = num_enabled_exts,
        .ppEnabledExtensionNames = enabled_exts,
    };
    VkResult res = vkCreateInstance(&inst_info, NULL, &ctx->instance);
    if (res != VK_SUCCESS)
    {
        snprintf(err, err_size, "vkCreateInstance failed (%d). %s", res, REQUIREMENTS);
        pwvk_unload();
        return false;
    }
    missing = pwvk_load_instance(ctx->instance);
    if (missing)
    {
        snprintf(err, err_size, "The Vulkan driver has no %s. %s", missing, REQUIREMENTS);
        pwvk_context_destroy();
        return false;
    }

#ifndef NDEBUG
    if (vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = pwvk_debug_callback,
        };
        vkCreateDebugUtilsMessengerEXT(ctx->instance, &messenger_info, NULL, &g_pwvk.messenger);
    }
#endif

    // Physical device
    VkPhysicalDevice devices[16];
    uint32_t         num_devices = PW_ARRLEN(devices);
    vkEnumeratePhysicalDevices(ctx->instance, &num_devices, devices);

    // The host may already be running on the iGPU of a hybrid laptop, which is fine
    int         best_score   = 0;
    const char* last_missing = NULL;
    char        last_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "no Vulkan device";
    for (uint32_t i = 0; i < num_devices; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        uint32_t queue_family = 0;
        missing               = pwvk_check_device(devices[i], &queue_family);
        if (missing)
        {
            cplug_log("[vulkan] Skipping %s: missing %s", props.deviceName, missing);
            last_missing = missing;
            memcpy(last_name, props.deviceName, sizeof(last_name));
            continue;
        }
        int score = pwvk_device_type_score(props.deviceType) + 1;
        if (score > best_score)
        {
            best_score              = score;
            ctx->physical_device    = devices[i];
            ctx->queue_family_index = queue_family;
        }
    }
    if (!ctx->physical_device)
    {
        if (last_missing)
            snprintf(err, err_size, "%s has no %s. %s", last_name, last_missing, REQUIREMENTS);
        else
            snprintf(err, err_size, "No Vulkan device found. %s", REQUIREMENTS);
        pwvk_context_destroy();
        return false;
    }

    // Device
    PWVKFeatures f;
    pwvk_features_chain(&f);
    f.f2.features.dualSrcBlend           = VK_TRUE;
    f.v12.bufferDeviceAddress            = VK_TRUE;
    f.v13.dynamicRendering               = VK_TRUE;
    f.v13.synchronization2               = VK_TRUE;
    f.descriptor_buffer.descriptorBuffer = VK_TRUE;

    const float             queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info     = {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx->queue_family_index,
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &f.f2,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_info,
        .enabledExtensionCount   = PW_ARRLEN(PWVK_DEVICE_EXTENSIONS),
        .ppEnabledExtensionNames = PWVK_DEVICE_EXTENSIONS,
    };
    res = vkCreateDevice(ctx->physical_device, &device_info, NULL, &ctx->device);
    if (res != VK_SUCCESS)
    {
        snprintf(err, err_size, "vkCreateDevice failed (%d). %s", res, REQUIREMENTS);
        pwvk_context_destroy();
        return false;
    }
    // Only one device per process, so device level function pointers can be globals
    missing = pwvk_load_device(ctx->device);
    if (missing)
    {
        snprintf(err, err_size, "The Vulkan driver has no %s. %s", missing, REQUIREMENTS);
        pwvk_context_destroy();
        return false;
    }
    vkGetDeviceQueue(ctx->device, ctx->queue_family_index, 0, &ctx->queue);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &props);
    cplug_log("[vulkan] Using %s", props.deviceName);
    return true;
}

PWVK_FUNC const PWVKContext* pwvk_context_retain(char* err, size_t err_size)
{
    if (g_pwvk.refcount == 0 && !pwvk_context_create(err, err_size))
        return NULL;
    g_pwvk.refcount++;
    return &g_pwvk.ctx;
}

PWVK_FUNC void pwvk_context_release(void)
{
    CPLUG_LOG_ASSERT_RETURN(g_pwvk.refcount > 0, );
    g_pwvk.refcount--;
    if (g_pwvk.refcount == 0)
        pwvk_context_destroy();
}

PWVK_FUNC bool pwvk_swapchain_init(PWVKSwapchain* sc, VkSurfaceKHR surface)
{
    const PWVKContext* ctx = &g_pwvk.ctx;
    CPLUG_LOG_ASSERT(g_pwvk.refcount > 0);
    memset(sc, 0, sizeof(*sc));
    sc->surface        = surface;
    sc->needs_recreate = true;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx->physical_device, ctx->queue_family_index, surface, &supported);
    if (!supported)
    {
        cplug_log("[vulkan] The graphics queue can't present to this surface");
        pwvk_swapchain_deinit(sc);
        return false;
    }

    VkSurfaceFormatKHR formats[64];
    uint32_t           num_formats = PW_ARRLEN(formats);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, surface, &num_formats, formats);
    bool has_format = false;
    for (uint32_t i = 0; i < num_formats; i++)
        if (formats[i].format == PWVK_SWAPCHAIN_FORMAT && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            has_format = true;
    if (!has_format)
    {
        cplug_log("[vulkan] The surface doesn't support VK_FORMAT_B8G8R8A8_UNORM");
        pwvk_swapchain_deinit(sc);
        return false;
    }

    VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (int i = 0; i < PWVK_NUM_ACQUIRE_SEMAPHORES; i++)
        vkCreateSemaphore(ctx->device, &sem_info, NULL, &sc->present_complete[i]);
    for (int i = 0; i < PWVK_MAX_SWAPCHAIN_IMAGES; i++)
        vkCreateSemaphore(ctx->device, &sem_info, NULL, &sc->render_finished[i]);
    return true;
}

static void pwvk_swapchain_destroy_views(PWVKSwapchain* sc)
{
    for (uint32_t i = 0; i < sc->num_images; i++)
        vkDestroyImageView(g_pwvk.ctx.device, sc->views[i], NULL);
    memset(sc->views, 0, sizeof(sc->views));
    memset(sc->images, 0, sizeof(sc->images));
    sc->num_images = 0;
}

PWVK_FUNC void pwvk_swapchain_deinit(PWVKSwapchain* sc)
{
    const PWVKContext* ctx = &g_pwvk.ctx;
    // sokol may still reference the images in flight
    vkQueueWaitIdle(ctx->queue);
    pwvk_swapchain_destroy_views(sc);
    if (sc->swapchain)
        vkDestroySwapchainKHR(ctx->device, sc->swapchain, NULL);
    if (sc->surface)
        vkDestroySurfaceKHR(ctx->instance, sc->surface, NULL);
    // vkDestroySemaphore ignores VK_NULL_HANDLE
    for (int i = 0; i < PWVK_NUM_ACQUIRE_SEMAPHORES; i++)
        vkDestroySemaphore(ctx->device, sc->present_complete[i], NULL);
    for (int i = 0; i < PWVK_MAX_SWAPCHAIN_IMAGES; i++)
        vkDestroySemaphore(ctx->device, sc->render_finished[i], NULL);
    memset(sc, 0, sizeof(*sc));
}

static VkPresentModeKHR pwvk_choose_present_mode(VkSurfaceKHR surface)
{
    // Avoid FIFO. Under XWayland, FIFO can block the main thread indefinitely while the window is hidden.
    // In a host, N open plugin GUIs with FIFO would block the host's main thread N times per vblank.
    // Frames are paced by host timers / timerfd instead
    VkPresentModeKHR modes[16];
    uint32_t         num_modes = PW_ARRLEN(modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_pwvk.ctx.physical_device, surface, &num_modes, modes);

    VkPresentModeKHR best = VK_PRESENT_MODE_FIFO_KHR; // Always supported
    for (uint32_t i = 0; i < num_modes; i++)
    {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            return modes[i];
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
            best = modes[i];
    }
    return best;
}

static bool pwvk_swapchain_recreate(PWVKSwapchain* sc, uint32_t width, uint32_t height)
{
    const PWVKContext* ctx = &g_pwvk.ctx;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, sc->surface, &caps);

    // On X11 the extent must equal the window size, which is what the caller should be passing anyway
    VkExtent2D extent = {width, height};
    if (extent.width < caps.minImageExtent.width)
        extent.width = caps.minImageExtent.width;
    if (extent.width > caps.maxImageExtent.width)
        extent.width = caps.maxImageExtent.width;
    if (extent.height < caps.minImageExtent.height)
        extent.height = caps.minImageExtent.height;
    if (extent.height > caps.maxImageExtent.height)
        extent.height = caps.maxImageExtent.height;
    if (extent.width == 0 || extent.height == 0)
        return false; // Minimised
    if (extent.width != width || extent.height != height)
        cplug_log("[vulkan] Swapchain size %ux%u clamped to %ux%u", width, height, extent.width, extent.height);

    uint32_t num_images = caps.minImageCount + 1;
    if (caps.maxImageCount && num_images > caps.maxImageCount)
        num_images = caps.maxImageCount;
    if (num_images > PWVK_MAX_SWAPCHAIN_IMAGES)
        num_images = PWVK_MAX_SWAPCHAIN_IMAGES;

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & composite_alpha))
        composite_alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);

    // Destroy old image views only once sokol is done with them
    vkQueueWaitIdle(ctx->queue);
    pwvk_swapchain_destroy_views(sc);

    VkSwapchainKHR           old_swapchain = sc->swapchain;
    VkSwapchainCreateInfoKHR info          = {
                 .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                 .surface          = sc->surface,
                 .minImageCount    = num_images,
                 .imageFormat      = PWVK_SWAPCHAIN_FORMAT,
                 .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                 .imageExtent      = extent,
                 .imageArrayLayers = 1,
                 .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                 .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                 .preTransform     = caps.currentTransform,
                 .compositeAlpha   = composite_alpha,
                 .presentMode      = pwvk_choose_present_mode(sc->surface),
                 .clipped          = VK_TRUE,
                 .oldSwapchain     = old_swapchain,
    };
    VkResult res  = vkCreateSwapchainKHR(ctx->device, &info, NULL, &sc->swapchain);
    if (old_swapchain)
        vkDestroySwapchainKHR(ctx->device, old_swapchain, NULL);
    if (res != VK_SUCCESS)
    {
        cplug_log("[vulkan] vkCreateSwapchainKHR failed (%d)", res);
        sc->swapchain = VK_NULL_HANDLE;
        return false;
    }

    sc->num_images = PWVK_MAX_SWAPCHAIN_IMAGES;
    res            = vkGetSwapchainImagesKHR(ctx->device, sc->swapchain, &sc->num_images, sc->images);
    if (res != VK_SUCCESS)
    {
        // The driver made more images than PWVK_MAX_SWAPCHAIN_IMAGES
        cplug_log("[vulkan] vkGetSwapchainImagesKHR failed (%d)", res);
        sc->num_images = 0;
        return false;
    }
    for (uint32_t i = 0; i < sc->num_images; i++)
    {
        VkImageViewCreateInfo view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = sc->images[i],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = PWVK_SWAPCHAIN_FORMAT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCreateImageView(ctx->device, &view_info, NULL, &sc->views[i]);
    }

    // Compare against the unclamped size, or a clamped swapchain would be recreated every frame
    sc->requested_extent = (VkExtent2D){width, height};
    sc->needs_recreate   = false;
    return true;
}

PWVK_FUNC bool pwvk_swapchain_acquire(PWVKSwapchain* sc, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return false;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        bool size_changed = sc->requested_extent.width != width || sc->requested_extent.height != height;
        if (sc->needs_recreate || size_changed || !sc->swapchain)
            if (!pwvk_swapchain_recreate(sc, width, height))
                return false;

        // Never wait. With FIFO, or a compositor holding every image (eg. hidden window), this would block the main
        // thread indefinitely. Skip the frame instead
        VkResult res = vkAcquireNextImageKHR(
            g_pwvk.ctx.device,
            sc->swapchain,
            0,
            sc->present_complete[(sc->frame_index + 1) % PWVK_NUM_ACQUIRE_SEMAPHORES],
            VK_NULL_HANDLE,
            &sc->image_index);
        if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR)
        {
            sc->frame_index++;
            // The image was acquired and the semaphore will be signalled, so it must be used. Recreate next frame
            if (res == VK_SUBOPTIMAL_KHR)
                sc->needs_recreate = true;
            return true;
        }
        // No image free yet. The semaphore isn't signalled, so its slot can be reused next frame
        if (res == VK_NOT_READY || res == VK_TIMEOUT)
            return false;
        if (res != VK_ERROR_OUT_OF_DATE_KHR)
        {
            cplug_log("[vulkan] vkAcquireNextImageKHR failed (%d)", res);
            return false;
        }
        sc->needs_recreate = true;
    }
    return false;
}

PWVK_FUNC void pwvk_swapchain_present(PWVKSwapchain* sc)
{
    VkPresentInfoKHR info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &sc->render_finished[sc->image_index],
        .swapchainCount     = 1,
        .pSwapchains        = &sc->swapchain,
        .pImageIndices      = &sc->image_index,
    };
    VkResult res = vkQueuePresentKHR(g_pwvk.ctx.queue, &info);
    if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
        sc->needs_recreate = true;
    else if (res != VK_SUCCESS)
        cplug_log("[vulkan] vkQueuePresentKHR failed (%d)", res);
}
#endif // PW_VULKAN

void pw_get_keyboard_focus(void* pw) {}
void pw_release_keyboard_focus(void* pw) {}

float pw_get_backing_scale_factor(void* pw) { return 1.0f; }

void pw_set_clipboard_text(void* pw, const char* text) {}
bool pw_get_clipboard_text(void* pw, char** out, size_t* len) { return false; }
void pw_free_clipboard_text(char* ptr) {}

void pw_set_mouse_cursor(void* pw, enum PWCursorType type) {}
void pw_beep() {}

bool pw_choose_file(const struct PWChooseFileArgs* args) { return false; }

#ifdef PW_VULKAN
// No window yet, so no surface/swapchain. Forward these to g_pwvk.ctx and the window's PWVKSwapchain once there is
void*    pw_get_vulkan_instance(void* pw) { return NULL; }
void*    pw_get_vulkan_physical_device(void* pw) { return NULL; }
void*    pw_get_vulkan_device(void* pw) { return NULL; }
void*    pw_get_vulkan_queue(void* pw) { return NULL; }
uint32_t pw_get_vulkan_queue_family_index(void* pw) { return 0; }
void*    pw_get_vulkan_render_image(void* pw) { return NULL; }
void*    pw_get_vulkan_render_view(void* pw) { return NULL; }
void*    pw_get_vulkan_present_complete_semaphore(void* pw) { return NULL; }
void*    pw_get_vulkan_render_finished_semaphore(void* pw) { return NULL; }
#endif

// No GUI yet: hosts get NULL from cplug_createGUI
void* cplug_createGUI(CplugHostContext* host_ctx, void* userPlugin) { return NULL; }
void  cplug_destroyGUI(void* userGUI) {}
void  cplug_setParent(void* userGUI, void* newParent) {}
void  cplug_setVisible(void* userGUI, bool visible) {}
void  cplug_setScaleFactor(void* userGUI, float scale) {}
void  cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height) {}
void  cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height) {}
bool  cplug_setSize(void* userGUI, uint32_t width, uint32_t height) { return false; }
