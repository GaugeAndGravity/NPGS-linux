#pragma once

#include <utility>

#define NPGS_INLINE inline

// Basic namespace defines
// -----------------------
#define _ASSET_BEGIN namespace Asset {
#define _ASSET_END }
#define _ASTRO_BEGIN namespace Astro {
#define _ASTRO_END }
#define _CONFIG_BEGIN namespace Config {
#define _CONFIG_END }
#define _GENERATOR_BEGIN namespace Generator {
#define _GENERATOR_END }
#define _GRAPHICS_BEGIN namespace Graphics {
#define _GRAPHICS_END }
#define _INTELLI_BEGIN namespace Intelli {
#define _INTELLI_END }
#define _MATH_BEGIN namespace Math {
#define _MATH_END }
#define _NPGS_BEGIN namespace Npgs {
#define _NPGS_END }
#define _RUNTIME_BEGIN namespace Runtime {
#define _RUNTIME_END }
#define _SPATIAL_BEGIN namespace Spatial {
#define _SPATIAL_END }
#define _UI_BEGIN namespace UI {
#define _UI_END }
#define _SYSTEM_BEGIN namespace System {
#define _SYSTEM_END }
#define _THREAD_BEGIN namespace Thread {
#define _THREAD_END }
#define _UTIL_BEGIN namespace Util {
#define _UTIL_END }

// Bit operator function
// ---------------------
#define Bit(x) (1ULL << x)

// NpgsBind
// --------
#define NpgsBindMemberFunc(Func) [this](auto&&... Args) -> decltype(auto) \
{                                                                         \
    return this->Func(std::forward<decltype(Args)>(Args)...);             \
}

#define NpgsBind(Func) [](auto&&... Args) -> decltype(auto) \
{                                                           \
    return Func(std::forward<decltype(Args)>(Args)...);     \
}
