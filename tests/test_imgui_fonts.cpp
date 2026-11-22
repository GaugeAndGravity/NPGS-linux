#include <array>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include "Engine/Core/System/UI/ui_framework.h"
#include "Engine/Core/System/UI/UIFontRendering.h"
#include "Engine/Core/System/UI/SystemFonts.h"
#include <fontconfig/fcfreetype.h>

using namespace Npgs::System::UI;

namespace
{
std::string Translations;

void Check(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

struct FTextureConsumer
{
    unsigned Creates = 0, Updates = 0, Retired = 0;
    ImTextureID NextID = 1;

    // CPU model of the official renderer texture protocol, not a font manager.
    void Consume(ImDrawData& Data, const FUIFontTextureLimits& Limits)
    {
        Check(Data.Textures != nullptr, "DrawData must carry dynamic textures");
        for (ImTextureData* Texture : *Data.Textures)
        {
            ValidateUIFontTexture(*Texture, Limits);
            if (Texture->Status == ImTextureStatus_WantCreate)
            {
                Check(Texture->GetPixels() != nullptr, "Created texture needs pixels");
                Texture->SetTexID(NextID++);
                Texture->SetStatus(ImTextureStatus_OK);
                ++Creates;
            }
            else if (Texture->Status == ImTextureStatus_WantUpdates)
            {
                Check(Texture->Updates.Size > 0, "Dynamic update needs rectangles");
                Check(Texture->UpdateRect.x + Texture->UpdateRect.w <= Texture->Width &&
                      Texture->UpdateRect.y + Texture->UpdateRect.h <= Texture->Height,
                      "Texture update outside image");
                Texture->SetStatus(ImTextureStatus_OK);
                ++Updates;
            }
            else if (Texture->Status == ImTextureStatus_WantDestroy && Texture->UnusedFrames >= 2)
            {
                Texture->SetTexID(ImTextureID_Invalid);
                Texture->SetStatus(ImTextureStatus_Destroyed);
                ++Retired;
            }
        }
        for (const ImDrawList* List : Data.CmdLists)
            for (const ImDrawCmd& Command : List->CmdBuffer)
            {
                if (!Command.ElemCount || Command.UserCallback) continue;
                Check(Command.TexRef._TexData != nullptr, "Font command must retain a texture reference");
                Check(Command.GetTexID() != ImTextureID_Invalid, "Same-frame texture must be resolved");
                const auto* Texture = Command.TexRef._TexData;
                Check(Texture->Status == ImTextureStatus_OK, "Draw uses a retired/pending texture");
                for (unsigned Index = Command.IdxOffset; Index < Command.IdxOffset + Command.ElemCount; ++Index)
                {
                    const auto& Vertex = List->VtxBuffer[List->IdxBuffer[Index] + Command.VtxOffset];
                    Check(std::isfinite(Vertex.pos.x) && std::isfinite(Vertex.pos.y), "Nonfinite glyph position");
                    Check(Vertex.uv.x >= 0 && Vertex.uv.x <= 1 && Vertex.uv.y >= 0 && Vertex.uv.y <= 1,
                          "Glyph UV outside its referenced texture");
                }
            }
    }
};

void Run(const FSystemUIFonts& Selection, bool CJK)
{
    ImGui::CreateContext();
    auto& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.DisplaySize = ImVec2(1000, 800);
    const FUIFontTextureLimits Limits{2048, 2048, 2048ull * 2048 * 4};
    ConfigureUIFontTextures(Limits);
    auto& UI = UIContext::Get();
    std::array<ImFont**, 5> Slots{&UI.m_font_regular, &UI.m_font_bold, &UI.m_font_large,
                                &UI.m_font_small, &UI.m_font_subtitle};
    const std::array<float, 5> Sizes{UI.FontSizeRegular, UI.FontSizeBold, UI.FontSizeLarge,
                                     UI.FontSizeSmall, UI.FontSizeSubtitle};
    std::array<ImFont*, 5> Fonts{};
    for (unsigned Index = 0; Index < Fonts.size(); ++Index)
    {
        Fonts[Index] = AddSystemUIFont(*IO.Fonts, Selection, Sizes[Index]);
        Check(Fonts[Index] != nullptr, "Font loading failed");
        Check(Fonts[Index]->Sources.Size == static_cast<int>(Selection.Faces.size()),
              "Production merge sources were not retained");
        for (int Source = 0; Source < Fonts[Index]->Sources.Size; ++Source)
            Check(Fonts[Index]->Sources[Source]->FontNo == Selection.Faces[Source].FaceIndex,
                  "Fontconfig TTC face index was not passed to ImGui");
        *Slots[Index] = Fonts[Index];
    }
    IO.FontDefault = UI.m_font_regular;
    ImGui::GetStyle().FontSizeBase = UI.FontSizeRegular;
    if (CJK)
    {
        Check(Fonts[0]->IsGlyphInFont(0x4e2d) && Fonts[0]->IsGlyphInFont(0x6587),
              "Selected CJK font lacks expected glyphs");
        ImFontGlyphRangesBuilder Required;
        Required.AddText(Translations.c_str());
        Required.AddText("—“”…∮⌘▶◈☼の！（），：；？");
        ImVector<ImWchar> Ranges;
        Required.BuildRanges(&Ranges);
        unsigned Missing = 0, Covered = 0;
        for (int Index = 0; Ranges[Index]; Index += 2)
            for (unsigned Codepoint = Ranges[Index]; Codepoint <= Ranges[Index + 1]; ++Codepoint)
            {
                if (Codepoint < 0x20) continue;
                if (Fonts[0]->IsGlyphInFont(Codepoint)) ++Covered;
                else
                {
                    ++Missing;
                    Check(std::ranges::find(Selection.MissingCharacters, Codepoint) !=
                          Selection.MissingCharacters.end(),
                          "Discovery failed to request/report a translation/UI symbol");
                }
            }
        std::cout << "Translation/UI coverage: " << Covered << " characters, " << Missing << " unavailable\n";
        if (Missing) std::cout << "SKIP: unavailable system translation/symbol glyphs listed by discovery\n";
    }
    const char* Text = CJK ? "NPGS 中文设置の∮⌘▶◈☼" : "NPGS UI text";
    FTextureConsumer Consumer;
    const std::array<float, 9> Scales{1, 1, 1.25f, 1.5f, 2, 1, 1, 1, 1};
    std::array<float, 5> BaseWidths{};
    for (unsigned Frame = 0; Frame < Scales.size(); ++Frame)
    {
        const float Scale = Scales[Frame];
        const float FontScale = Frame == 5 ? 1.25f : 1.0f;
        ImGui::GetStyle().FontScaleMain = FontScale;
        IO.DisplayFramebufferScale = ImVec2(Scale, Scale);
        IO.DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("CPU fonts", nullptr, ImGuiWindowFlags_NoDecoration);
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        for (unsigned Index = 0; Index < Fonts.size(); ++Index)
        {
            Check(*Slots[Index] == Fonts[Index], "Font pointer changed across scale transitions");
            const float Size = UI.GetFontSize(Fonts[Index]);
            Check(UI.GetFontBaseSize(Fonts[Index]) == Sizes[Index], "UIContext base size changed");
            Check(Size == std::floor(Sizes[Index] * FontScale + 0.5f), "UIContext effective logical size mismatch");
            ImGui::PushFont(Fonts[Index], UI.GetFontBaseSize(Fonts[Index]));
            Check(ImGui::GetFontSize() == Size, "PushFont logical size mismatch");
            auto* Baked = ImGui::GetFontBaked(); // Valid only in this frame.
            Check(Baked->Size == Size && Baked->RasterizerDensity == Scale,
                  "Official baked size/density chain mismatch");
            if (CJK && Frame == 0)
                Check(!Baked->IsGlyphLoaded(0x4e2d), "CJK glyph should load on demand");
            const ImVec2 Measured = Fonts[Index]->CalcTextSizeA(Size, FLT_MAX, 0, Text);
            if (CJK)
                Check(Baked->IsGlyphLoaded(0x4e2d), "Text measurement did not request a CJK glyph");
            Check(Measured.y == Size, "Text height must remain logical");
            if (Frame == 0) BaseWidths[Index] = Measured.x;
            Check(std::abs(Measured.x / FontScale - BaseWidths[Index]) < 8, "Framebuffer scale changed logical width");
            const char* End = Text + std::char_traits<char>::length(Text);
            const char* Wrapped = Fonts[Index]->CalcWordWrapPosition(Size, Text, End, Measured.x * 0.6f);
            Check(Wrapped > Text && Wrapped < End, "Logical-size word wrapping failed");
            DrawList->AddText(Fonts[Index], Size, ImVec2(40, 40 + Index * 60), IM_COL32_WHITE, Text);
            const ImVec2 Center(700, 200 + Index * 100);
            const int ReferenceStart = DrawList->VtxBuffer.Size;
            DrawList->AddText(Fonts[Index], Size,
                              ImVec2(Center.x - Measured.x / 2, Center.y - Measured.y / 2),
                              IM_COL32_WHITE, Text);
            const int RotatedStart = DrawList->VtxBuffer.Size;
            DrawRotatedUIText(DrawList, Fonts[Index], Size, Center, IM_COL32_WHITE, Text);
            Check(DrawList->VtxBuffer.Size - RotatedStart == RotatedStart - ReferenceStart,
                  "Rotation changed glyph geometry count");
            for (int Vertex = 0; Vertex < RotatedStart - ReferenceStart; ++Vertex)
            {
                const auto& Original = DrawList->VtxBuffer[ReferenceStart + Vertex];
                const auto& Rotated = DrawList->VtxBuffer[RotatedStart + Vertex];
                Check(std::abs(Rotated.pos.x - (Center.x - (Original.pos.y - Center.y))) < 0.001f &&
                      std::abs(Rotated.pos.y - (Center.y + (Original.pos.x - Center.x))) < 0.001f,
                      "Rotation did not preserve logical glyph metrics");
                Check(Original.uv.x == Rotated.uv.x && Original.uv.y == Rotated.uv.y,
                      "Rotation changed glyph UVs");
            }
            ImGui::PopFont();
        }
        {
            ImFont* Font = UI.m_font_small;
            const float Size = UI.GetFontSize(Font);
            const ImVec2 Center(400, 400);
            const ImVec2 Extent = Font->CalcTextSizeA(Size, FLT_MAX, 0, Text);
            const int ReferenceStart = DrawList->VtxBuffer.Size;
            DrawList->AddText(Font, Size, ImVec2(400 - Extent.x / 2, 400 - Size / 2), IM_COL32_WHITE, Text);
            const int RotatedStart = DrawList->VtxBuffer.Size;
            DrawList->PushClipRect(ImVec2(400 - Size, 200), ImVec2(400 + Size, 600), true);
            DrawRotatedUIText(DrawList, Font, Size, Center, IM_COL32_WHITE, Text);
            DrawList->PopClipRect();
            Check(DrawList->VtxBuffer.Size - RotatedStart == RotatedStart - ReferenceStart,
                  "Vertical button incorrectly culled horizontal source glyphs");
        }
        if (CJK && Frame == 4)
        {
            // On-demand glyphs force bounded official atlas growth, including
            // replacement while commands already refer to the previous texture.
            for (ImWchar Codepoint = 0x4e00; Codepoint < 0x4f80; ++Codepoint)
                IO.FontDefault->GetFontBaked(UI.FontSizeRegular)->FindGlyph(Codepoint);
        }
        ImGui::End();
        ImGui::Render();
        auto& Data = *ImGui::GetDrawData();
        Check(Data.DisplaySize.x == 1000 && Data.FramebufferScale.x == Scale,
              "Logical display/framebuffer scale contract changed");
        Consumer.Consume(Data, Limits);
    }
    Check(Consumer.Creates > 0 && Consumer.Updates > 0, "No dynamic texture creation/update observed");
    if (CJK) Check(Consumer.Creates > 1 && Consumer.Retired > 0, "No official atlas growth/retirement observed");

    bool Rejected = false;
    ImTextureData Oversized;
    Oversized.Width = 4096;
    Oversized.Height = 2048;
    Oversized.Format = ImTextureFormat_RGBA32;
    Oversized.SetStatus(ImTextureStatus_WantCreate);
    try { ValidateUIFontTexture(Oversized, Limits); }
    catch (const std::runtime_error&) { Rejected = true; }
    Check(Rejected, "Oversized upload was not rejected");
    Rejected = false;
    Oversized.Width = Oversized.Height = 2048;
    try { ValidateUIFontTexture(Oversized, {2048, 2048, 1024ull * 1024 * 4}); }
    catch (const std::runtime_error&) { Rejected = true; }
    Check(Rejected, "Resource budget overflow was not rejected");
    ConfigureUIFontTextures({3000, 2048, 1024ull * 1024 * 4});
    Check(IO.Fonts->TexMaxWidth == 1024 && IO.Fonts->TexMaxHeight == 1024,
          "Resource/power-of-two limits not propagated");
    Rejected = false;
    try { ConfigureUIFontTextures({256, 128, 256ull * 128 * 4}); }
    catch (const std::runtime_error&) { Rejected = true; }
    Check(Rejected, "Insufficient initial texture limits were not rejected");
    std::cout << (CJK ? "CJK" : "Latin") << ": scales 1/1.25/1.5/2; five logical sizes; creates="
              << Consumer.Creates << ", updates=" << Consumer.Updates << ", retired=" << Consumer.Retired << '\n';
    for (auto Slot : Slots) *Slot = nullptr;
    ImGui::DestroyContext();
}
}

using FConfig = std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)>;

