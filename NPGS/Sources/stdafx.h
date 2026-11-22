#pragma once

#include "xstdafx.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <fast-cpp-csv-parser/csv.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spirv_cross/spirv_reflect.hpp>
#include <stb_image.h>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_format_traits.hpp>
#include <vulkan/vulkan_to_string.hpp>

#define NPGS_ENABLE_CONSOLE_LOGGER
#include "Engine/Utils/Logger.h"
