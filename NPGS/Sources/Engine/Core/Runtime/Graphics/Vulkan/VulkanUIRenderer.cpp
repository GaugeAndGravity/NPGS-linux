#include "VulkanUIRenderer.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "Engine/Core/Runtime/AssetLoaders/GetAssetFullPath.h"
#include "Engine/Core/System/UI/SystemFonts.h"
#include "Engine/Core/System/UI/ui_framework.h"
#include "Engine/Utils/Logger.h"

_NPGS_BEGIN
_RUNTIME_BEGIN
_GRAPHICS_BEGIN

namespace
{
System::UI::FUIFontTextureLimits GetUIFontAtlasLimits(vk::PhysicalDevice Device, std::uint32_t MaxImageDimension2D)
{
    // Match the official Vulkan backend's dynamic texture configuration.
    VkImageFormatProperties FormatProperties{};
    const VkResult Result = vkGetPhysicalDeviceImageFormatProperties(
        static_cast<VkPhysicalDevice>(Device), VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &FormatProperties);
    if (Result != VK_SUCCESS)
        throw std::runtime_error("Cannot query ImGui RGBA8_UNORM sampled/transfer-destination atlas limits: VkResult " +
                                 std::to_string(static_cast<int>(Result)));
    if (!(FormatProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) ||
        FormatProperties.maxExtent.depth < 1 || FormatProperties.maxMipLevels < 1 ||
        FormatProperties.maxArrayLayers < 1)
        throw std::runtime_error("Device does not support the ImGui RGBA8_UNORM font atlas image configuration");
    return {
        std::min(MaxImageDimension2D, FormatProperties.maxExtent.width),
        std::min(MaxImageDimension2D, FormatProperties.maxExtent.height),
        FormatProperties.maxResourceSize
    };
}

void LoadUIFonts()
{
    ImGuiIO& IO = ImGui::GetIO();
    const auto TranslationPath = Asset::GetAssetPath("Lang/translations.json");
    std::ifstream TranslationFile(TranslationPath, std::ios::binary);
    if (!TranslationFile)
        throw std::runtime_error("Cannot read UI translations for font coverage: " + TranslationPath);
    const std::string Translations{std::istreambuf_iterator<char>(TranslationFile), {}};
    const auto Fonts = System::UI::FindSystemUIFonts(nullptr, Translations);
    for (const auto& Face : Fonts.Faces)
        NpgsCoreInfo("UI font source: {} (face {})", Face.Path, Face.FaceIndex);
    if (!Fonts.MissingCharacters.empty())
        std::cerr << "NPGS UI: system fonts lack " << Fonts.MissingCharacters.size()
                  << " requested Chinese/UI characters. Install Noto Sans CJK / Source Han Sans "
                     "and system symbol fonts, or set NPGS_FONT to an absolute font path.\n";
    auto LoadFont = [&](float Size) { return System::UI::AddSystemUIFont(*IO.Fonts, Fonts, Size); };

    auto& UI = System::UI::UIContext::Get();
    UI.m_font_regular = LoadFont(UI.FontSizeRegular);
    UI.m_font_bold = LoadFont(UI.FontSizeBold);
    UI.m_font_large = LoadFont(UI.FontSizeLarge);
    UI.m_font_small = LoadFont(UI.FontSizeSmall);
    UI.m_font_subtitle = LoadFont(UI.FontSizeSubtitle);
    IO.FontDefault = UI.m_font_regular;
    ImGui::GetStyle().FontSizeBase = UI.FontSizeRegular;
}
}

FVulkanUIRenderer::FVulkanUIRenderer()
    : _context(FVulkanContext::GetClassInstance())
{}

FVulkanUIRenderer::~FVulkanUIRenderer()
{
    Shutdown();
}

bool FVulkanUIRenderer::Initialize(GLFWwindow* window)
{
    if (_initialized) return true;
    if (!window)
        throw std::invalid_argument("UI initialization requires a GLFW window");

    try
    {
        CreateDescriptorPool();
        IMGUI_CHECKVERSION();
        _imguiContext = ImGui::CreateContext();
        ImGuiIO& IO = ImGui::GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Wayland cannot position platform windows. Keep docking and the full
        // application UI in the main GLFW window, on every platform.
        IO.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        // This UI owns its layout. Do not read/write imgui.ini relative to cwd.
        IO.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        _fontTextureLimits = GetUIFontAtlasLimits(
            _context->GetPhysicalDevice(), _context->GetPhysicalDeviceProperties().limits.maxImageDimension2D);
        System::UI::ConfigureUIFontTextures(_fontTextureLimits);
        LoadUIFonts();

        if (!ImGui_ImplGlfw_InitForVulkan(window, true))
            throw std::runtime_error("Failed to initialize ImGui GLFW backend");
        _glfwInitialized = true;

        _imageCount = _context->GetSwapchainImageCount();
        ImGui_ImplVulkan_InitInfo Info{};
        Info.ApiVersion = VK_API_VERSION_1_4;
        Info.Instance = _context->GetInstance();
        Info.PhysicalDevice = _context->GetPhysicalDevice();
        Info.Device = _context->GetDevice();
        Info.QueueFamily = _context->GetGraphicsQueueFamilyIndex();
        Info.Queue = _context->GetGraphicsQueue();
        Info.DescriptorPool = _descriptorPool;
        _colorAttachmentFormat = static_cast<VkFormat>(_context->GetSwapchainCreateInfo().imageFormat);
        Info.UseDynamicRendering = true;
        Info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        Info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        // The backend retains this pointer beyond Init(), so use member storage.
        Info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_colorAttachmentFormat;
        Info.MinImageCount = std::max(2u, _imageCount);
        Info.ImageCount = _imageCount;
        Info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        Info.CheckVkResultFn = [](VkResult Result)
        {
            if (Result < VK_SUCCESS)
                throw std::runtime_error("ImGui Vulkan backend failed with VkResult " +
                                         std::to_string(static_cast<int>(Result)));
        };
        if (_imageCount < 2)
            throw std::runtime_error("ImGui Vulkan backend requires at least two swapchain images");
        if (!ImGui_ImplVulkan_Init(&Info))
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
        _vulkanInitialized = true;

        if (!(IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures))
            throw std::runtime_error("ImGui Vulkan backend must support dynamic textures");

        _initialized = true;
        NpgsCoreInfo("Vulkan UI Renderer initialized successfully");
        return true;
    }
    catch (...)
    {
        Shutdown();
        throw;
    }
}