bool HasCharacter(const FSystemUIFonts& Fonts, FcChar32 Codepoint)
{
    for (const auto& Face : Fonts.Faces)
    {
        int Count = 0;
        std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> Pattern(
            FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(Face.Path.c_str()),
                            Face.FaceIndex, nullptr, &Count), FcPatternDestroy);
        FcCharSet* Charset = nullptr;
        if (Pattern && FcPatternGetCharSet(Pattern.get(), FC_CHARSET, 0, &Charset) == FcResultMatch &&
            FcCharSetHasChar(Charset, Codepoint))
            return true;
    }
    return false;
}

struct FEnvironment
{
    const bool HadValue = std::getenv("NPGS_FONT") != nullptr;
    const std::string Previous = HadValue ? std::getenv("NPGS_FONT") : "";
    FEnvironment() { unsetenv("NPGS_FONT"); }
    ~FEnvironment()
    {
        if (HadValue) setenv("NPGS_FONT", Previous.c_str(), 1);
        else unsetenv("NPGS_FONT");
    }
};

void CheckDiscovery(const FSystemUIFonts& System)
{
    FConfig Empty(FcConfigCreate(), FcConfigDestroy);
    bool Rejected = false;
    try { FindSystemUIFonts(Empty.get()); }
    catch (const std::runtime_error& Error)
    {
        Rejected = std::string(Error.what()).find("No usable system") != std::string::npos;
    }
    Check(Rejected, "Empty isolated Fontconfig config did not report the dependency error");

    const auto AssetDirectory = std::filesystem::path(NPGS_TEST_TRANSLATIONS).parent_path();
    const std::array<std::string, 6> InvalidPaths{
        "", "relative.ttf", (AssetDirectory / "npgs-no-such-font.ttf").string(),
        AssetDirectory.string(), "/dev/null/npgs.ttf", "/proc/self/exe"};
    for (const auto& Invalid : InvalidPaths)
    {
        setenv("NPGS_FONT", Invalid.c_str(), 1);
        Rejected = false;
        try { FindSystemUIFonts(Empty.get()); }
        catch (const std::runtime_error& Error)
        {
            Rejected = std::string(Error.what()).find("NPGS_FONT") != std::string::npos;
        }
        Check(Rejected, "Invalid override was not rejected before ImGui loading");
    }

    const auto& Primary = System.Faces.front();
    setenv("NPGS_FONT", Primary.Path.c_str(), 1);
    auto Override = FindSystemUIFonts(nullptr, Translations);
    Check(Override.Faces.front().Path == Primary.Path && Override.Faces.front().FaceIndex == 0,
          "Override did not retain the explicit primary file, face zero");
    Run(Override, false);
    unsetenv("NPGS_FONT");

    // No FcConfigSetCurrent or environment config override: these tests add
    // installed font files to a private Fontconfig object, never the host config.
    FConfig Isolated(FcConfigCreate(), FcConfigDestroy);
    Check(FcConfigAppFontAddFile(Isolated.get(),
          reinterpret_cast<const FcChar8*>(Primary.Path.c_str())), "Cannot add isolated system font");
    const auto OnlyPrimary = FindSystemUIFonts(Isolated.get());
    Check(std::ranges::all_of(OnlyPrimary.Faces, [&](const auto& Face) { return Face.Path == Primary.Path; }),
          "Private Fontconfig configuration leaked system fonts");
    Run(OnlyPrimary, false);

    for (const auto& Face : System.Faces)
        Check(FcConfigAppFontAddFile(Isolated.get(),
              reinterpret_cast<const FcChar8*>(Face.Path.c_str())), "Cannot add isolated fallback");
    if (System.Faces.size() > 1)
    {
        const auto& Preferred = System.Faces[1];
        int Count = 0;
        std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)> Pattern(
            FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(Preferred.Path.c_str()),
                            Preferred.FaceIndex, nullptr, &Count), FcPatternDestroy);
        FcChar8* Family = nullptr;
        Check(Pattern && FcPatternGetString(Pattern.get(), FC_FAMILY, 0, &Family) == FcResultMatch,
              "Cannot read preferred system family");
        std::string Escaped;
        for (const char* Char = reinterpret_cast<const char*>(Family); *Char; ++Char)
            switch (*Char)
            {
            case '&': Escaped += "&amp;"; break;
            case '<': Escaped += "&lt;"; break;
            case '>': Escaped += "&gt;"; break;
            default: Escaped += *Char;
            }
        const std::string XML = "<fontconfig><match target=\"pattern\">"
            "<edit name=\"family\" mode=\"assign\" binding=\"strong\"><string>" +
            Escaped + "</string></edit></match></fontconfig>";
        Check(FcConfigParseAndLoadFromMemory(Isolated.get(),
              reinterpret_cast<const FcChar8*>(XML.c_str()), FcTrue),
              "Cannot configure isolated user family preference");
        const auto PreferredFonts = FindSystemUIFonts(Isolated.get(), Translations);
        Check(PreferredFonts.Faces.front().Path == Preferred.Path &&
              PreferredFonts.Faces.front().FaceIndex == Preferred.FaceIndex,
              "Production matching ignored the configured family/TTC face");
        Run(PreferredFonts, false);
    }
    // Force a primary file with missing coverage to exercise actual merge mode.
    setenv("NPGS_FONT", Primary.Path.c_str(), 1);
    const auto Fallback = FindSystemUIFonts(Isolated.get(), Translations);
    unsetenv("NPGS_FONT");
    Check(HasCharacter(Fallback, 'A'), "Fallback selection lost Latin coverage");
    if (!HasCharacter(OnlyPrimary, 0x4e2d) && HasCharacter(System, 0x4e2d))
    {
        Check(Fallback.Faces.size() > 1 && HasCharacter(Fallback, 0x4e2d),
              "System CJK fallback was not merged with the Latin primary");
        Run(Fallback, true);
    }
    bool NonzeroFace = false;
    for (const auto& Face : System.Faces)
        NonzeroFace |= Face.FaceIndex != 0;
    std::cout << (NonzeroFace ? "TTC nonzero face exercised\n" :
                               "SKIP: no selected system TTC nonzero face available\n");
}

