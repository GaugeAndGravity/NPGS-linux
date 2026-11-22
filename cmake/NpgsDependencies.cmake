include(FetchContent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NPGS_FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
find_package(Threads REQUIRED)
find_package(Vulkan 1.4 REQUIRED)

# Large/common dependencies must come from the distribution. Missing small
# dependencies below have pinned, SHA256-verified source-only fallbacks.
find_package(glm 1.0 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    pkg_check_modules(NPGS_JSON REQUIRED IMPORTED_TARGET nlohmann_json>=3.11)
    add_library(nlohmann_json::nlohmann_json ALIAS PkgConfig::NPGS_JSON)
endif()
find_package(assimp CONFIG QUIET)
if(NOT TARGET assimp::assimp)
    pkg_check_modules(NPGS_ASSIMP REQUIRED IMPORTED_TARGET assimp)
    add_library(assimp::assimp ALIAS PkgConfig::NPGS_ASSIMP)
endif()
find_path(NPGS_BOOST_INCLUDE_DIR boost/multiprecision/cpp_int.hpp REQUIRED)
pkg_check_modules(NPGS_STB QUIET IMPORTED_TARGET stb)
if(NPGS_STB_FOUND)
    set(_stb_target PkgConfig::NPGS_STB)
else()
    find_path(NPGS_STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)
    add_library(npgs_stb INTERFACE)
    target_include_directories(npgs_stb SYSTEM INTERFACE "${NPGS_STB_INCLUDE_DIR}")
    set(_stb_target npgs_stb)
endif()

function(npgs_fetch name repository revision sha256)
    string(TOUPPER "${name}" _upper)
    if(NOT NPGS_FETCH_MISSING_DEPENDENCIES AND NOT FETCHCONTENT_SOURCE_DIR_${_upper})
        message(FATAL_ERROR
            "Missing dependency ${name}. Install its development package, set "
            "FETCHCONTENT_SOURCE_DIR_${_upper}, or enable NPGS_FETCH_MISSING_DEPENDENCIES.")
    endif()
    message(STATUS "Using pinned ${name} source: ${repository}@${revision}")
    FetchContent_Declare("${name}"
        URL "https://codeload.github.com/${repository}/tar.gz/${revision}"
        URL_HASH "SHA256=${sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        # Header-only upstream projects sometimes configure bundled libraries
        # or tests; never run their CMake files implicitly.
        SOURCE_SUBDIR npgs-no-cmake
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable("${name}")
    set("${name}_SOURCE_DIR" "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    set("${name}_BINARY_DIR" "${${name}_BINARY_DIR}" PARENT_SCOPE)
    # Fetched code is compiled into npgs; retain its redistribution notices.
    # GLI carries its dual MIT license in manual.md rather than LICENSE.
    foreach(_notice LICENSE LICENSE.txt LICENSE.md COPYING copying.txt manual.md)
        if(EXISTS "${${name}_SOURCE_DIR}/${_notice}")
            install(FILES "${${name}_SOURCE_DIR}/${_notice}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/npgs/${name}"
                COMPONENT Runtime)
            break()
        endif()
    endforeach()
endfunction()

find_package(glfw3 3.4 CONFIG QUIET)
if(TARGET glfw)
    set(_glfw_target glfw)
else()
    pkg_check_modules(NPGS_GLFW QUIET IMPORTED_TARGET glfw3>=3.4)
    if(NPGS_GLFW_FOUND)
        set(_glfw_target PkgConfig::NPGS_GLFW)
    endif()
endif()

if(_glfw_target)
    # This query does not open a display and works in headless build hosts.
    # A version check alone cannot distinguish an X11-only GLFW package.
    if(NOT CMAKE_CROSSCOMPILING)
        include(CheckCXXSourceRuns)
        set(CMAKE_REQUIRED_LIBRARIES "${_glfw_target}")
        check_cxx_source_runs("
            #define GLFW_INCLUDE_NONE
            #include <GLFW/glfw3.h>
            int main() { return glfwPlatformSupported(GLFW_PLATFORM_WAYLAND) ? 0 : 1; }
        " NPGS_SYSTEM_GLFW_HAS_WAYLAND)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if(NOT NPGS_SYSTEM_GLFW_HAS_WAYLAND)
            message(FATAL_ERROR "System GLFW lacks native Wayland support. Rebuild GLFW >=3.4 with Wayland enabled.")
        endif()
    else()
        message(STATUS "Cross-compiling: GLFW Wayland support must be ensured by the toolchain")
    endif()
else()
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    npgs_fetch(npgs_glfw glfw/glfw 3.4
        c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01)
    add_subdirectory("${npgs_glfw_SOURCE_DIR}" "${npgs_glfw_BINARY_DIR}" EXCLUDE_FROM_ALL)
    set(_glfw_target glfw)
endif()

set(_compat_include "${PROJECT_BINARY_DIR}/generated/dependency-include")
file(MAKE_DIRECTORY "${_compat_include}")
add_library(npgs_headers INTERFACE)
target_include_directories(npgs_headers SYSTEM INTERFACE "${_compat_include}" "${NPGS_BOOST_INCLUDE_DIR}")

find_path(NPGS_CSV_INCLUDE_DIR NAMES fast-cpp-csv-parser/csv.h csv.h
    PATH_SUFFIXES fast-cpp-csv-parser)
if(NOT NPGS_CSV_INCLUDE_DIR)
    npgs_fetch(npgs_csv ben-strasser/fast-cpp-csv-parser 574a9fe4d323ba63416877a4a5fe59088d37aa34
        b43696c26ff9fe02629109daa9059dd208fdff1002fa2b2e5860a31570c35ca1)
    set(NPGS_CSV_INCLUDE_DIR "${npgs_csv_SOURCE_DIR}")
endif()
target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_CSV_INCLUDE_DIR}")
if(EXISTS "${NPGS_CSV_INCLUDE_DIR}/fast-cpp-csv-parser/csv.h")
    target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_CSV_INCLUDE_DIR}")
else()
    file(MAKE_DIRECTORY "${_compat_include}/fast-cpp-csv-parser")
    file(CONFIGURE OUTPUT "${_compat_include}/fast-cpp-csv-parser/csv.h"
        CONTENT "#include \"${NPGS_CSV_INCLUDE_DIR}/csv.h\"\n" @ONLY)
endif()

find_path(NPGS_VMA_INCLUDE_DIR NAMES vma/vk_mem_alloc.h vk_mem_alloc.h
    PATH_SUFFIXES vma VulkanMemoryAllocator)
if(NOT NPGS_VMA_INCLUDE_DIR)
    npgs_fetch(npgs_vma GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator v3.2.1
        5e7749504cb802427ffb7bec38a0b6a15db46ae253f00560acb3e624d9fe695c)
    set(NPGS_VMA_INCLUDE_DIR "${npgs_vma_SOURCE_DIR}/include")
endif()
target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_VMA_INCLUDE_DIR}")
if(EXISTS "${NPGS_VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h")
    target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_VMA_INCLUDE_DIR}")
else()
    file(MAKE_DIRECTORY "${_compat_include}/vma")
    file(CONFIGURE OUTPUT "${_compat_include}/vma/vk_mem_alloc.h"
        CONTENT "#include \"${NPGS_VMA_INCLUDE_DIR}/vk_mem_alloc.h\"\n" @ONLY)
endif()

find_path(NPGS_GLI_INCLUDE_DIR gli/gli.hpp)
if(NOT NPGS_GLI_INCLUDE_DIR)
    # Fixed post-0.8.2 commit; use the system GLM, never external/glm/.
    npgs_fetch(npgs_gli g-truc/gli 3542f8830178061e0661f5df1e89d36ba7d7b0ab
        46709eb842892dfde9dbd0fb113d1bd736eaf52f2e70eabe62583b1199aec15e)
    set(NPGS_GLI_INCLUDE_DIR "${npgs_gli_SOURCE_DIR}")
endif()
target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_GLI_INCLUDE_DIR}")

