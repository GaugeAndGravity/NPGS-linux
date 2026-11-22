#include "SystemFonts.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <fontconfig/fcfreetype.h>

namespace Npgs::System::UI
{
namespace
{
template <typename T, void (*Destroy)(T*)>
using FOwner = std::unique_ptr<T, decltype(Destroy)>;
using FPattern = FOwner<FcPattern, FcPatternDestroy>;
using FCharset = FOwner<FcCharSet, FcCharSetDestroy>;

FCharset RequiredCharacters(std::string_view TranslationText)
{
    FCharset Required(FcCharSetUnion(
        FcLangGetCharSet(reinterpret_cast<const FcChar8*>("zh-cn")),
        FcLangGetCharSet(reinterpret_cast<const FcChar8*>("en"))), FcCharSetDestroy);
    // Standard language coverage plus actual translations and UI symbols.
    // This is a discovery request, NOT an eagerly rasterized glyph range.
    for (char32_t Codepoint : U"—“”…∮⌘▶◈☼の！（），：；？")
        if (Codepoint) FcCharSetAddChar(Required.get(), Codepoint);
    while (!TranslationText.empty())
    {
        FcChar32 Codepoint;
        const int Length = FcUtf8ToUcs4(reinterpret_cast<const FcChar8*>(TranslationText.data()),
                                       &Codepoint, static_cast<int>(TranslationText.size()));
        if (Length <= 0) throw std::runtime_error("UI translations contain invalid UTF-8");
        if (Codepoint >= 0x20) FcCharSetAddChar(Required.get(), Codepoint);
        TranslationText.remove_prefix(Length);
    }
    return Required;
}

FPattern QueryFace(const std::string& Path, int FaceIndex)
{
    // FreeType/Fontconfig validates the file; do not duplicate font parsers.
    std::error_code Error;
    if (!std::filesystem::is_regular_file(Path, Error) || !std::ifstream(Path, std::ios::binary))
        return FPattern(nullptr, FcPatternDestroy);
    int FaceCount = 0;
    FPattern Face(FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(Path.c_str()),
                                FaceIndex, nullptr, &FaceCount), FcPatternDestroy);
    FcBool Outline = FcFalse;
    FcChar8* Format = nullptr;
    if (!Face || FcPatternGetBool(Face.get(), FC_OUTLINE, 0, &Outline) != FcResultMatch ||
        !Outline || FcPatternGetString(Face.get(), FC_FONTFORMAT, 0, &Format) != FcResultMatch ||
        (std::string(reinterpret_cast<const char*>(Format)) != "TrueType" &&
         std::string(reinterpret_cast<const char*>(Format)) != "CFF"))
        return FPattern(nullptr, FcPatternDestroy);
    return Face;
}

FPattern MakeRequest(FcConfig* Config, const FcCharSet* Required = nullptr)
{
    FPattern Request(FcPatternCreate(), FcPatternDestroy);
    FcPatternAddString(Request.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>("sans-serif"));
    FcPatternAddBool(Request.get(), FC_SCALABLE, FcTrue);
    FcPatternAddBool(Request.get(), FC_OUTLINE, FcTrue);
    FcPatternAddInteger(Request.get(), FC_WEIGHT, FC_WEIGHT_REGULAR);
    if (Required)
    {
        FcPatternAddCharSet(Request.get(), FC_CHARSET, Required);
        // Both shipped UI languages must work independently of desktop locale.
        FcPatternAddString(Request.get(), FC_LANG, reinterpret_cast<const FcChar8*>("zh-cn"));
        FcPatternAddString(Request.get(), FC_LANG, reinterpret_cast<const FcChar8*>("en"));
    }
    FcConfigSubstitute(Config, Request.get(), FcMatchPattern);
    FcDefaultSubstitute(Request.get());
    return Request;
}

bool ReadFace(FcPattern* Pattern, FSystemFontFace& Face)
{
    FcChar8* Path = nullptr;
    int Index = 0;
    if (FcPatternGetString(Pattern, FC_FILE, 0, &Path) != FcResultMatch ||
        FcPatternGetInteger(Pattern, FC_INDEX, 0, &Index) != FcResultMatch)
        return false;
    // Fontconfig's upper bits identify variation instances, not TTC faces.
    // ImGui/stb uses the base outline of that face.
    Face = {reinterpret_cast<const char*>(Path), Index & 0xffff};
    return true;
}
}

