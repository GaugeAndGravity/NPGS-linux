#pragma once

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstdint>
#include <stdexcept>
#include <imgui.h>

namespace Npgs::System::UI
{
static_assert(IMGUI_VERSION_NUM == 19300, "NPGS requires the pinned ImGui 1.93.0 docking API");

struct FUIFontTextureLimits
{
    std::uint32_t MaxWidth;
    std::uint32_t MaxHeight;
    std::uint64_t MaxResourceSize;
};

inline void ConfigureUIFontTextures(const FUIFontTextureLimits& Limits)
{
    // ImGui requires power-of-two bounds. Constrain even its largest possible
    // RGBA texture to the device's format-specific resource size.
    auto Width = std::bit_floor(std::min(Limits.MaxWidth, 0x40000000u));
    auto Height = std::bit_floor(std::min(Limits.MaxHeight, 0x40000000u));
    while (std::uint64_t(Width) * Height * 4 > Limits.MaxResourceSize)
    {
        if (Width >= Height) Width /= 2;
        else Height /= 2;
    }
    if (Width < 512 || Height < 128)
        throw std::runtime_error("Device limits cannot hold the initial ImGui font texture");
    auto& Atlas = *ImGui::GetIO().Fonts;
    Atlas.TexMaxWidth = static_cast<int>(Width);
    Atlas.TexMaxHeight = static_cast<int>(Height);
    auto& Platform = ImGui::GetPlatformIO();
    Platform.Renderer_TextureMaxWidth = Atlas.TexMaxWidth;
    Platform.Renderer_TextureMaxHeight = Atlas.TexMaxHeight;
}

inline void ValidateUIFontTexture(const ImTextureData& Texture, const FUIFontTextureLimits& Limits)
{
    if (Texture.Status != ImTextureStatus_WantCreate && Texture.Status != ImTextureStatus_WantUpdates)
        return;
    if (Texture.Format != ImTextureFormat_RGBA32 || Texture.Width <= 0 || Texture.Height <= 0 ||
        std::uint32_t(Texture.Width) > Limits.MaxWidth ||
        std::uint32_t(Texture.Height) > Limits.MaxHeight ||
        std::uint64_t(Texture.Width) * Texture.Height * 4 > Limits.MaxResourceSize)
        throw std::runtime_error("ImGui font texture exceeds device image/resource limits; refusing upload");
}

inline void DrawRotatedUIText(ImDrawList* DrawList, ImFont* Font, float Size,
                              const ImVec2& Center, ImU32 Color, const char* Text)
{
    if ((Color & IM_COL32_A_MASK) == 0 || !Text || !*Text) return;
    const ImVec2 TextSize = Font->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text);
    const int FirstVertex = DrawList->VtxBuffer.Size;
    // Official text rendering resolves dynamic glyphs, texture references and
    // atlas growth. Inverse-rotate the CPU culling rectangle; the draw command
    // retains its original GPU scissor. Otherwise horizontal culling would
    // incorrectly remove visible glyphs in a narrow vertical button.
    const ImVec2 ClipMin = DrawList->GetClipRectMin();
    const ImVec2 ClipMax = DrawList->GetClipRectMax();
    const ImVec4 TextClip(Center.x + ClipMin.y - Center.y, Center.y - ClipMax.x + Center.x,
                         Center.x + ClipMax.y - Center.y, Center.y - ClipMin.x + Center.x);
    Font->RenderText(DrawList, Size, ImVec2(Center.x - TextSize.x * 0.5f,
                                          Center.y - TextSize.y * 0.5f),
                     Color, TextClip, Text, nullptr);
    for (int Index = FirstVertex; Index < DrawList->VtxBuffer.Size; ++Index)
    {
        auto& Position = DrawList->VtxBuffer[Index].pos;
        const float X = Position.x - Center.x;
        const float Y = Position.y - Center.y;
        Position = ImVec2(Center.x - Y, Center.y + X);
    }
}
}