find_path(NPGS_MAGIC_ENUM_INCLUDE_DIR magic_enum/magic_enum.hpp)
if(NOT NPGS_MAGIC_ENUM_INCLUDE_DIR)
    npgs_fetch(npgs_magic_enum Neargye/magic_enum v0.9.7
        b403d3dad4ef542fdc3024fa37d3a6cedb4ad33c72e31b6d9bab89dcaf69edf7)
    set(NPGS_MAGIC_ENUM_INCLUDE_DIR "${npgs_magic_enum_SOURCE_DIR}/include")
endif()
target_include_directories(npgs_headers SYSTEM INTERFACE "${NPGS_MAGIC_ENUM_INCLUDE_DIR}")

find_package(spirv_cross_core CONFIG QUIET)
find_package(spirv_cross_glsl CONFIG QUIET)
find_package(spirv_cross_reflect CONFIG QUIET)
if(TARGET spirv-cross-reflect AND TARGET spirv-cross-glsl AND TARGET spirv-cross-core)
    message(STATUS "Using system SPIRV-Cross reflect, glsl, and core targets")
elseif(TARGET spirv-cross-reflect OR TARGET spirv-cross-glsl OR TARGET spirv-cross-core)
    # Mixing partial system exports with the same-named fetched targets would
    # either redefine targets or mix incompatible library versions.
    message(FATAL_ERROR
        "Incomplete system SPIRV-Cross package: reflect, glsl, and core targets "
        "are all required. Install the complete development package, or disable "
        "all three system lookups with CMAKE_DISABLE_FIND_PACKAGE_spirv_cross_core, "
        "CMAKE_DISABLE_FIND_PACKAGE_spirv_cross_glsl, and "
        "CMAKE_DISABLE_FIND_PACKAGE_spirv_cross_reflect set to ON to use the pinned source fallback.")
