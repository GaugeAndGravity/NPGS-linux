#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>

#include "DataStructures.h"

#include "Engine/Core/Base/Base.h"
#include "Engine/Core/Runtime/Graphics/Vulkan/Context.h"
#include "Engine/Core/Runtime/Graphics/Vulkan/Resources.h"
#include "Engine/Core/Runtime/Graphics/Vulkan/Wrappers.h"
#include "Engine/Core/Runtime/Graphics/Vulkan/VulkanUIRenderer.h"

#include "Engine/Core/System/Spatial/Camera.h"
#include "Engine/Core/System/ScrollSettings.h"
#include "Engine/Core/System/UI/neural_ui.h" 
#include "Engine/Core/System/UI/ScreenManager.h" // [新增]

#include "Engine/Core/Types/Entries/Astro/Star.h"

#include <Engine/Core/System/Generators/StellarGenerator.h>
_NPGS_BEGIN

namespace Runtime::Asset
{
class FAssetManager;
}

class FApplication
{
private:
    struct FRenderer
    {
        std::vector<Runtime::Graphics::FVulkanFramebuffer> Framebuffers;
        std::unique_ptr<Runtime::Graphics::FVulkanRenderPass> RenderPass;
    };

public:
    void OnLanguageChanged();
    std::string FormatTime(double total_seconds);
    FApplication(const vk::Extent2D& WindowSize, const std::string& WindowTitle, bool bEnableVSync,
                 bool bEnableFullscreen, bool enableValidation = false);
    ~FApplication();

    void Quit();

    std::uint64_t ExecuteMainRender(std::uint64_t maxFrames = 0, bool smokeResize = false);
    void Terminate();
    void Command(const std::string& command);
private:
    bool InitializeWindow();
    void InitializeInputCallbacks();
    void SetCursorMode(bool disabled);
    bool WaitForDrawableFramebuffer();
    void CheckFramebufferPixelLimit() const;
    void RebuildSwapchain();
    void ProcessInput();
    void update();
    void RenderDebugUI();

    void DumpArgsToJson(const std::string& filepath);

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void CharCallback(GLFWwindow* window, unsigned int codepoint);
    static void FramebufferSizeCallback(GLFWwindow* Window, int Width, int Height);
    static void CursorPosCallback(GLFWwindow* Window, double PosX, double PosY);
    static void ScrollCallback(GLFWwindow* Window, double OffsetX, double OffsetY);

    void HandleMouseButton(int button, int action, int mods);
    void HandleKey(int key, int scancode, int action, int mods);
    void HandleChar(unsigned int codepoint);
    void HandleFramebufferSize(int width, int height);
    void HandleCursorPos(double posX, double posY);
    void HandleScroll(double offsetX, double offsetY); 

    //void Quit();
private:
    Runtime::Graphics::FVulkanContext*        _VulkanContext;
    Runtime::Asset::FAssetManager*             _AssetManager = nullptr;

    std::string                               _WindowTitle;
    vk::Extent2D                              _WindowSize;
    GLFWwindow*                               _Window = nullptr;
    bool                                      _bEnableVSync;
    bool                                      _bEnableFullscreen;
    bool                                      _bEnableValidation;
    bool                                      _bGlfwInitialized = false;
    bool                                      _bFramebufferResized = false;
    vk::Extent2D                              _FramebufferSize{};
    std::uint64_t                             _SmokeMaxPixels = 0;
    std::uint64_t                             _SwapchainRebuildCount = 0;

    std::unique_ptr<Runtime::Graphics::FVulkanUIRenderer> _uiRenderer;
    std::unique_ptr<Npgs::System::UI::ScreenManager> m_screen_manager;
    std::string m_beam_energy;
    std::string m_rkkv_mass;
    std::string m_VN_mass;
    ImTextureID stage0ID;
    ImTextureID stage1ID;
    ImTextureID stage2ID;
    ImTextureID stage3ID;
    ImTextureID stage4ID;

    // State trackers for the buttons
    bool m_is_beam_button_active;
    bool m_is_rkkv_button_active;

    float s_GeodesicThrust = 1.5f;
    System::FScrollSettings _ScrollSettings;
    std::unique_ptr<System::Spatial::FCamera> _FreeCamera;
    glm::vec3 LastCameraWorldPos = glm::vec3(0.0f); // 用于记录上一帧位置
    bool _bLeftMousePressedInWorld = false;
    bool _bIsDraggingInWorld = false;
    double CurrentTime = 0.0;
    double RealityTime = 0.0;
    double GameTime = 0.0;
    double TimeRate = 1.0;
    double PreviousTime = 0.0;
    double LastFrameTime = 0.0;
    int    FramePerSec= 0;
    long long int  FrameCount = 0;
         
    double                                    _DeltaTime   = 0.0;
    double                                    _LastDeltaTime   = 0.0;
    double                                    _DragStartX  = 0.0;
    double                                    _DragStartY  = 0.0;
    double                                    _LastX       = 0.0;
    double                                    _LastY       = 0.0;

    bool   _bRightMousePressedInWorld = false;
    bool   _bIsDraggingRightInWorld = false;
    bool   _bFirstMouseRight = true;
    double _DragRightStartX = 0.0;
    double _DragRightStartY = 0.0;
    double _LastRightX = 0.0;
    double _LastRightY = 0.0;

    float _buffered_scroll_y = 0.0f;
    bool                                      _bFirstMouse = true;
};

_NPGS_END
