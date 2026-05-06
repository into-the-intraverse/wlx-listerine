# Doctest executables — markdown + colorizer test suites.
# Test sources migrate into mirrored runtime/<sub>/ paths as Phase 1 progresses.

add_executable(tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    runtime/parser/test_document_model.cpp
    runtime/theme/test_theme_service.cpp
    runtime/io/test_file_service.cpp
    runtime/parser/test_markdown_parser.cpp
    runtime/cache/test_cache_service.cpp
    runtime/layout/test_layout_engine.cpp
    runtime/interaction/test_text_selection.cpp
    runtime/search/test_search_engine.cpp
    runtime/search/test_search_ops.cpp
    runtime/host/test_wlx_host_common.cpp
    runtime/search/test_search_counter_format.cpp
    runtime/search/test_search_hud_painter.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
    comctl32
)

add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    plugin_colorizer/language/test_colorizer_routing.cpp
    plugin_colorizer/test_colorizer_smoke.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/registry/test_core_config.cpp
    core_dll/grammar/test_grammar_cache.cpp
    tools/test_token_json_writer.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    wlx-listerine-core
    doctest::doctest
)
