# wlx-core static library — markdown plugin runtime.
# Sources migrate into per-subfolder relative paths as Phase 1 progresses.

add_library(wlx-core STATIC
    io/file_service.cpp
    parser/markdown_parser.cpp
    layout/inline_layout.cpp
    layout/code_fence_layout.cpp
    layout/layout_engine.cpp
    layout/line_index.cpp
    layout/md_materialize.cpp
    render/render_engine.cpp
    interaction/interaction_engine.cpp
    theme/theme_service.cpp
    cache/cache_service.cpp
    interaction/text_selection.cpp
    interaction/url_scanner.cpp
    search/search_index.cpp
    search/search_hud_painter.cpp
    host/async_loader.cpp
    host/clipboard.cpp
    host/context_menu.cpp
    host/dark_mode.cpp
    host/factories.cpp
    host/goto_line.cpp
    host/grammar_menu.cpp
    host/hit_test.cpp
    host/module_path.cpp
    host/web_search.cpp
    host/window_class.cpp
)

target_include_directories(wlx-core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/colorizer
)

target_link_libraries(wlx-core
    PUBLIC
        md4c::md4c
        d2d1
        dwrite
        wlx-listerine-core
    PRIVATE
        tomlplusplus::tomlplusplus
        dwmapi
        uxtheme
)

if(WLX_TRACE_ENABLE)
    target_compile_definitions(wlx-core PRIVATE WLX_TRACE_ENABLE)
endif()
