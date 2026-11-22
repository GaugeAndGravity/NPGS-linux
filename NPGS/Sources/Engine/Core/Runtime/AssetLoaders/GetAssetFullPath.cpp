#include "GetAssetFullPath.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>

_NPGS_BEGIN
_RUNTIME_BEGIN
_ASSET_BEGIN

namespace
{
namespace fs = std::filesystem;

fs::path ExecutableDirectory()
{
    std::error_code Error;
    fs::path Executable = fs::read_symlink("/proc/self/exe", Error);
    if (Error)
        throw std::runtime_error("Cannot locate NPGS executable via /proc/self/exe: " + Error.message());
    return Executable.parent_path();
}

fs::path ValidateRelativePath(const std::string& Name)
{
    fs::path Path(Name);
    if (Path.is_absolute() || Path.has_root_name())
        throw std::invalid_argument("Asset path must be relative to Assets: " + Name);
    for (const auto& Part : Path)
        if (Part == "..")
            throw std::invalid_argument("Asset path must not escape Assets: " + Name);
    return Path;
}

std::string ResolveAssetRoot()
{
    std::error_code Error;
    if (const char* Override = std::getenv("NPGS_ASSET_DIR"))
    {
        fs::path Root(Override);
        if (Root.empty() || !Root.is_absolute() || !fs::is_directory(Root, Error))
            throw std::runtime_error("NPGS_ASSET_DIR must name an existing absolute Assets directory: '" +
                                     std::string(Override) + "'" + (Error ? " (" + Error.message() + ")" : ""));
        return fs::canonical(Root).string();
    }

    const fs::path ExeDir = ExecutableDirectory();
    // Build executables may be in <build> or <build>/bin. Installed ones are
    // in <prefix>/bin; neither discovery path depends on the process cwd.
    const fs::path Candidates[] = {
#ifdef NPGS_RESOURCE_RELATIVE_DIR
        ExeDir / NPGS_RESOURCE_RELATIVE_DIR,
#endif
        ExeDir.parent_path() / "share/npgs/Assets",
        ExeDir / "share/npgs/Assets"
    };
    std::string Searched;
    for (const auto& Candidate : Candidates)
    {
        if (!Searched.empty()) Searched += ", ";
        Searched += Candidate.lexically_normal().string();
        Error.clear();
        if (fs::is_directory(Candidate, Error))
            return fs::canonical(Candidate).string();
    }
#ifdef NPGS_INSTALL_ASSET_DIR
    const fs::path InstallAssets(NPGS_INSTALL_ASSET_DIR);
    Searched += ", " + InstallAssets.string();
    Error.clear();
    if (InstallAssets.is_absolute() && fs::is_directory(InstallAssets, Error))
        return fs::canonical(InstallAssets).string();
#endif
#ifdef NPGS_BUILD_ASSET_DIR
    const fs::path BuildAssets(NPGS_BUILD_ASSET_DIR);
    const fs::path BuildRoot = BuildAssets.parent_path().parent_path().parent_path();
    // Never let a broken installed package borrow a developer's build assets.
    if (ExeDir == BuildRoot || ExeDir == BuildRoot / "bin")
    {
        Searched += ", " + BuildAssets.string();
        Error.clear();
        if (BuildAssets.is_absolute() && fs::is_directory(BuildAssets, Error))
            return fs::canonical(BuildAssets).string();
    }
#endif
    throw std::runtime_error("NPGS assets not found. Searched: " + Searched +
                             ". Install the asset directory or set NPGS_ASSET_DIR to an absolute Assets directory.");
}

fs::path ShaderCacheDirectory()
{
    fs::path Root;
    if (const char* CacheHome = std::getenv("XDG_CACHE_HOME"); CacheHome && *CacheHome)
    {
        Root = CacheHome;
        if (!Root.is_absolute())
            throw std::runtime_error("XDG_CACHE_HOME must be absolute");
    }
    else if (const char* Home = std::getenv("HOME"); Home && *Home)
    {
        Root = fs::path(Home) / ".cache";
        if (!Root.is_absolute())
            throw std::runtime_error("HOME must be absolute to locate the NPGS shader cache");
    }
    else
        throw std::runtime_error("Set XDG_CACHE_HOME or HOME to locate the writable NPGS shader cache");

    Root /= "npgs/Shaders";
    std::error_code Error;
    fs::create_directories(Root, Error);
    if (Error)
        throw std::runtime_error("Cannot create NPGS shader cache '" + Root.string() + "': " + Error.message());
    return Root;
}
}

std::string GetAssetRoot()
{
    static const std::string Root = ResolveAssetRoot();
    return Root;
}

std::string GetAssetPath(const std::string& RelativePath)
{
    return (fs::path(GetAssetRoot()) / ValidateRelativePath(RelativePath)).string();
}

std::string GetAssetFullPath(EAssetType Type, const std::string& Filename)
{
    const fs::path Relative = ValidateRelativePath(Filename);
    switch (Type)
    {
    case EAssetType::kBinaryShader:
        return (ShaderCacheDirectory() / Relative).string();
    case EAssetType::kDataTable:
        return GetAssetPath((fs::path("DataTables") / Relative).string());
    case EAssetType::kFont:
        return GetAssetPath((fs::path("Fonts") / Relative).string());
    case EAssetType::kModel:
        return GetAssetPath((fs::path("Models") / Relative).string());
    case EAssetType::kShader:
        return GetAssetPath((fs::path("Shaders") / Relative).string());
    case EAssetType::kTexture:
        return GetAssetPath((fs::path("Textures") / Relative).string());
    default:
        throw std::invalid_argument("Invalid NPGS asset type");
    }
}

_ASSET_END
_RUNTIME_END
_NPGS_END
