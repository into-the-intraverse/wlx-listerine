# wlx-core static library — markdown plugin runtime.
# Sources migrate into per-subfolder relative paths as Phase 1 progresses.

add_library(wlx-core STATIC
    io/file_service.cpp
    parser/markdown_parser.cpp
    layout/layout_engine.cpp
    render/render_engine.cpp
    interaction/interaction_engine.cpp
    theme/theme_service.cpp
    cache/cache_service.cpp
    interaction/text_selection.cpp
    search/search_index.cpp
    search/search_hud_painter.cpp
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
)
