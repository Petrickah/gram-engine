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

# gram_copy_next_to(<target> <folder> <files>...): after <target> is built, copies <files> into
# <folder>/ next to its executable. Under `gram build` that's the build dir itself (nothing to copy:
# gram copies the folder next to the executable it places); in an IDE project (`gram ide`) the
# executable lands elsewhere, and this is how its shaders and textures follow it. Called from the
# directory that created <target>, which is where CMake accepts a POST_BUILD command for it.
function(gram_copy_next_to target folder)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/${folder}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ARGN} $<TARGET_FILE_DIR:${target}>/${folder}
        VERBATIM
    )
endfunction()

# gram_hlsl_headers(<out var> <source>): the .hlsli headers next to <source>, so a shader recompiles when
# one of them changes (the pass header gram generates next to it, `<name>.generated.hlsli`). Every
# header of the folder, not only the ones this shader includes: dxc has no dependency file output to say
# which. CONFIGURE_DEPENDS re-globs at build time, so a header gram writes for the first time is seen.
function(gram_hlsl_headers out source)
    get_filename_component(folder ${source} DIRECTORY)
    file(GLOB headers CONFIGURE_DEPENDS ${folder}/*.hlsli)
    set(${out} ${headers} PARENT_SCOPE)
endfunction()

# gram_add_hlsl_raster_shader(<target> <source>): called by the module that owns the shader (the game,
# for its own content), after add_subdirectory(GramEngine). Compiles <source> (a .hlsl path,
# relative to the caller's CMakeLists or absolute: the game keeps its shaders in Content/),
# built before <target>.
# One .hlsl file holds both stages (VSMain/PSMain), so <name>.hlsl compiles to <name>.vert.spv and <name>.frag.spv.
function(gram_add_hlsl_raster_shader target source)
    get_filename_component(source ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(name ${source} NAME_WE)
    gram_hlsl_headers(headers ${source})
    set(dir ${CMAKE_BINARY_DIR}/shaders)
    set(vert ${dir}/${name}.vert.spv)
    set(frag ${dir}/${name}.frag.spv)
    add_custom_command(
        OUTPUT ${vert} ${frag}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T vs_6_0 -E VSMain ${source} -Fo ${vert}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T ps_6_0 -E PSMain ${source} -Fo ${frag}
        DEPENDS ${source} ${headers}
        COMMENT "Compiling shader ${name}.hlsl to SPIR-V"
        VERBATIM
    )
    add_custom_target(${target}_${name}_spirv DEPENDS ${vert} ${frag})
    add_dependencies(${target} ${target}_${name}_spirv)
    gram_copy_next_to(${target} shaders ${vert} ${frag})
endfunction()

# gram_add_hlsl_compute_shader(<target> <source>): called by the module that owns the shader (the game,
# for its own content), after add_subdirectory(GramEngine). Compiles <source> (a .hlsl path, relative
# to the caller's CMakeLists or absolute: the game keeps its shaders in Content/), built before <target>.
# One .hlsl file holds one stage (CSMain), so <name>.hlsl compiles to <name>.compute.spv.
function(gram_add_hlsl_compute_shader target source)
    get_filename_component(source ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(name ${source} NAME_WE)
    gram_hlsl_headers(headers ${source})
    set(dir ${CMAKE_BINARY_DIR}/shaders)
    set(compute ${dir}/${name}.compute.spv)
    add_custom_command(
        OUTPUT  ${compute}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${GRAM_DXC_EXECUTABLE} -fspv-target-env=vulkan1.3 -spirv -T cs_6_0 -E CSMain ${source} -Fo ${compute}
        DEPENDS ${source} ${headers}
        COMMENT "Compiling shader ${name}.hlsl to SPIR-V"
        VERBATIM
    )
    add_custom_target(${target}_${name}_spirv DEPENDS ${compute})
    add_dependencies(${target} ${target}_${name}_spirv)
    gram_copy_next_to(${target} shaders ${compute})
endfunction()
