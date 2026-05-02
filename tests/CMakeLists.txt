# Doctest executables — markdown + colorizer test suites.
# Test sources migrate into mirrored runtime/<sub>/ paths as Phase 1 progresses.

add_executable(tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    runtime/parser/test_document_model.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_theme_service.cpp
    runtime/io/test_file_service.cpp
    runtime/parser/test_markdown_parser.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_cache_service.cpp
    runtime/layout/test_layout_engine.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_text_selection.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_search_engine.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_search_ops.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_wlx_host_common.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_search_counter_format.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_search_hud_painter.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
    comctl32
)

add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer_helix_theme.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer_grammar.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer_query_highlighter.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer_grammars.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_colorizer_routing.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_wlx_core_abi.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_core_config.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_grammar_cache.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    wlx-listerine-core
    doctest::doctest
)
