# wlx-listerine-colorizer plugin DLL.
# Standalone code colorizer view; depends on wlx-core (runtime) and wlx-listerine-core (engine).

add_library(wlx-listerine-colorizer SHARED
    window/colorizer_host_adapter.cpp
    layout/colorizer_layout.cpp
    layout/grid_window.cpp
    colorize/span_table.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/search/search_hud.cpp
    exports/plugin.def
    "${CMAKE_BINARY_DIR}/generated/colorizer_resource.rc"
)

target_include_directories(wlx-listerine-colorizer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(wlx-listerine-colorizer PRIVATE
    wlx-core
    wlx-listerine-core
    tomlplusplus::tomlplusplus
    shell32
    dwmapi
    uxtheme
    comctl32
)

set_target_properties(wlx-listerine-colorizer PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

target_compile_definitions(wlx-listerine-colorizer PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
if(WLX_TRACE_ENABLE)
    target_compile_definitions(wlx-listerine-colorizer PRIVATE WLX_TRACE_ENABLE)
endif()

# The POST_BUILD staging below copies the whole grammars/ directory; depend on
# every grammar target so the copy can't race a tree-sitter-* DLL still being
# linked into it (same intermittent CI failure as the core DLL's test mirror).
get_property(_wlx_grammar_targets GLOBAL PROPERTY WLX_GRAMMAR_TARGETS)
if(_wlx_grammar_targets)
    add_dependencies(wlx-listerine-colorizer ${_wlx_grammar_targets})
endif()

# Output staging — runs as the last per-plugin POST_BUILD; mirrors today's behavior.
add_custom_command(TARGET wlx-listerine-colorizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-md.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-md.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-colorizer.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-colorizer.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/themes/default_light.toml"
        "${CMAKE_SOURCE_DIR}/output/themes/default_light.toml"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/output/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_SOURCE_DIR}/output/grammars"
)