void FVulkanUIRenderer::Shutdown()
{
    if (_vulkanInitialized)
        _context->WaitIdle();
    if (_imguiContext)
        ImGui::SetCurrentContext(_imguiContext);
    // WaitIdle above retires all uses; RemoveTexture needs the live backend/pool.
    SetBlurTexture(0);
    if (_vulkanInitialized)
        ImGui_ImplVulkan_Shutdown();
    if (_glfwInitialized)
        ImGui_ImplGlfw_Shutdown();
    if (_imguiContext)
    {
        auto& UI = System::UI::UIContext::Get();
        UI.m_font_regular = UI.m_font_bold = UI.m_font_large = UI.m_font_small = UI.m_font_subtitle = nullptr;
        ImGui::DestroyContext(_imguiContext);
    }
    CleanupDescriptorPool();
    _imguiContext = nullptr;
    _glfwInitialized = _vulkanInitialized = _initialized = false;
}

void FVulkanUIRenderer::BeginFrame()
{
    if (!_initialized) return;
    ImGui::SetCurrentContext(_imguiContext);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void FVulkanUIRenderer::EndFrame()
{
    if (_initialized) ImGui::Render();
}

void FVulkanUIRenderer::Render(vk::CommandBuffer commandBuffer)
{
    if (_initialized)
    {
        ImDrawData* DrawData = ImGui::GetDrawData();
        if (DrawData->Textures)
            for (const ImTextureData* Texture : *DrawData->Textures)
                System::UI::ValidateUIFontTexture(*Texture, _fontTextureLimits);
        // On the application's render thread, before its frame submit. The
        // official backend owns texture uploads, queue waits and retirement.
        ImGui_ImplVulkan_RenderDrawData(DrawData, commandBuffer);
    }
}

void FVulkanUIRenderer::Resize(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    if (!_initialized) return;
    const uint32_t ImageCount = _context->GetSwapchainImageCount();
    if (ImageCount != _imageCount)
    {
        if (ImageCount < 2)
            throw std::runtime_error("ImGui Vulkan backend requires at least two swapchain images");
        ImGui_ImplVulkan_SetMinImageCount(ImageCount);
        _imageCount = ImageCount;
    }
}

void FVulkanUIRenderer::CreateDescriptorPool()
{
    const std::vector<vk::DescriptorPoolSize> PoolSizes = {
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eSampler, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE }
    };
    vk::DescriptorPoolCreateInfo PoolInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        1000 + IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE, PoolSizes);
    _descriptorPool = _context->GetDevice().createDescriptorPool(PoolInfo);
}

void FVulkanUIRenderer::CleanupDescriptorPool()
{
    if (_descriptorPool)
    {
        _context->GetDevice().destroyDescriptorPool(_descriptorPool);
        _descriptorPool = nullptr;
    }
}

ImTextureID FVulkanUIRenderer::AddTexture(vk::ImageView imageView, vk::ImageLayout layout)
{
    return (ImTextureID)ImGui_ImplVulkan_AddTexture(
        static_cast<VkImageView>(imageView),
        static_cast<VkImageLayout>(layout));
}

void FVulkanUIRenderer::SetBlurTexture(ImTextureID texture)
{
    if (texture && !_vulkanInitialized)
        throw std::logic_error("Cannot adopt a blur texture without a live ImGui Vulkan backend");
    if (_imguiContext)
        ImGui::SetCurrentContext(_imguiContext);
    if (_blurTexture != texture)
    {
        if (_blurTexture)
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)_blurTexture);
        _blurTexture = texture;
    }
    // UIContext borrows the ID; never free an ID merely found in its public field.
    System::UI::UIContext::Get().m_scene_blur_texture = texture;
}

_GRAPHICS_END
_RUNTIME_END
_NPGS_END
