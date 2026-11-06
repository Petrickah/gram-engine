# --- shaders ---
# HLSL -> SPIR-V at build time with the Vulkan SDK's dxc (the SDK ships it on Windows, Linux
# and macOS; vcpkg's directx-dxc port has no macOS support). SPIR-V is platform-independent, so
# the host's dxc is the right one even when cross-compiling for Windows. Output lands in
# <top-level build dir>/shaders/, which gram copies next to the built executable.
# Stopgap until gram grows a dedicated shader module kind.
# Plain find_program instead of FindVulkan's dxc component: that one also requires the dxcompiler
# library for the *target* platform, which a mingw cross-build doesn't have (and doesn't need).
# NO_CMAKE_FIND_ROOT_PATH keeps the search on the host when cross-compiling.
find_program(GRAM_DXC_EXECUTABLE dxc
    HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)

# gram_add_hlsl_raster_shader(<target> <source>): called by the module that owns the shader (the game,
# for its own content), after add_subdirectory(GramEngine). Compiles <source> (a .hlsl path,
# relative to the caller's CMakeLists or absolute: the game keeps its shaders in Content/),
# built before <target>.
# One .hlsl file holds both stages (VSMain/PSMain), so <name>.hlsl compiles to <name>.vert.spv and <name>.frag.spv.
function(gram_add_hlsl_raster_shader target source)
    get_filename_component(source ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(name ${source} NAME_WE)
    set(dir ${CMAKE_BINARY_DIR}/shaders)
    set(vert ${dir}/${name}.vert.spv)
    set(frag ${dir}/${name}.frag.spv)
    add_custom_command(
        OUTPUT ${vert} ${frag}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T vs_6_0 -E VSMain ${source} -Fo ${vert}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T ps_6_0 -E PSMain ${source} -Fo ${frag}
        DEPENDS ${source}
        COMMENT "Compiling shader ${name}.hlsl to SPIR-V"
        VERBATIM
    )
    add_custom_target(${target}_${name}_spirv DEPENDS ${vert} ${frag})
    add_dependencies(${target} ${target}_${name}_spirv)
endfunction()

# gram_add_hlsl_compute_shader(<target> <source>): called by the module that owns the shader (the game,
# for its own content), after add_subdirectory(GramEngine). Compiles <source> (a .hlsl path, relative
# to the caller's CMakeLists or absolute: the game keeps its shaders in Content/), built before <target>.
# One .hlsl file holds one stage (CSMain), so <name>.hlsl compiles to <name>.compute.spv.
function(gram_add_hlsl_compute_shader target source)
    get_filename_component(source ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(name ${source} NAME_WE)
    set(dir ${CMAKE_BINARY_DIR}/shaders)
    set(compute ${dir}/${name}.compute.spv)
    add_custom_command(
        OUTPUT  ${compute}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T cs_6_0 -E CSMain ${source} -Fo ${compute}
        DEPENDS ${source}
        COMMENT "Compiling shader ${name}.hlsl to SPIR-V"
        VERBATIM
    )
    add_custom_target(${target}_${name}_spirv DEPENDS ${compute})
    add_dependencies(${target} ${target}_${name}_spirv)
endfunction()
