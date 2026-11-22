// VulkanUIRenderer.h
#pragma once

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Engine/Core/Runtime/Graphics/Vulkan/Context.h"
#include "Engine/Core/Runtime/Graphics/Vulkan/Resources.h"
#include "Engine/Core/System/UI/UIFontRendering.h"

_NPGS_BEGIN
_RUNTIME_BEGIN
_GRAPHICS_BEGIN

class FVulkanUIRenderer
{
public:
    FVulkanUIRenderer();
    ~FVulkanUIRenderer();

    // Uses dynamic rendering into the context's swapchain color format.
    bool Initialize(GLFWwindow* window);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void Render(vk::CommandBuffer commandBuffer);

    void Resize(uint32_t width, uint32_t height);

    ImTextureID AddTexture(vk::ImageView imageView, vk::ImageLayout layout);

    // Transfers ownership of a descriptor returned by this renderer's AddTexture;
    // do not pass borrowed texture IDs. Frees the previously owned blur descriptor.
    // Before replacing/clearing, the caller must ensure no in-flight GPU work
    // uses the old descriptor (e.g. wait idle during swapchain reconstruction).
    // A nonzero ID requires a live initialized backend. Zero clears the blur.
    void SetBlurTexture(ImTextureID texture);
private:
    void CreateDescriptorPool();
    void CleanupDescriptorPool();

private:
    bool _initialized = false;
    bool _glfwInitialized = false;
    bool _vulkanInitialized = false;
    ImGuiContext* _imguiContext = nullptr;
    uint32_t _imageCount = 0;
    VkFormat _colorAttachmentFormat = VK_FORMAT_UNDEFINED;
    ImTextureID _blurTexture = 0; // Only this explicitly transferred ID is owned.
    System::UI::FUIFontTextureLimits _fontTextureLimits{};
    vk::DescriptorPool _descriptorPool;
    FVulkanContext* _context = nullptr;
};

_GRAPHICS_END
_RUNTIME_END
_NPGS_END
