# --- textures ---
# Images the game loads at runtime (stb_image reads them as-is: no conversion step yet). Copied
# into <top-level build dir>/textures/, which gram copies next to the built executable, so the
# game opens them as <workspace>/textures/<file name>. Stopgap, like the shaders, until gram
# grows a dedicated asset module kind.

# gram_add_texture(<target> <source>): called by the module that owns the texture (the game, for
# its own content), after add_subdirectory(GramEngine). Copies <source> (an image path, relative
# to the caller's CMakeLists or absolute: the game keeps its textures in Content/textures/) into
# the textures folder, before <target> is built and again whenever the file changes.
function(gram_add_texture target source)
    get_filename_component(source ${source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    get_filename_component(file ${source} NAME)
    set(dir ${CMAKE_BINARY_DIR}/textures)
    set(output ${dir}/${file})
    add_custom_command(
        OUTPUT  ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${source} ${output}
        DEPENDS ${source}
        COMMENT "Copying texture ${file}"
        VERBATIM
    )
    # The file name with its extension: texture.jpg and texture.png get different targets.
    string(MAKE_C_IDENTIFIER ${file} id)
    add_custom_target(${target}_${id}_texture DEPENDS ${output})
    add_dependencies(${target} ${target}_${id}_texture)
endfunction()
