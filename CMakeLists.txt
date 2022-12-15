cmake_minimum_required(VERSION 3.10.0)
project(kv-lockfree VERSION 0.1.0)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

set(CMAKE_CXX_STANDARD 20)
add_library(kv-lockfree INTERFACE)
target_include_directories(kv-lockfree INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

#test
add_subdirectory(test)
add_subdirectory(sample)