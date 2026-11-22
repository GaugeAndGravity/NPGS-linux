#include "Core.h"

#include <cstdlib>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cstdio>
#include <exception>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>

#include "Engine/Core/Runtime/Graphics/Vulkan/ExtFunctionsImpl.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/Utils.h"

_NPGS_BEGIN
_RUNTIME_BEGIN
_GRAPHICS_BEGIN

namespace
{
    template <typename Operation>
    void CleanupStep(vk::Result& FirstError, const char* Name, Operation&& Cleanup) noexcept
    {
        try
        {
            Cleanup();
        }
        catch (const vk::SystemError& Error)
        {
            if (FirstError == vk::Result::eSuccess)
            {
                FirstError = static_cast<vk::Result>(Error.code().value());
            }
            std::fprintf(stderr, "Vulkan cleanup (%s): %s\n", Name, Error.what());
        }
        catch (const std::exception& Error)
        {
            if (FirstError == vk::Result::eSuccess)
            {
                FirstError = vk::Result::eErrorUnknown;
            }
            std::fprintf(stderr, "Vulkan cleanup (%s): %s\n", Name, Error.what());
        }
        catch (...)
        {
            if (FirstError == vk::Result::eSuccess)
            {
                FirstError = vk::Result::eErrorUnknown;
            }
            std::fprintf(stderr, "Vulkan cleanup (%s): unknown exception\n", Name);
        }
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL ValidationDebugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT Severity,
        vk::DebugUtilsMessageTypeFlagsEXT,
        const vk::DebugUtilsMessengerCallbackDataEXT* Data,
        void* UserData) noexcept
    {
        const bool bError = static_cast<bool>(Severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
        const char* SeverityName = bError ? "ERROR" :
            (Severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) ? "WARNING" :
            (Severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) ? "INFO" : "VERBOSE";
        const char* MessageId = Data && Data->pMessageIdName ? Data->pMessageIdName : "<no message ID>";
        const char* Message = Data && Data->pMessage ? Data->pMessage : "<no message>";
        const int MessageNumber = Data ? static_cast<int>(Data->messageIdNumber) : 0;

        // Avoid logger initialization/exception dependencies inside the C callback.
        // Print the complete message (including multiline VUID details) and flush
        // before the diagnostic-only exit, without unwinding through Vulkan.
        std::fprintf(stderr, "Vulkan validation [%s] pMessageIdName=%s messageIdNumber=%d\n%s\n",
                     SeverityName, MessageId, MessageNumber, Message);
        std::fflush(stderr);
        if (bError && UserData && *static_cast<const bool*>(UserData))
        {
            // Stops synchronous validation errors only. GPU-assisted validation
            // and execution-time faults may arrive after GPU work was submitted.
            std::_Exit(3);
        }
        return vk::False;
    }

    vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo(bool* FailFast)
    {
        const auto Severities = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        const auto Types = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        return vk::DebugUtilsMessengerCreateInfoEXT({}, Severities, Types, ValidationDebugCallback, FailFast);
    }

    // Actual renderer/SPIR-V requirements: dynamic rendering, barrier2, the
    // default anisotropic sampler, and formatless writes in the compute shaders.
    bool SupportsRendererFeatures(vk::PhysicalDevice Device)
    {
        vk::PhysicalDeviceVulkan13Features Features13;
        vk::PhysicalDeviceFeatures2 Features;
        Features.setPNext(&Features13);
        Device.getFeatures2(&Features);
        return Features13.dynamicRendering && Features13.synchronization2 &&
               Features.features.samplerAnisotropy && Features.features.shaderStorageImageWriteWithoutFormat;
    }

    vk::Extent2D ResolveSwapchainExtent(const vk::SurfaceCapabilitiesKHR& Capabilities,
                                        vk::Extent2D FramebufferSize)
    {
        if (FramebufferSize.width == 0 || FramebufferSize.height == 0 ||
            Capabilities.currentExtent.width == 0 || Capabilities.currentExtent.height == 0)
        {
            return {};
        }
        if (Capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        {
            return Capabilities.currentExtent;
        }
        return {
            std::clamp(FramebufferSize.width, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width),
            std::clamp(FramebufferSize.height, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height)
        };
    }
}

FVulkanCore::FVulkanCore()
    :
    _GraphicsQueueFamilyIndex(vk::QueueFamilyIgnored),
    _PresentQueueFamilyIndex(vk::QueueFamilyIgnored),
    _ComputeQueueFamilyIndex(vk::QueueFamilyIgnored),
    _CurrentImageIndex(std::numeric_limits<std::uint32_t>::max()),
    _ApiVersion(VK_API_VERSION_1_4)
{
}

FVulkanCore::~FVulkanCore()
{
    if (!_bShutdown)
    {
        Shutdown();
    }
}

vk::Result FVulkanCore::Shutdown() noexcept
{
    if (_bShutdown || _bShuttingDown)
    {
        return _ShutdownResult;
    }
    _bShuttingDown = true;
    CleanupStep(_ShutdownResult, "wait idle", [this]()
    {
        _ShutdownResult = WaitIdle();
    });
    const vk::Result SurfaceResult = ReleaseSurfaceInternal(false);
    if (_ShutdownResult == vk::Result::eSuccess)
    {
        _ShutdownResult = SurfaceResult;
    }

    // Keep the device and allocator alive until every registered GPU owner has
    // emptied its containers. Moving the callbacks also permits deregistration
    // during cleanup without invalidating this traversal.
    auto DestroyCallbacks = std::move(_DestroyDeviceCallbacks);
    for (auto& [Name, Callback] : DestroyCallbacks)
    {
        CleanupStep(_ShutdownResult, Name.c_str(), Callback);
    }
    DestroyCallbacks.clear();
    _CreateSwapchainCallbacks.clear();
    _DestroySwapchainCallbacks.clear();
    _CreateDeviceCallbacks.clear();
    _DestroyDeviceCallbacks.clear();
    _InstanceLayers.clear();
    _InstanceExtensions.clear();
    _DeviceExtensions.clear();
    _AvailableSurfaceFormats.clear();
    _SwapchainImages.clear();
    _SwapchainExtent = vk::Extent2D();
    _FramebufferSize = vk::Extent2D();
    _CurrentImageIndex = std::numeric_limits<std::uint32_t>::max();

    if (_VmaAllocator)
    {
        const auto Allocator = std::exchange(_VmaAllocator, nullptr);
        CleanupStep(_ShutdownResult, "VMA allocator", [Allocator]() { vmaDestroyAllocator(Allocator); });
    }
    if (_Device)
    {
        const auto Device = std::exchange(_Device, vk::Device());
        CleanupStep(_ShutdownResult, "device", [Device]() { Device.destroy(); });
    }
    _GraphicsQueue = vk::Queue();
    _PresentQueue = vk::Queue();
    _ComputeQueue = vk::Queue();
    _PhysicalDevice = vk::PhysicalDevice();
    _AvailablePhysicalDevices.clear();
    _PhysicalDeviceProperties = vk::PhysicalDeviceProperties();
    _PhysicalDeviceMemoryProperties = vk::PhysicalDeviceMemoryProperties();
    _GraphicsQueueFamilyIndex = vk::QueueFamilyIgnored;
    _PresentQueueFamilyIndex = vk::QueueFamilyIgnored;
    _ComputeQueueFamilyIndex = vk::QueueFamilyIgnored;
    if (_DebugUtilsMessenger && _Instance)
    {
        const auto Messenger = std::exchange(_DebugUtilsMessenger, vk::DebugUtilsMessengerEXT());
        CleanupStep(_ShutdownResult, "debug messenger", [this, Messenger]()
        {
            _Instance.destroyDebugUtilsMessengerEXT(Messenger);
        });
    }
    _DebugUtilsMessenger = vk::DebugUtilsMessengerEXT();
    if (_Instance)
    {
        const auto Instance = std::exchange(_Instance, vk::Instance());
        CleanupStep(_ShutdownResult, "instance", [Instance]() { Instance.destroy(); });
    }
    kVkCreateDebugUtilsMessengerExt = nullptr;
    kVkDestroyDebugUtilsMessengerExt = nullptr;
    _bValidationFailFast = false;
    _bShuttingDown = false;
    _bShutdown = true;
    return _ShutdownResult;
}

vk::Result FVulkanCore::ReleaseSurface() noexcept
{
    if (_bShutdown || _bShuttingDown)
    {
        return _ShutdownResult;
    }
    return ReleaseSurfaceInternal(true);
}

vk::Result FVulkanCore::ReleaseSurfaceInternal(bool bWaitForDevice) noexcept
{
    if (!_Surface && !_Swapchain && _SwapchainImageViews.empty() && !_SwapchainCreateInfo.oldSwapchain)
    {
        return vk::Result::eSuccess;
    }

    // Preserve device/VMA: assets constructed after the core may still need
    // them during static destruction, but WSI must not outlive GLFW's display.
    vk::Result Result = vk::Result::eSuccess;
    if (bWaitForDevice)
    {
        CleanupStep(Result, "surface wait idle", [&]() { Result = WaitIdle(); });
    }
    if (_Device)
    {
        if (_Swapchain)
        {
            for (auto& Callback : _DestroySwapchainCallbacks)
            {
                CleanupStep(Result, Callback.first.c_str(), Callback.second);
            }
        }
        for (const auto View : _SwapchainImageViews)
        {
            if (View)
            {
                CleanupStep(Result, "swapchain image view", [&]() { _Device.destroyImageView(View); });
            }
        }
        if (_SwapchainCreateInfo.oldSwapchain && _SwapchainCreateInfo.oldSwapchain != _Swapchain)
        {
            CleanupStep(Result, "old swapchain", [&]() { _Device.destroySwapchainKHR(_SwapchainCreateInfo.oldSwapchain); });
        }
        if (_Swapchain)
        {
            CleanupStep(Result, "swapchain", [&]() { _Device.destroySwapchainKHR(_Swapchain); });
        }
    }
    _SwapchainImageViews.clear();
    _SwapchainImages.clear();
    _AvailableSurfaceFormats.clear();
    _Swapchain = vk::SwapchainKHR();
    _SwapchainCreateInfo = vk::SwapchainCreateInfoKHR();
    _SwapchainExtent = vk::Extent2D();
    _FramebufferSize = vk::Extent2D();
    _CurrentImageIndex = std::numeric_limits<std::uint32_t>::max();

    if (_Surface && _Instance)
    {
        CleanupStep(Result, "surface", [&]() { _Instance.destroySurfaceKHR(_Surface); });
    }
    _Surface = vk::SurfaceKHR();
    // Device loss does not prevent destroying its WSI objects; still report it.
    return Result;
}

vk::Result FVulkanCore::CheckInstanceLayers()
{
    std::vector<vk::LayerProperties> AvailableLayers;
    try
    {
        AvailableLayers = vk::enumerateInstanceLayerProperties();
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to enumerate instance layers: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    for (const char* RequestedLayer : _InstanceLayers)
    {
        bool bLayerFound = false;
        for (const auto& AvailableLayer : AvailableLayers)
        {
            if (Util::Equal(RequestedLayer, AvailableLayer.layerName))
            {
                bLayerFound = true;
                break;
            }
        }
        if (!bLayerFound)
        {
            NpgsCoreError("Requested Vulkan instance layer is unavailable: {}", RequestedLayer);
            return vk::Result::eErrorLayerNotPresent;
        }
    }

    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CheckInstanceExtensions(const std::string& Layer)
{
    std::vector<vk::ExtensionProperties> AvailableExtensions;
    try
    {
        AvailableExtensions = Layer.empty() ? vk::enumerateInstanceExtensionProperties(nullptr) :
                                             vk::enumerateInstanceExtensionProperties(Layer);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to enumerate instance extensions: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    for (const char* RequestedExtension : _InstanceExtensions)
    {
        bool bExtensionFound = false;
        for (const auto& AvailableExtension : AvailableExtensions)
        {
            if (Util::Equal(RequestedExtension, AvailableExtension.extensionName))
            {
                bExtensionFound = true;
                break;
            }
        }
        if (!bExtensionFound)
        {
            NpgsCoreError("Required Vulkan instance extension is unavailable: {}", RequestedExtension);
            return vk::Result::eErrorExtensionNotPresent;
        }
    }

    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CheckDeviceExtensions()
{
    std::vector<vk::ExtensionProperties> AvailableExtensions;
    try
    {
        AvailableExtensions = _PhysicalDevice.enumerateDeviceExtensionProperties();
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to enumerate device extensions: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    for (const char* RequestedExtension : _DeviceExtensions)
    {
        bool bExtensionFound = false;
        for (const auto& AvailableExtension : AvailableExtensions)
        {
            if (Util::Equal(RequestedExtension, AvailableExtension.extensionName))
            {
                bExtensionFound = true;
                break;
            }
        }
        if (!bExtensionFound)
        {
            NpgsCoreError("Required Vulkan device extension is unavailable: {}", RequestedExtension);
            return vk::Result::eErrorExtensionNotPresent;
        }
    }

    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CreateInstance(vk::InstanceCreateFlags Flags)
{
    if (_bShutdown || _bShuttingDown)
    {
        return vk::Result::eErrorInitializationFailed;
    }
    if (vk::Result Result = UseLatestApiVersion(); Result != vk::Result::eSuccess)
    {
        return Result;
    }
    const bool bValidationRequested =
        std::any_of(_InstanceLayers.begin(), _InstanceLayers.end(), [](const char* Layer)
        {
            return std::strcmp(Layer, "VK_LAYER_KHRONOS_validation") == 0;
        });
    if (bValidationRequested)
    {
        AddInstanceExtension(vk::EXTDebugUtilsExtensionName);
    }
    const char* FailFast = std::getenv("NPGS_VALIDATION_FAIL_FAST");
    _bValidationFailFast = bValidationRequested && FailFast && std::strcmp(FailFast, "1") == 0;
    if (_bValidationFailFast)
    {
        NpgsCoreWarn("Diagnostic validation fail-fast enabled: first ERROR exits with code 3. "
                     "This does not guarantee GPU-assisted validation/execution-time safety.");
    }
    if (vk::Result Result = CheckInstanceLayers(); Result != vk::Result::eSuccess)
    {
        return Result;
    }
    if (vk::Result Result = CheckInstanceExtensions({}); Result != vk::Result::eSuccess)
    {
        return Result;
    }

    vk::ApplicationInfo ApplicationInfo("Von-Neumann in Galaxy Simulator", VK_MAKE_VERSION(1, 0, 0),
                                        "No Engine", VK_MAKE_VERSION(1, 0, 0), _ApiVersion);
    vk::InstanceCreateInfo InstanceCreateInfo(Flags, &ApplicationInfo, _InstanceLayers, _InstanceExtensions);
    const auto DebugCreateInfo = MakeDebugMessengerCreateInfo(&_bValidationFailFast);
    if (bValidationRequested)
    {
        InstanceCreateInfo.setPNext(&DebugCreateInfo);
    }

    try
    {
        _Instance = vk::createInstance(InstanceCreateInfo);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to create Vulkan instance: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    if (bValidationRequested)
    {
        if (vk::Result Result = CreateDebugMessenger(); Result != vk::Result::eSuccess)
        {
            return Result;
        }
    }

    NpgsCoreInfo("Vulkan instance created successfully.");
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CreateDevice(std::uint32_t PhysicalDeviceIndex, vk::DeviceCreateFlags Flags)
{
    if (_bShutdown || _bShuttingDown || !_Instance || !_Surface)
    {
        NpgsCoreError("CreateDevice requires an instance and a GLFW-created surface.");
        return vk::Result::eErrorInitializationFailed;
    }
    AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (vk::Result Result = EnumeratePhysicalDevices(); Result != vk::Result::eSuccess)
    {
        return Result;
    }
    if (vk::Result Result = DeterminePhysicalDevice(PhysicalDeviceIndex, true, true); Result != vk::Result::eSuccess)
    {
        return Result;
    }

    float QueuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> DeviceQueueCreateInfos;

    if (_GraphicsQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        DeviceQueueCreateInfos.emplace_back(vk::DeviceQueueCreateFlags{}, _GraphicsQueueFamilyIndex, 1, &QueuePriority);
    }
    if (_PresentQueueFamilyIndex != vk::QueueFamilyIgnored &&
        _PresentQueueFamilyIndex != _GraphicsQueueFamilyIndex)
    {
        DeviceQueueCreateInfos.emplace_back(vk::DeviceQueueCreateFlags{}, _PresentQueueFamilyIndex, 1, &QueuePriority);
    }
    if (_ComputeQueueFamilyIndex != vk::QueueFamilyIgnored &&
        _ComputeQueueFamilyIndex != _GraphicsQueueFamilyIndex &&
        _ComputeQueueFamilyIndex != _PresentQueueFamilyIndex)
    {
        DeviceQueueCreateInfos.emplace_back(vk::DeviceQueueCreateFlags{}, _ComputeQueueFamilyIndex, 1, &QueuePriority);
    }

    vk::PhysicalDeviceFeatures PhysicalDeviceFeatures;
    PhysicalDeviceFeatures.setSamplerAnisotropy(vk::True).setShaderStorageImageWriteWithoutFormat(vk::True);
    vk::PhysicalDeviceVulkan13Features Features13;
    Features13.setDynamicRendering(vk::True).setSynchronization2(vk::True);
    vk::DeviceCreateInfo DeviceCreateInfo(Flags, DeviceQueueCreateInfos, {}, _DeviceExtensions,
                                         &PhysicalDeviceFeatures, &Features13);

    try
    {
        _Device = _PhysicalDevice.createDevice(DeviceCreateInfo);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to create logical device: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    // 由于创建的队列族指定了每个队列族只有一个队列，所以这里直接获取第一个队列
    if (_GraphicsQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        _GraphicsQueue = _Device.getQueue(_GraphicsQueueFamilyIndex, 0);
    }
    if (_PresentQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        _PresentQueue = _Device.getQueue(_PresentQueueFamilyIndex, 0);
    }
    if (_ComputeQueueFamilyIndex != vk::QueueFamilyIgnored)
    {
        _ComputeQueue = _Device.getQueue(_ComputeQueueFamilyIndex, 0);
    }

    _PhysicalDeviceProperties       = _PhysicalDevice.getProperties();
    _PhysicalDeviceMemoryProperties = _PhysicalDevice.getMemoryProperties();
    NpgsCoreInfo("Logical device created successfully.");
    NpgsCoreInfo("Renderer: {}", _PhysicalDeviceProperties.deviceName.data());

    if (vk::Result Result = InitializeVmaAllocator(); Result != vk::Result::eSuccess)
    {
        return Result;
    }
    for (auto& Callback : _CreateDeviceCallbacks)
    {
        Callback.second();
    }

    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::RecreateDevice(std::uint32_t PhysicalDeviceIndex, vk::DeviceCreateFlags Flags)
{
    if (vk::Result Result = WaitIdle(); Result != vk::Result::eSuccess)
    {
        return Result;
    }

    if (_Swapchain)
    {
        for (auto& Callback : _DestroySwapchainCallbacks)
        {
            Callback.second();
        }
        for (auto& ImageView : _SwapchainImageViews)
        {
            if (ImageView)
            {
                _Device.destroyImageView(ImageView);
            }
        }
        _SwapchainImageViews.clear();

        _Device.destroySwapchainKHR(_Swapchain);
        _Swapchain           = vk::SwapchainKHR();
        _SwapchainCreateInfo = vk::SwapchainCreateInfoKHR();
    }

    for (auto& Callback : _DestroyDeviceCallbacks)
    {
        Callback.second();
    }
    if (_Device)
    {
        if (_VmaAllocator)
        {
            vmaDestroyAllocator(_VmaAllocator);
            _VmaAllocator = nullptr;
        }
        _Device.destroy();
        _Device = vk::Device();
    }
    _AvailableSurfaceFormats.clear();

    return CreateDevice(PhysicalDeviceIndex, Flags);
}

vk::Result FVulkanCore::SetSurfaceFormat(const vk::SurfaceFormatKHR& SurfaceFormat)
{
    bool bFormatAvailable = false;
    if (SurfaceFormat.format == vk::Format::eUndefined)
    {
        for (const auto& AvailableSurfaceFormat : _AvailableSurfaceFormats)
        {
            if (AvailableSurfaceFormat.colorSpace == SurfaceFormat.colorSpace)
            {
                _SwapchainCreateInfo.setImageFormat(AvailableSurfaceFormat.format)
                                    .setImageColorSpace(AvailableSurfaceFormat.colorSpace);
                bFormatAvailable = true;
                break;
            }
        }
    }
    else
    {
        for (const auto& AvailableSurfaceFormat : _AvailableSurfaceFormats)
        {
            if (AvailableSurfaceFormat.format     == SurfaceFormat.format &&
                AvailableSurfaceFormat.colorSpace == SurfaceFormat.colorSpace)
            {
                _SwapchainCreateInfo.setImageFormat(AvailableSurfaceFormat.format)
                                    .setImageColorSpace(AvailableSurfaceFormat.colorSpace);
                bFormatAvailable = true;
                break;
            }
        }
    }

    if (!bFormatAvailable)
    {
        return vk::Result::eErrorFormatNotSupported;
    }

    if (_Swapchain)
    {
        return RecreateSwapchain();
    }

    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CreateSwapchain(vk::Extent2D Extent, bool bLimitFps, vk::SwapchainCreateFlagsKHR Flags)
{
    SetFramebufferSize(Extent);
    // Swapchain 需要的信息：
    // 1.基本 Surface 能力（Swapchain 中图像的最小/最大数量、宽度和高度）
    vk::SurfaceCapabilitiesKHR SurfaceCapabilities;
    try
    {
        SurfaceCapabilities = _PhysicalDevice.getSurfaceCapabilitiesKHR(_Surface);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to get surface capabilities: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    vk::Extent2D SwapchainExtent = ResolveSwapchainExtent(SurfaceCapabilities, _FramebufferSize);
    if (SwapchainExtent.width == 0 || SwapchainExtent.height == 0)
    {
        return vk::Result::eNotReady;
    }
    _SwapchainExtent = SwapchainExtent;

    std::uint32_t MinImageCount = SurfaceCapabilities.minImageCount + 1;
    if (SurfaceCapabilities.maxImageCount != 0)
    {
        MinImageCount = std::min(MinImageCount, SurfaceCapabilities.maxImageCount);
    }
    _SwapchainCreateInfo.setFlags(Flags)
                        .setSurface(_Surface)
                        .setMinImageCount(MinImageCount)
                        .setImageExtent(SwapchainExtent)
                        .setImageArrayLayers(1)
                        .setImageSharingMode(vk::SharingMode::eExclusive)
                        .setPreTransform(SurfaceCapabilities.currentTransform)
                        .setClipped(vk::True);

    // 设置图像格式
    if (SurfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit) // 优先使用继承模式
    {
        _SwapchainCreateInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eInherit);
    }
    else
    {
        // 找不到继承模式，随便挑一个凑合用的
        static const vk::CompositeAlphaFlagBitsKHR kCompositeAlphaFlags[]
        {
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
            vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
        };

        for (auto CompositeAlphaFlag : kCompositeAlphaFlags)
        {
            if (SurfaceCapabilities.supportedCompositeAlpha & CompositeAlphaFlag)
            {
                _SwapchainCreateInfo.setCompositeAlpha(CompositeAlphaFlag);
                break;
            }
        }
    }

    // The current renderer copies its final image into the swapchain.
    if (!(SurfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment))
    {
        return vk::Result::eErrorFeatureNotPresent;
    }
    _SwapchainCreateInfo.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
    if (SurfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
    {
        _SwapchainCreateInfo.setImageUsage(_SwapchainCreateInfo.imageUsage | vk::ImageUsageFlagBits::eTransferSrc);
    }
    if (SurfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)
    {
        _SwapchainCreateInfo.setImageUsage(_SwapchainCreateInfo.imageUsage | vk::ImageUsageFlagBits::eTransferDst);
    }
    else
    {
        NpgsCoreError("Failed to get supported usage flags.");
        return vk::Result::eErrorFeatureNotPresent;
    }

    // 2.设置 Swapchain 像素格式和色彩空间
    vk::Result Result;
    if (_AvailableSurfaceFormats.empty())
    {
        if ((Result = ObtainPhysicalDeviceSurfaceFormats()) != vk::Result::eSuccess)
        {
            return Result;
        }
    }

    if (_SwapchainCreateInfo.imageFormat == vk::Format::eUndefined)
    {
        if (SetSurfaceFormat({ vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear }) != vk::Result::eSuccess &&
            SetSurfaceFormat({ vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear }) != vk::Result::eSuccess)
        {
            NpgsCoreError("The default renderer supports only SDR output: R8G8B8A8Unorm or "
                          "B8G8R8A8Unorm with SrgbNonlinear color space. No supported pair is available; "
                          "sRGB-attachment gamma conversion and HDR output are not implemented.");
            return vk::Result::eErrorFormatNotSupported;
        }
    }
    NpgsCoreInfo("Selected swapchain format {}, color space {}.",
                 vk::to_string(_SwapchainCreateInfo.imageFormat),
                 vk::to_string(_SwapchainCreateInfo.imageColorSpace));

    // 3.设置 Swapchain 呈现模式
    std::vector<vk::PresentModeKHR> SurfacePresentModes;
    try
    {
        SurfacePresentModes = _PhysicalDevice.getSurfacePresentModesKHR(_Surface);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to get surface present modes: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    // FIFO is guaranteed; immediate (the zero/default enum value) is not.
    _SwapchainCreateInfo.setPresentMode(vk::PresentModeKHR::eFifo);
    if (!bLimitFps)
    {
        for (const auto& SurfacePresentMode : SurfacePresentModes)
        {
            if (SurfacePresentMode == vk::PresentModeKHR::eMailbox)
            {
                _SwapchainCreateInfo.setPresentMode(vk::PresentModeKHR::eMailbox);
                break;
            }
        }
    }

    if ((Result = CreateSwapchainInternal()) != vk::Result::eSuccess)
    {
        return Result;
    }

    for (auto& Callback : _CreateSwapchainCallbacks)
    {
        Callback.second();
    }

    NpgsCoreInfo("Swapchain created successfully.");
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::RecreateSwapchain()
{
    vk::SurfaceCapabilitiesKHR SurfaceCapabilities;
    try
    {
        SurfaceCapabilities = _PhysicalDevice.getSurfaceCapabilitiesKHR(_Surface);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to get surface capabilities: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    const vk::Extent2D Extent = ResolveSwapchainExtent(SurfaceCapabilities, _FramebufferSize);
    if (Extent.width == 0 || Extent.height == 0)
    {
        return vk::Result::eNotReady;
    }
    _SwapchainExtent = Extent;
    _SwapchainCreateInfo.setImageExtent(Extent)
                        .setPreTransform(SurfaceCapabilities.currentTransform);
    std::uint32_t ImageCount = SurfaceCapabilities.minImageCount + 1;
    if (SurfaceCapabilities.maxImageCount != 0)
    {
        ImageCount = std::min(ImageCount, SurfaceCapabilities.maxImageCount);
    }
    _SwapchainCreateInfo.setMinImageCount(ImageCount);

    try
    {
        _GraphicsQueue.waitIdle();
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to wait for graphics queue to be idle: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    if (_GraphicsQueueFamilyIndex != _PresentQueueFamilyIndex)
    {
        try
        {
            _PresentQueue.waitIdle();
        }
        catch (const vk::SystemError& e)
        {
            NpgsCoreError("Failed to wait for present queue to be idle: {}", e.what());
            return static_cast<vk::Result>(e.code().value());
        }
    }

    for (auto& Callback : _DestroySwapchainCallbacks)
    {
        Callback.second();
    }
    for (auto& ImageView : _SwapchainImageViews)
    {
        if (ImageView)
        {
            _Device.destroyImageView(ImageView);
        }
    }
    _SwapchainImageViews.clear();

    const vk::SwapchainKHR OldSwapchain = _Swapchain;
    _SwapchainCreateInfo.setOldSwapchain(OldSwapchain);
    if (vk::Result Result = CreateSwapchainInternal(); Result != vk::Result::eSuccess)
    {
        return Result;
    }
    // Both graphics and presentation were drained before retiring the old WSI
    // objects. Never defer their destruction until a potentially blocked acquire.
    if (OldSwapchain)
    {
        _Device.destroySwapchainKHR(OldSwapchain);
    }
    _SwapchainCreateInfo.setOldSwapchain({});

    for (auto& Callback : _CreateSwapchainCallbacks)
    {
        Callback.second();
    }

    NpgsCoreInfo("Swapchain recreated successfully.");
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::SwapImage(vk::Semaphore Semaphore)
{
    if (_FramebufferSize.width == 0 || _FramebufferSize.height == 0)
    {
        return vk::Result::eNotReady;
    }
    // A SUBOPTIMAL acquire has already signalled Semaphore. The caller must
    // consume it in submit/present, then rebuild with a fresh framebuffer size.
    // OUT_OF_DATE did not acquire an image. Never hide either result by retrying.
    return _Device.acquireNextImageKHR(_Swapchain, std::numeric_limits<std::uint64_t>::max(),
                                       Semaphore, vk::Fence(), &_CurrentImageIndex);
}

vk::Result FVulkanCore::PresentImage(const vk::PresentInfoKHR& PresentInfo)
{
    // Use the result-returning Vulkan-Hpp overload, without enhanced-mode
    // exceptions or implicit recreation, so the caller can count real presents.
    return _PresentQueue.presentKHR(&PresentInfo);
}

vk::Result FVulkanCore::PresentImage(vk::Semaphore Semaphore)
{
    vk::PresentInfoKHR PresentInfo;
    if (Semaphore)
    {
        PresentInfo = vk::PresentInfoKHR(Semaphore, _Swapchain, _CurrentImageIndex);
    }
    else
    {
        PresentInfo = vk::PresentInfoKHR({}, _Swapchain, _CurrentImageIndex);
    }
    return PresentImage(PresentInfo);
}

vk::Result FVulkanCore::WaitIdle() const
{
    if (_bShutdown || !_Device)
    {
        return vk::Result::eSuccess;
    }
    try
    {
        _Device.waitIdle();
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to wait for device to be idle: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    return vk::Result::eSuccess;
}

FVulkanCore* FVulkanCore::GetClassInstance()
{
    static FVulkanCore kInstance;
    return &kInstance;
}

void FVulkanCore::AddElementChecked(const char* Element, std::vector<const char*>& Vector)
{
    auto it = std::find_if(Vector.begin(), Vector.end(), [&Element](const char* ElementInVector)
    {
        return Util::Equal(Element, ElementInVector);
    });

    if (it == Vector.end())
    {
        Vector.push_back(Element);
    }
}

vk::Result FVulkanCore::UseLatestApiVersion()
{
    std::uint32_t LoaderVersion = VK_API_VERSION_1_0;
    if (reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")))
    {
        try
        {
            LoaderVersion = vk::enumerateInstanceVersion();
        }
        catch (const vk::SystemError& e)
        {
            NpgsCoreError("Failed to enumerate instance version: {}", e.what());
            return static_cast<vk::Result>(e.code().value());
        }
    }
    else
    {
        NpgsCoreError("Vulkan 1.4 is required; the loader only supports Vulkan 1.0.");
        return vk::Result::eErrorIncompatibleDriver;
    }

    NpgsCoreInfo("Vulkan loader version: {}.{}.{}; required API: 1.4.",
                 VK_VERSION_MAJOR(LoaderVersion), VK_VERSION_MINOR(LoaderVersion), VK_VERSION_PATCH(LoaderVersion));
    if (LoaderVersion < VK_API_VERSION_1_4)
    {
        NpgsCoreError("Vulkan 1.4 or newer is required by the renderer and shader target.");
        return vk::Result::eErrorIncompatibleDriver;
    }
    // The loader's patch/newer minor version is not the physical device API.
    _ApiVersion = VK_API_VERSION_1_4;
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CreateDebugMessenger()
{
    kVkCreateDebugUtilsMessengerExt =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(_Instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
    if (kVkCreateDebugUtilsMessengerExt == nullptr)
    {
        NpgsCoreError("Failed to get vkCreateDebugUtilsMessengerEXT function pointer.");
        return vk::Result::eErrorExtensionNotPresent;
    }

    kVkDestroyDebugUtilsMessengerExt =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(_Instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
    if (kVkDestroyDebugUtilsMessengerExt == nullptr)
    {
        NpgsCoreError("Failed to get vkDestroyDebugUtilsMessengerEXT function pointer.");
        return vk::Result::eErrorExtensionNotPresent;
    }

    const auto DebugUtilsMessengerCreateInfo = MakeDebugMessengerCreateInfo(&_bValidationFailFast);
    try
    {
        _DebugUtilsMessenger = _Instance.createDebugUtilsMessengerEXT(DebugUtilsMessengerCreateInfo);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to create debug messenger: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    NpgsCoreInfo("Debug messenger created successfully.");
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::EnumeratePhysicalDevices()
{
    try
    {
        _AvailablePhysicalDevices = _Instance.enumeratePhysicalDevices();
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to enumerate physical devices: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    NpgsCoreInfo("Enumerate physical devices successfully, {} devices found.", _AvailablePhysicalDevices.size());
    if (_AvailablePhysicalDevices.empty())
    {
        NpgsCoreError("No Vulkan physical devices are available.");
        return vk::Result::eErrorInitializationFailed;
    }
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::DeterminePhysicalDevice(std::uint32_t Index, bool bEnableGraphicsQueue, bool bEnableComputeQueue)
{
    if (Index >= _AvailablePhysicalDevices.size())
    {
        NpgsCoreError("Physical device preference index {} is out of range.", Index);
        return vk::Result::eErrorInitializationFailed;
    }
    // Treat the old index argument as a preference, not permission to select an
    // unusable GPU. Keep its priority among suitable devices, then try the rest.
    vk::PhysicalDevice BestDevice;
    FQueueFamilyIndicesComplex BestIndices;
    int BestScore = -1;
    for (std::uint32_t i = 0; i < _AvailablePhysicalDevices.size(); ++i)
    {
        try
        {
            const auto Device = _AvailablePhysicalDevices[i];
            const auto Properties = Device.getProperties();
            if (Properties.apiVersion < VK_API_VERSION_1_4)
            {
                NpgsCoreWarn("Skipping GPU {}: Vulkan {}.{}.{} is below required 1.4.",
                             Properties.deviceName.data(), VK_VERSION_MAJOR(Properties.apiVersion),
                             VK_VERSION_MINOR(Properties.apiVersion), VK_VERSION_PATCH(Properties.apiVersion));
                continue;
            }
            if (!SupportsRendererFeatures(Device))
            {
                NpgsCoreWarn("Skipping GPU {}: required dynamicRendering, synchronization2, samplerAnisotropy or shaderStorageImageWriteWithoutFormat is unavailable.",
                             Properties.deviceName.data());
                continue;
            }
            const auto Extensions = Device.enumerateDeviceExtensionProperties();
            const bool bExtensionsAvailable =
                std::all_of(_DeviceExtensions.begin(), _DeviceExtensions.end(), [&Extensions](const char* Requested)
                {
                    return std::any_of(Extensions.begin(), Extensions.end(), [Requested](const auto& Extension)
                    {
                        return std::strcmp(Requested, Extension.extensionName.data()) == 0;
                    });
                });
            if (!bExtensionsAvailable)
            {
                NpgsCoreWarn("Skipping GPU {}: a required device extension is missing.", Properties.deviceName.data());
                continue;
            }
            FQueueFamilyIndicesComplex Indices;
            if (ObtainQueueFamilyIndices(Device, bEnableGraphicsQueue, bEnableComputeQueue, Indices) != vk::Result::eSuccess)
            {
                continue;
            }
            if (_Surface && (Device.getSurfaceFormatsKHR(_Surface).empty() ||
                             Device.getSurfacePresentModesKHR(_Surface).empty()))
            {
                continue;
            }
            const int Score = (Indices.GraphicsQueueFamilyIndex == Indices.PresentQueueFamilyIndex ? 100 : 0) +
                              (i == Index ? 10 : 0) +
                              (Properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 1 : 0);
            if (Score > BestScore)
            {
                BestScore = Score;
                BestDevice = Device;
                BestIndices = Indices;
            }
        }
        catch (const vk::SystemError& e)
        {
            NpgsCoreWarn("Skipping GPU {} after capability query failed: {}", i, e.what());
        }
    }
    if (!BestDevice)
    {
        NpgsCoreError("No GPU satisfies Vulkan 1.4, renderer features, required extensions and graphics/compute/present queues.");
        return vk::Result::eErrorFeatureNotPresent;
    }
    _PhysicalDevice = BestDevice;
    _GraphicsQueueFamilyIndex = BestIndices.GraphicsQueueFamilyIndex;
    _PresentQueueFamilyIndex = BestIndices.PresentQueueFamilyIndex;
    _ComputeQueueFamilyIndex = BestIndices.ComputeQueueFamilyIndex;
    const auto Properties = _PhysicalDevice.getProperties();
    NpgsCoreInfo("Selected GPU {} (Vulkan {}.{}.{}).", Properties.deviceName.data(),
                 VK_VERSION_MAJOR(Properties.apiVersion), VK_VERSION_MINOR(Properties.apiVersion),
                 VK_VERSION_PATCH(Properties.apiVersion));
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::ObtainQueueFamilyIndices(vk::PhysicalDevice PhysicalDevice, bool bEnableGraphicsQueue,
                                                 bool bEnableComputeQueue, FQueueFamilyIndicesComplex& Indices)
{
    std::vector<vk::QueueFamilyProperties> QueueFamilyProperties = PhysicalDevice.getQueueFamilyProperties();
    if (QueueFamilyProperties.empty())
    {
        NpgsCoreError("Failed to get queue family properties.");
        return vk::Result::eErrorInitializationFailed;
    }

    auto& [GraphicsQueueFamilyIndex, PresentQueueFamilyIndex, ComputeQueueFamilyIndex] = Indices;
    GraphicsQueueFamilyIndex = PresentQueueFamilyIndex = ComputeQueueFamilyIndex = vk::QueueFamilyIgnored;

    // The application records draw and dispatch in the graphics command buffer.
    // Its exclusive swapchain path does not perform queue-family ownership
    // transfers, so require one graphics+compute+present family for now.
    for (std::uint32_t i = 0; i != QueueFamilyProperties.size(); ++i)
    {
        if (QueueFamilyProperties[i].queueCount == 0)
        {
            continue;
        }
        const bool bSupportGraphics = !bEnableGraphicsQueue ||
            static_cast<bool>(QueueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics);
        const bool bSupportCompute = !bEnableComputeQueue ||
            static_cast<bool>(QueueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute);
        if (!bSupportGraphics || !bSupportCompute)
        {
            continue;
        }

        if (_Surface)
        {
            try
            {
                if (!PhysicalDevice.getSurfaceSupportKHR(i, _Surface))
                {
                    continue;
                }
            }
            catch (const vk::SystemError& e)
            {
                NpgsCoreError("Failed to determine if the queue family supports presentation: {}", e.what());
                return static_cast<vk::Result>(e.code().value());
            }
        }

        GraphicsQueueFamilyIndex = bEnableGraphicsQueue ? i : vk::QueueFamilyIgnored;
        ComputeQueueFamilyIndex = bEnableComputeQueue ? i : vk::QueueFamilyIgnored;
        PresentQueueFamilyIndex = _Surface ? i : vk::QueueFamilyIgnored;
        NpgsCoreInfo("Selected unified graphics/compute/present queue family {} (flags {}).",
                     i, vk::to_string(QueueFamilyProperties[i].queueFlags));
        return vk::Result::eSuccess;
    }

    NpgsCoreError("No unified graphics+compute+present queue family is available. "
                  "Split-present queue ownership is not supported by the current renderer.");
    return vk::Result::eErrorFeatureNotPresent;
}

vk::Result FVulkanCore::ObtainPhysicalDeviceSurfaceFormats()
{
    try
    {
        _AvailableSurfaceFormats = _PhysicalDevice.getSurfaceFormatsKHR(_Surface);
    }
    catch (const vk::SystemError& e)
    {
        NpgsCoreError("Failed to get surface formats: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }

    NpgsCoreInfo("Surface formats obtained successfully, {} formats found.", _AvailableSurfaceFormats.size());
    if (_AvailableSurfaceFormats.empty())
    {
        return vk::Result::eErrorFormatNotSupported;
    }
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::CreateSwapchainInternal()
{
    vk::SwapchainKHR NewSwapchain;
    std::vector<vk::Image> NewImages;
    std::vector<vk::ImageView> NewImageViews;
    try
    {
        NewSwapchain = _Device.createSwapchainKHR(_SwapchainCreateInfo);
        NewImages = _Device.getSwapchainImagesKHR(NewSwapchain);
        vk::ImageViewCreateInfo ImageViewCreateInfo = vk::ImageViewCreateInfo()
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(_SwapchainCreateInfo.imageFormat)
            .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
        for (const auto& Image : NewImages)
        {
            ImageViewCreateInfo.setImage(Image);
            NewImageViews.push_back(_Device.createImageView(ImageViewCreateInfo));
        }
    }
    catch (const vk::SystemError& e)
    {
        for (const auto View : NewImageViews)
        {
            _Device.destroyImageView(View);
        }
        if (NewSwapchain)
        {
            _Device.destroySwapchainKHR(NewSwapchain);
        }
        NpgsCoreError("Failed to create swapchain/images/image views: {}", e.what());
        return static_cast<vk::Result>(e.code().value());
    }
    _Swapchain = NewSwapchain;
    _SwapchainImages = std::move(NewImages);
    _SwapchainImageViews = std::move(NewImageViews);
    return vk::Result::eSuccess;
}

vk::Result FVulkanCore::InitializeVmaAllocator()
{
    VmaAllocatorCreateInfo AllocatorCreateInfo
    {
        .flags          = 0,
        .physicalDevice = _PhysicalDevice,
        .device         = _Device,
        .instance       = _Instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    };

    if (vk::Result Result = static_cast<vk::Result>(vmaCreateAllocator(&AllocatorCreateInfo, &_VmaAllocator));
        Result != vk::Result::eSuccess)
    {
        NpgsCoreError("Failed to create VMA allocator: {}", vk::to_string(Result));
        return Result;
    }

    NpgsCoreInfo("VMA allocator created successfully.");
    return vk::Result::eSuccess;
}

_GRAPHICS_END
_RUNTIME_END
_NPGS_END
