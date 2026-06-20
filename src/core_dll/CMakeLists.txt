# wlx-listerine-core shared DLL — colorizer engine.
# Owns the colorize() pipeline, grammar cache, and Helix theme loader.

add_library(wlx-listerine-core SHARED
    abi/wlx_core_abi.cpp
    abi/dllmain.cpp
    colorizer/colorizer.cpp
    lexilla/lex_document.cpp
    lexilla/lexilla_highlighter.cpp
    lexilla/lexer_registry.cpp
    theme/helix_theme.cpp
    registry/core_registry.cpp
    registry/core_config.cpp
    "${CMAKE_BINARY_DIR}/generated/core_resource.rc"
)

target_include_directories(wlx-listerine-core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(wlx-listerine-core
    PRIVATE
        lexilla
        tomlplusplus::tomlplusplus
)

target_compile_definitions(wlx-listerine-core PRIVATE WLX_CORE_BUILDING)

set_target_properties(wlx-listerine-core PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

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

# The CoreRegistry singleton derives its install dir from the core DLL's actual
# filesystem location (GetModuleFileNameW). Mirror themes alongside the DLL so
# the test rig running from build/<cfg>/ finds them at the same DLL-relative
# paths the production install uses. (Lexilla needs no per-grammar files.)
add_custom_command(TARGET wlx-listerine-core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/config/themes"
        "${CMAKE_BINARY_DIR}/$<CONFIG>/themes"
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
