#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <fontconfig/fontconfig.h>
#include <imgui.h>

namespace Npgs::System::UI
{
struct FSystemFontFace
{
    std::string Path;
    int FaceIndex = 0;
};

struct FSystemUIFonts
{
    // Primary first, then only faces adding coverage of required UI characters.
    std::vector<FSystemFontFace> Faces;
    std::vector<std::uint32_t> MissingCharacters;
};

// A null Config loads the standard system/user Fontconfig configuration.
// NPGS_FONT overrides the primary file (face zero), not system fallback.
FSystemUIFonts FindSystemUIFonts(FcConfig* Config = nullptr, std::string_view TranslationText = {});

// Shared by the renderer and headless tests. ImGui owns sources and the atlas.
ImFont* AddSystemUIFont(ImFontAtlas& Atlas, const FSystemUIFonts& Fonts, float Size);
}
