cmake_minimum_required(VERSION 3.28)
project(NPGS VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NPGS supports Linux only (Vulkan and native Wayland).")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build, e.g. cmake --preset linux-debug.")
endif()

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
option(NPGS_BUILD_APPLICATION "Build the application (OFF permits resource/shader-only checks)" ON)
option(NPGS_FETCH_MISSING_DEPENDENCIES "Download pinned source dependencies missing from the system" ON)
option(NPGS_USE_PCH "Precompile and force-include the upstream stdafx.h" ON)
option(NPGS_TEST_WAYLAND "Opt in to CTest opening a native Wayland window and testing resize" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# All translation units are ordinary headers/sources, not C++ modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Both trees deliberately share the bin/ + share/npgs/ layout.
set(NPGS_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/share/npgs/Assets")
set(NPGS_SHADER_DIR "${NPGS_RESOURCE_DIR}/Shaders")
set(NPGS_INSTALL_RESOURCE_SUBDIR "${CMAKE_INSTALL_DATADIR}/npgs/Assets")
set(NPGS_INSTALL_RESOURCE_DIR "${CMAKE_INSTALL_FULL_DATADIR}/npgs/Assets")
file(RELATIVE_PATH NPGS_RESOURCE_RELATIVE_DIR
    "${CMAKE_INSTALL_FULL_BINDIR}" "${NPGS_INSTALL_RESOURCE_DIR}")
include(NpgsResources)

if(NPGS_BUILD_APPLICATION)
    include(NpgsDependencies)
    # Application translation units; tools and shader includes are separate.
    set(npgs_sources
        Engine/Core/Runtime/AssetLoaders/AssetManager.cpp
        Engine/Core/Runtime/AssetLoaders/GetAssetFullPath.cpp
        Engine/Core/Runtime/AssetLoaders/Shader.cpp
        Engine/Core/Runtime/AssetLoaders/Texture.cpp
        Engine/Core/Runtime/Graphics/Renderers/PipelineManager.cpp
        Engine/Core/Runtime/Graphics/Vulkan/Context.cpp
        Engine/Core/Runtime/Graphics/Vulkan/Core.cpp
        Engine/Core/Runtime/Graphics/Vulkan/ExtFunctionsImpl.cpp
        Engine/Core/Runtime/Graphics/Vulkan/Resources.cpp
        Engine/Core/Runtime/Graphics/Vulkan/ShaderResourceManager.cpp
        Engine/Core/Runtime/Graphics/Vulkan/Wrappers.cpp
        Engine/Core/Runtime/Graphics/Vulkan/VulkanUIRenderer.cpp
        Engine/Core/Runtime/Threads/ThreadPool.cpp
        Engine/Core/System/Generators/CivilizationGenerator.cpp
        Engine/Core/System/Generators/OrbitalGenerator.cpp
        Engine/Core/System/Generators/StellarGenerator.cpp
        Engine/Core/System/Spatial/Camera.cpp
        Engine/Core/System/UI/components/CelestialInfoPanel.cpp
        Engine/Core/System/UI/components/CinematicInfoPanel.cpp
        Engine/Core/System/UI/components/CollapsedMainButton.cpp
        Engine/Core/System/UI/components/GlobalTooltip.cpp
        Engine/Core/System/UI/components/HackerTextHelper.cpp
        Engine/Core/System/UI/components/InputField.cpp
        Engine/Core/System/UI/components/LogPanel.cpp
        Engine/Core/System/UI/components/NeuralMenu.cpp
        Engine/Core/System/UI/components/NeuralParticleView.cpp
        Engine/Core/System/UI/components/PulsarButton.cpp
        Engine/Core/System/UI/components/ScreenVignette.cpp
        Engine/Core/System/UI/components/TechBorderPanel.cpp
        Engine/Core/System/UI/components/TechButton.cpp
        Engine/Core/System/UI/components/TechInteractivePanel.cpp
        Engine/Core/System/UI/components/TechText.cpp
        Engine/Core/System/UI/components/TimeControlPanel.cpp
        Engine/Core/System/UI/ScreenManager.cpp
        Engine/Core/System/UI/Screens/GameScreen.cpp
        Engine/Core/System/UI/Screens/MainMenuScreen.cpp
        Engine/Core/System/UI/UILoader.cpp
        Engine/Core/System/UI/UITool/AstroDataBuilder.cpp
        Engine/Core/System/UI/ui_framework.cpp
        Engine/Core/System/UI/SystemFonts.cpp
        Engine/Core/System/UI/I18n/I18nManager.cpp
        Engine/Core/Types/Entries/Astro/CelestialObject.cpp
        Engine/Core/Types/Entries/Astro/Planet.cpp
        Engine/Core/Types/Entries/Astro/Star.cpp
        Engine/Core/Types/Entries/Astro/StellarSystem.cpp
        Engine/Core/Types/Entries/NpgsObject.cpp
        Engine/Core/Types/Properties/Intelli/Artifact.cpp
        Engine/Core/Types/Properties/Intelli/Civilization.cpp
        Engine/Core/Types/Properties/StellarClass.cpp
        Engine/Utils/Logger.cpp
        Engine/Utils/TooolfuncForStarMap.cpp
        Engine/Utils/Utils.cpp
        ExternalImpl/stb_impl.cpp
        ExternalImpl/vma_impl.cpp
        Program/Application.cpp
        Program/main.cpp
        Program/Universe.cpp
        stdafx.cpp
    )
    list(TRANSFORM npgs_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources/")
    add_executable(npgs ${npgs_sources})
    set_target_properties(npgs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    target_include_directories(npgs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources")
    target_link_libraries(npgs PRIVATE npgs_dependencies)
    target_compile_definitions(npgs PRIVATE
        GLM_ENABLE_EXPERIMENTAL GLM_FORCE_INTRINSICS GLM_FORCE_ALIGNED_GENTYPES
        NPGS_BUILD_ASSET_DIR="${NPGS_RESOURCE_DIR}"
        NPGS_INSTALL_ASSET_DIR="${NPGS_INSTALL_RESOURCE_DIR}"
        NPGS_RESOURCE_RELATIVE_DIR="${NPGS_RESOURCE_RELATIVE_DIR}")
    if(NPGS_USE_PCH)
        target_precompile_headers(npgs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources/stdafx.h")
    else()
        # The original project relied on forced inclusion even with no PCH cache.
        foreach(_source IN LISTS npgs_sources)
            if(NOT _source MATCHES "/ExternalImpl/")
                set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_OPTIONS
                    -include "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources/stdafx.h")
            endif()
        endforeach()
    endif()
    # Implementation macros must take effect before these headers are first seen.
    set_source_files_properties(
        NPGS/Sources/ExternalImpl/stb_impl.cpp
        NPGS/Sources/ExternalImpl/vma_impl.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    add_dependencies(npgs npgs_resources npgs_shaders)
    install(TARGETS npgs RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
endif()

include(CTest)
if(BUILD_TESTING)
    if(NPGS_BUILD_APPLICATION)
        add_executable(npgs-scroll-test tests/test_scroll.cpp
            NPGS/Sources/Engine/Core/System/Spatial/Camera.cpp)
        target_include_directories(npgs-scroll-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources")
        target_link_libraries(npgs-scroll-test PRIVATE glm::glm)
        add_test(NAME npgs-scroll COMMAND npgs-scroll-test)
        set_tests_properties(npgs-scroll PROPERTIES LABELS "cpu;input" TIMEOUT 30)

        add_executable(npgs-imgui-font-test tests/test_imgui_fonts.cpp
            NPGS/Sources/Engine/Core/System/UI/SystemFonts.cpp)
        target_include_directories(npgs-imgui-font-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Sources")
        target_compile_definitions(npgs-imgui-font-test PRIVATE
            NPGS_TEST_TRANSLATIONS="${CMAKE_CURRENT_SOURCE_DIR}/NPGS/Assets/Lang/translations.json")
        target_link_libraries(npgs-imgui-font-test PRIVATE "${_imgui_target}" PkgConfig::NPGS_FONTCONFIG)
        add_test(NAME npgs-imgui-fonts COMMAND npgs-imgui-font-test)
        add_test(NAME npgs-imgui-cjk COMMAND npgs-imgui-font-test --cjk)
        set_tests_properties(npgs-imgui-cjk PROPERTIES LABELS "cpu;ui" TIMEOUT 30 SKIP_RETURN_CODE 77)
        set_tests_properties(npgs-imgui-fonts PROPERTIES LABELS "cpu;ui" TIMEOUT 30)
    endif()
    add_test(NAME npgs-resource-layout COMMAND "${CMAKE_COMMAND}"
        "-DRESOURCE_DIR=${CMAKE_CURRENT_BINARY_DIR}/share/npgs"
        "-DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/npgs-resource-manifest.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckResources.cmake")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_spirv.py")
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        find_program(NPGS_SPIRV_VAL NAMES spirv-val)
        if(NPGS_SPIRV_VAL)
            add_test(NAME npgs-spirv COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_spirv.py" "${CMAKE_CURRENT_BINARY_DIR}"
                --spirv-val "${NPGS_SPIRV_VAL}")
        else()
            message(WARNING "spirv-val was not found: the npgs-spirv CTest is unavailable. Install SPIRV-Tools to enable shader validation.")
        endif()
        add_test(NAME npgs-acceptance-selftest COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_acceptance.py")
        if(NPGS_BUILD_APPLICATION)
            add_test(NAME npgs-cli COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.py" "$<TARGET_FILE:npgs>")
            if(NPGS_TEST_WAYLAND)
                add_test(NAME npgs-wayland COMMAND "${Python3_EXECUTABLE}" -B
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_wayland.py"
                    "$<TARGET_FILE:npgs>" --smoke-resize)
                set_tests_properties(npgs-wayland PROPERTIES LABELS "wayland;runtime" TIMEOUT 150)
            endif()
        endif()
    endif()
endif()