else()
    set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    npgs_fetch(npgs_spirv_cross KhronosGroup/SPIRV-Cross vulkan-sdk-1.4.304.1
        bd3614abfc83a52760e667536c945b6e136ab013efca72918f222403da081cab)
    add_subdirectory("${npgs_spirv_cross_SOURCE_DIR}" "${npgs_spirv_cross_BINARY_DIR}" EXCLUDE_FROM_ALL)
    file(CREATE_LINK "${npgs_spirv_cross_SOURCE_DIR}" "${_compat_include}/spirv_cross" SYMBOLIC)
endif()

# Distribution ImGui is accepted only if it contains both required backends
# and the pinned docking API. Mixing system core with fetched backends is unsafe.
pkg_check_modules(NPGS_IMGUI QUIET IMPORTED_TARGET imgui=1.93.0)
unset(NPGS_SYSTEM_IMGUI_HAS_BACKENDS CACHE)
if(NPGS_IMGUI_FOUND)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::NPGS_IMGUI "${_glfw_target}" Vulkan::Vulkan)
    check_cxx_source_compiles("
        #include <imgui.h>
        #include <imgui_impl_glfw.h>
        #include <imgui_impl_vulkan.h>
        #ifndef IMGUI_HAS_DOCK
        #error Docking support is required
        #endif
        static_assert(IMGUI_VERSION_NUM == 19300);
        int main() {
            ImGui_ImplVulkan_InitInfo info{};
            info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplGlfw_NewFrame(); ImGui_ImplVulkan_NewFrame();
            ImGui_ImplVulkan_UpdateTexture(nullptr);
            ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    " NPGS_SYSTEM_IMGUI_HAS_BACKENDS)
    unset(CMAKE_REQUIRED_LIBRARIES)
endif()
if(NPGS_SYSTEM_IMGUI_HAS_BACKENDS)
    set(_imgui_target PkgConfig::NPGS_IMGUI)
else()
    npgs_fetch(npgs_imgui ocornut/imgui v1.93.0-docking
        746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5)
    add_library(npgs_imgui STATIC
        "${npgs_imgui_SOURCE_DIR}/imgui.cpp"
        "${npgs_imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${npgs_imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${npgs_imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${npgs_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${npgs_imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
    target_include_directories(npgs_imgui SYSTEM PUBLIC
        "${npgs_imgui_SOURCE_DIR}" "${npgs_imgui_SOURCE_DIR}/backends")
    # Include the pin in compile commands so an in-place FetchContent upgrade
    # also invalidates objects when archive source timestamps precede them.
    target_compile_definitions(npgs_imgui PRIVATE
        GLFW_INCLUDE_NONE IMGUI_IMPL_GLFW_DISABLE_X11 NPGS_IMGUI_VERSION_NUM=19300)
    target_link_libraries(npgs_imgui PUBLIC "${_glfw_target}" Vulkan::Vulkan)
    set(_imgui_target npgs_imgui)
endif()

add_library(npgs_dependencies INTERFACE)
target_link_libraries(npgs_dependencies INTERFACE
    Vulkan::Vulkan "${_glfw_target}" "${_imgui_target}" npgs_headers
    glm::glm fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json
    assimp::assimp "${_stb_target}"
    # Static SPIRV-Cross exports do not consistently propagate these dependencies.
    # Keep consumers before providers for GNU-style archive linking.
    spirv-cross-reflect spirv-cross-glsl spirv-cross-core
    Threads::Threads PkgConfig::NPGS_FONTCONFIG
    "${CMAKE_DL_LIBS}")
