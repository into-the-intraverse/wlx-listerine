# wlx-listerine-core shared DLL — colorizer engine.
# Owns the colorize() pipeline, grammar cache, and Helix theme loader.

add_library(wlx-listerine-core SHARED
    abi/wlx_core_abi.cpp
    abi/dllmain.cpp
    colorizer/colorizer.cpp
    grammar/grammar_registry.cpp
    grammar/grammar_cache.cpp
    highlighting/query_highlighter.cpp
    theme/helix_theme.cpp
    registry/core_registry.cpp
    registry/core_config.cpp
)

target_include_directories(wlx-listerine-core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(wlx-listerine-core
    PUBLIC
        tree-sitter::tree-sitter
    PRIVATE
        tomlplusplus::tomlplusplus
)

target_compile_definitions(wlx-listerine-core PRIVATE WLX_CORE_BUILDING)

set_target_properties(wlx-listerine-core PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

# The POST_BUILD mirrors below copy the whole grammars/ directory, which the
# tree-sitter-* targets are still linking into when MSBuild schedules them
# concurrently with this DLL. cmake -E copy_directory then hits a half-written
# DLL and fails the build — the intermittent CI "Error copying directory"
# failure at ~34s into the Build step. Depend on every grammar target so the
# mirror runs only after they all exist.
get_property(_wlx_grammar_targets GLOBAL PROPERTY WLX_GRAMMAR_TARGETS)
if(_wlx_grammar_targets)
    add_dependencies(wlx-listerine-core ${_wlx_grammar_targets})
endif()

# Mirror the DLL alongside test exes / screenshot_tool so they can find it on
# the loader search path. Tests live in ${CMAKE_BINARY_DIR}/tests/<cfg>/;
# screenshot_tool and plugin stubs live in ${CMAKE_BINARY_DIR}/<cfg>/.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:wlx-listerine-core>"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:wlx-listerine-core>"
        "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/wlx-listerine-core.dll"
)

# After Task 3, the CoreRegistry singleton derives its install dir from the
# core DLL's actual filesystem location (GetModuleFileNameW). Mirror grammars
# and themes alongside the DLL so the test rig running from build/<cfg>/ finds
# them at the same DLL-relative paths the production install uses.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/config/themes"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/grammars"
        "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/grammars"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/config/themes"
        "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/themes"
)

# Task 4: ship wlx-listerine-core.toml next to the core DLL — both in the
# production output/ install dir AND alongside the test rig DLL mirror in
# build/<cfg>/. The CoreConfig::load() call in the registry singleton looks
# for this file at $<TARGET_FILE_DIR:wlx-listerine-core>/wlx-listerine-core.toml.
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_SOURCE_DIR}/output/wlx-listerine-core.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/wlx-listerine-core.toml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/wlx-listerine-core.toml"
        "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/wlx-listerine-core.toml"
)
