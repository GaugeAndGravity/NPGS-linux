# Stage each file as a real build output. Never use checked-out .spv binaries.
set(_asset_root "${PROJECT_SOURCE_DIR}/NPGS/Assets")
file(GLOB_RECURSE _assets CONFIGURE_DEPENDS LIST_DIRECTORIES false
    RELATIVE "${_asset_root}" "${_asset_root}/*")
list(FILTER _assets EXCLUDE REGEX "^Shaders/")
# Models are reference assets; runtime resources are staged below.
list(FILTER _assets EXCLUDE REGEX "^Models/")
set(_resource_outputs)
set(_manifest "")
foreach(_asset IN LISTS _assets)
    set(_output "${NPGS_RESOURCE_DIR}/${_asset}")
    get_filename_component(_directory "${_output}" DIRECTORY)
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_directory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_asset_root}/${_asset}" "${_output}"
        # copy_if_different may retain a timestamp older than a touched input.
        COMMAND "${CMAKE_COMMAND}" -E touch "${_output}"
        DEPENDS "${_asset_root}/${_asset}" VERBATIM)
    list(APPEND _resource_outputs "${_output}")
    # Manifest entries are relative to <build>/share/npgs/, as used by tests.
    string(APPEND _manifest "Assets/${_asset}\n")
    get_filename_component(_relative_directory "${_asset}" DIRECTORY)
    install(FILES "${_asset_root}/${_asset}"
        DESTINATION "${NPGS_INSTALL_RESOURCE_SUBDIR}/${_relative_directory}"
        COMPONENT Resources)
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/npgs" COMPONENT Resources)
add_custom_target(npgs_resources ALL DEPENDS ${_resource_outputs})

set(_shader_root "${PROJECT_SOURCE_DIR}/NPGS/Sources/Engine/Shaders")
set(_shader_config "${PROJECT_SOURCE_DIR}/NPGS/Tools/ShaderCompiler/CompileShaders.cfg")
find_program(NPGS_GLSLC NAMES glslc)
if(NOT NPGS_GLSLC)
    find_program(NPGS_GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
endif()

function(npgs_compile_shader source output)
    if(NOT EXISTS "${_shader_root}/${source}")
        message(FATAL_ERROR "Shader configuration references missing source: ${source}")
    endif()
    if(NOT source MATCHES "\\.(vert|frag|comp|geom|tesc|tese|mesh|task|rgen|rchit|rahit|rmiss|rint|rcall)\\.glsl$")
        message(FATAL_ERROR "Cannot determine shader stage: ${source}")
    endif()
    set(_stage "${CMAKE_MATCH_1}")
    set(_output "${NPGS_SHADER_DIR}/${output}")
    get_filename_component(_directory "${_output}" DIRECTORY)
    set(_defines)
    foreach(_define IN LISTS ARGN)
        list(APPEND _defines "-D${_define}")
    endforeach()
    if(NPGS_GLSLC)
        set(_command "${NPGS_GLSLC}" --target-env=vulkan1.4 -O
            "-fshader-stage=${_stage}" "-I${_shader_root}" ${_defines}
            -MD -MF "${_output}.d" -MT "${_output}"
            "${_shader_root}/${source}" -o "${_output}")
    else()
        # glslang requires an explicit include extension, whereas upstream uses
        # glslc's implicit include support. Inject only a preamble, not source edits.
        set(_command "${NPGS_GLSLANG_VALIDATOR}" -V --target-env vulkan1.4 -Os
            -S "${_stage}" "-I${_shader_root}" ${_defines}
            --preamble-text "#extension GL_GOOGLE_include_directive : require"
            --depfile "${_output}.d" "${_shader_root}/${source}" -o "${_output}")
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_directory}"
        COMMAND ${_command}
        DEPENDS "${_shader_root}/${source}" "${_shader_config}"
        DEPFILE "${_output}.d"
        COMMENT "GLSL -> SPIR-V: ${output}"
        VERBATIM COMMAND_EXPAND_LISTS)
    set_property(GLOBAL APPEND PROPERTY NPGS_SHADER_OUTPUTS "${_output}")
    set_property(GLOBAL APPEND PROPERTY NPGS_SHADER_NAMES "Assets/Shaders/${output}")
    get_filename_component(_relative_directory "${output}" DIRECTORY)
    install(FILES "${_output}"
        DESTINATION "${NPGS_INSTALL_RESOURCE_SUBDIR}/Shaders/${_relative_directory}"
        COMPONENT Resources)
endfunction()

# Preserve precisely the same variants/macros as the upstream compiler config.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_shader_config}")
file(STRINGS "${_shader_config}" _variant_lines)
set(_configured_sources)
foreach(_line IN LISTS _variant_lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^//")
        continue()
    endif()
    separate_arguments(_tokens UNIX_COMMAND "${_line}")
    list(LENGTH _tokens _count)
    if(_count LESS 2)
        message(FATAL_ERROR "Invalid shader variant: ${_line}")
    endif()
    list(POP_FRONT _tokens _source _output)
    npgs_compile_shader("${_source}" "${_output}" ${_tokens})
    list(APPEND _configured_sources "${_source}")
endforeach()

# Common/*.glsl and BlackHole_common.glsl are include dependencies, not stages.
file(GLOB_RECURSE _shader_files CONFIGURE_DEPENDS
    RELATIVE "${_shader_root}" "${_shader_root}/*.glsl")
set(_legacy_shader_stages BlackHole.frag.glsl BlackHole.comp.glsl)
message(STATUS "Legacy shader stages excluded: ${_legacy_shader_stages} (unused single-pass paths; sources preserved)")
foreach(_legacy IN LISTS _legacy_shader_stages)
    string(REGEX REPLACE "\\.glsl$" ".spv" _legacy_output "${_legacy}")
    # Remove only our generated legacy artifacts from an existing build tree.
    file(REMOVE "${NPGS_SHADER_DIR}/${_legacy_output}" "${NPGS_SHADER_DIR}/${_legacy_output}.d")
endforeach()
foreach(_source IN LISTS _shader_files)
    # The application uses prepass + composite, not these retired single-pass
    # implementations. Their old interfaces are incompatible with the current
    # common physics/Uniform ABI; do not change that ABI merely to build them.
    if(_source IN_LIST _legacy_shader_stages)
        continue()
    endif()
    if(_source IN_LIST _configured_sources)
        continue()
    endif()
    if(_source MATCHES "\\.(vert|frag|comp|geom|tesc|tese|mesh|task|rgen|rchit|rahit|rmiss|rint|rcall)\\.glsl$")
        string(REGEX REPLACE "\\.glsl$" ".spv" _output "${_source}")
        npgs_compile_shader("${_source}" "${_output}")
    endif()
endforeach()
get_property(_shader_outputs GLOBAL PROPERTY NPGS_SHADER_OUTPUTS)
get_property(_shader_names GLOBAL PROPERTY NPGS_SHADER_NAMES)
add_custom_target(npgs_shaders ALL DEPENDS ${_shader_outputs})
foreach(_name IN LISTS _shader_names)
    string(APPEND _manifest "${_name}\n")
endforeach()
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/npgs-resource-manifest.txt"
    CONTENT "${_manifest}" @ONLY)
list(LENGTH _assets _asset_count)
list(LENGTH _shader_outputs _shader_count)
message(STATUS "Runtime data: ${_asset_count} assets and ${_shader_count} compiled shader variants")
message(STATUS "Build resources: ${NPGS_RESOURCE_DIR}")
