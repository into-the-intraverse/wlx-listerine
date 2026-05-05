# wlx-listerine-md plugin DLL — markdown viewer.

add_library(wlx-listerine-md SHARED
    window/host_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/search/search_hud.cpp
    exports/plugin.def
    "${CMAKE_BINARY_DIR}/generated/resource.rc"
)

target_include_directories(wlx-listerine-md PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(wlx-listerine-md PRIVATE
    wlx-core
    wlx-listerine-core
    shell32
    dwmapi
    uxtheme
    comctl32
)

set_target_properties(wlx-listerine-md PROPERTIES
    SUFFIX ".wlx64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/output"
)

target_compile_definitions(wlx-listerine-md PRIVATE WLX_VERSION_STRING="${WLX_VERSION}")
if(WLX_TRACE_ENABLE)
    target_compile_definitions(wlx-listerine-md PRIVATE WLX_TRACE_ENABLE)
endif()
