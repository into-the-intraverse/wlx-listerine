# screenshot_tool — generates rendered PNGs from markdown sources for visual regression.

add_executable(screenshot_tool
    colorizer_pipeline.cpp
    main.cpp
    markdown_pipeline.cpp
)

target_include_directories(screenshot_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
)

# Stage the binary alongside wlx-listerine-core.dll so visual-test.sh finds it.
set_target_properties(screenshot_tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
)
