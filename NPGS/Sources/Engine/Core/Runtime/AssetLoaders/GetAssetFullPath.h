#pragma once

#include <string>
#include "Engine/Core/Base/Base.h"

_NPGS_BEGIN
_RUNTIME_BEGIN
_ASSET_BEGIN

enum class EAssetType
{
    kBinaryShader, // 二进制着色器程序（不是 SPIR-V）
    kDataTable,    // 数据表
    kFont,         // 字体
    kModel,        // 模型
    kShader,       // 着色器
    kTexture       // 纹理
};

// Absolute, executable-relative asset root. NPGS_ASSET_DIR overrides discovery;
// an invalid explicit override throws std::runtime_error (never falls back).
std::string GetAssetRoot();

// Resolve a path relative to Assets (e.g. "UI/main_menu.json").
// Does not require the final path to exist: callers also use directory prefixes.
std::string GetAssetPath(const std::string& RelativePath);

std::string GetAssetFullPath(EAssetType Type, const std::string& Filename);

_ASSET_END
_RUNTIME_END
_NPGS_END
