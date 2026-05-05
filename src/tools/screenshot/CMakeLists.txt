# screenshot_tool — generates rendered PNGs from markdown sources for visual regression.

add_executable(screenshot_tool
    main.cpp
)

target_include_directories(screenshot_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenshot_tool PRIVATE
    wlx-core
    windowscodecs
    ole32
)
