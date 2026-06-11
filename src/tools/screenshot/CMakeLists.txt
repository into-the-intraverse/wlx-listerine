# screenshot_tool — generates rendered PNGs from markdown sources for visual regression.

add_executable(screenshot_tool
    colorizer_pipeline.cpp
    main.cpp
    markdown_pipeline.cpp
    # colorizer_layout is compiled into wlx-listerine-colorizer (a DLL) which
    # screenshot_tool cannot link as an import lib. Compile the source directly
    # here instead — it has no dllexport annotations, so this is safe.
    ${CMAKE_SOURCE_DIR}/src/plugin_colorizer/layout/colorizer_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin_colorizer/colorize/span_table.cpp
)

target_include_directories(screenshot_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
    tomlplusplus::tomlplusplus
)

# Stage the binary alongside wlx-listerine-core.dll so visual-test.sh finds it.
set_target_properties(screenshot_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
)
