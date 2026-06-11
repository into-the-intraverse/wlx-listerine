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
    runtime/layout/test_line_index.cpp
    runtime/layout/test_inline_layout.cpp
    runtime/layout/test_utf8_offset_map.cpp
    runtime/interaction/test_interaction_engine.cpp
    runtime/interaction/test_text_selection.cpp
    runtime/interaction/test_url_scanner.cpp
    runtime/search/test_search_engine.cpp
    runtime/search/test_search_ops.cpp
    runtime/host/test_wlx_host_common.cpp
    runtime/host/test_async_loader.cpp
    runtime/host/test_view_actions.cpp
    runtime/host/test_goto_line.cpp
    runtime/host/test_web_search.cpp
    runtime/host/test_context_menu.cpp
    runtime/host/test_grammar_menu.cpp
    runtime/search/test_search_counter_format.cpp
    runtime/search/test_search_hud_painter.cpp
    runtime/layout/test_lazy_layout.cpp
)

target_link_libraries(tests PRIVATE
    wlx-core
    doctest::doctest
    comctl32
)

add_executable(colorizer-tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    core_dll/colorizer/test_colorizer.cpp
    core_dll/colorizer/test_error_node_highlighting.cpp
    core_dll/theme/test_colorizer_helix_theme.cpp
    core_dll/grammar/test_colorizer_grammar.cpp
    core_dll/highlighting/test_colorizer_query_highlighter.cpp
    core_dll/grammar/test_colorizer_grammars.cpp
    plugin_colorizer/language/test_colorizer_routing.cpp
    plugin_colorizer/language/test_path_to_language.cpp
    plugin_colorizer/layout/test_colorizer_layout.cpp
    plugin_colorizer/test_colorizer_smoke.cpp
    plugin_colorizer/colorize/test_span_table.cpp
    core_dll/abi/test_wlx_core_abi.cpp
    core_dll/registry/test_core_config.cpp
    core_dll/grammar/test_grammar_cache.cpp
    tools/test_token_json_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_colorizer/layout/colorizer_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_colorizer/colorize/span_table.cpp
)

target_link_libraries(colorizer-tests PRIVATE
    wlx-core
    wlx-listerine-core
    doctest::doctest
)

# Stage test binaries alongside wlx-listerine-core.dll so they can resolve
# the DLL at runtime, and so CI / docs can run them at build/<cfg>/<exe>.
# Without this they default to build/tests/<cfg>/ after the source restructure.
set_target_properties(tests colorizer-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
)