int main(int Argc, char** Argv)
{
    try
    {
        FEnvironment Environment;
        IMGUI_CHECKVERSION();
        std::ifstream File(NPGS_TEST_TRANSLATIONS, std::ios::binary);
        Check(static_cast<bool>(File), "Cannot read actual translation asset");
        Translations.assign(std::istreambuf_iterator<char>(File), {});
        const auto Fonts = FindSystemUIFonts(nullptr, Translations);
        Check(Fonts.Faces.size() <= 16, "Selection exceeds the pinned ImGui source limit");
        for (const auto& Face : Fonts.Faces)
            std::cout << "Fontconfig source: " << Face.Path << " face=" << Face.FaceIndex << '\n';
        if (Argc > 1 && std::string(Argv[1]) == "--cjk")
        {
            if (!HasCharacter(Fonts, 0x4e2d) || !HasCharacter(Fonts, 0x6587) || !HasCharacter(Fonts, 0x8bbe))
            {
                std::cout << "SKIP: Fontconfig found no system CJK coverage; install a Chinese system font\n";
                return 77;
            }
            Run(Fonts, true);
        }
        else
        {
            CheckDiscovery(Fonts);
            Run(Fonts, false);
        }
    }
    catch (const std::exception& Error)
    {
        std::cerr << "ImGui CPU font regression: " << Error.what() << '\n';
        return 1;
    }
}
