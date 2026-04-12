# CMake sample
cmake_minimum_required(VERSION 3.20)
project(sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
option(BUILD_TESTS "Build tests" ON)

add_library(mylib STATIC src/lib.cpp)
target_include_directories(mylib PUBLIC include)

if(BUILD_TESTS)
    add_executable(tests tests/main.cpp)
    target_link_libraries(tests PRIVATE mylib)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