FSystemUIFonts FindSystemUIFonts(FcConfig* Config, std::string_view TranslationText)
{
    FOwner<FcConfig, FcConfigDestroy> OwnedConfig(
        Config ? nullptr : FcInitLoadConfigAndFonts(), FcConfigDestroy);
    if (!Config) Config = OwnedConfig.get();
    if (!Config) throw std::runtime_error("Cannot load system Fontconfig configuration");
    FSystemUIFonts Fonts;
    auto Missing = RequiredCharacters(TranslationText);
    auto AddFace = [&](const FSystemFontFace& Face, bool Primary)
    {
        auto Pattern = QueryFace(Face.Path, Face.FaceIndex);
        FcCharSet* Charset = nullptr;
        if (!Pattern || FcPatternGetCharSet(Pattern.get(), FC_CHARSET, 0, &Charset) != FcResultMatch)
            return false;
        if (!Primary && FcCharSetIntersectCount(Missing.get(), Charset) == 0)
            return false;
        Fonts.Faces.push_back(Face);
        Missing.reset(FcCharSetSubtract(Missing.get(), Charset));
        return true;
    };

    if (const char* Override = std::getenv("NPGS_FONT"))
    {
        if (!std::filesystem::path(Override).is_absolute() || !AddFace({Override, 0}, true))
            throw std::runtime_error("NPGS_FONT must name a valid, readable absolute TTF/OTF/TTC font: '" +
                                     std::string(Override) + "'");
    }
    else
    {
        auto Request = MakeRequest(Config);
        FcResult Result;
        FPattern Match(FcFontMatch(Config, Request.get(), &Result), FcPatternDestroy);
        FSystemFontFace Face;
        if (Match && ReadFace(Match.get(), Face)) AddFace(Face, true);
    }

    auto Request = MakeRequest(Config, Missing.get());
    FcResult Result;
    FOwner<FcFontSet, FcFontSetDestroy> Sorted(
        FcFontSort(Config, Request.get(), FcTrue, nullptr, &Result), FcFontSetDestroy);
    if (Sorted)
        // ImFontGlyph::SourceIdx is four bits in the pinned official API.
        for (int Index = 0; Index < Sorted->nfont && FcCharSetCount(Missing.get()) &&
                            Fonts.Faces.size() < 16; ++Index)
        {
            FSystemFontFace Face;
            if (ReadFace(Sorted->fonts[Index], Face))
                AddFace(Face, Fonts.Faces.empty());
        }
    if (Fonts.Faces.empty())
        throw std::runtime_error("No usable system outline fonts found by Fontconfig. "
                                 "Install a system sans-serif font and a Chinese font "
                                 "(Noto Sans CJK / Source Han Sans), or set NPGS_FONT.");

    FcChar32 Map[FC_CHARSET_MAP_SIZE], Next;
    for (FcChar32 Page = FcCharSetFirstPage(Missing.get(), Map, &Next);
         Page != FC_CHARSET_DONE; Page = FcCharSetNextPage(Missing.get(), Map, &Next))
        for (unsigned Word = 0; Word < FC_CHARSET_MAP_SIZE; ++Word)
            for (unsigned Bit = 0; Bit < 32; ++Bit)
                if (Map[Word] & (FcChar32(1) << Bit))
                    Fonts.MissingCharacters.push_back(Page + Word * 32 + Bit);
    return Fonts;
}

ImFont* AddSystemUIFont(ImFontAtlas& Atlas, const FSystemUIFonts& Fonts, float Size)
{
    if (Fonts.Faces.empty()) throw std::invalid_argument("No system UI font sources");
    if (Fonts.Faces.size() > 16) throw std::invalid_argument("Too many ImGui font sources");
    ImFont* Font = nullptr;
    for (const auto& Face : Fonts.Faces)
    {
        ImFontConfig Config;
        Config.FontNo = Face.FaceIndex;
        Config.PixelSnapH = true;
        Config.OversampleH = Config.OversampleV = 1;
        Config.MergeMode = Font != nullptr;
        Config.DstFont = Font;
        ImFont* Added = Atlas.AddFontFromFileTTF(Face.Path.c_str(), Size, &Config);
        if (!Added) throw std::runtime_error("Cannot load system UI font '" + Face.Path + "'");
        if (!Font) Font = Added;
    }
    return Font;
}
}
