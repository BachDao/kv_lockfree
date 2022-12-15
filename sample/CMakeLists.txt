
set(CMAKE_CXX_STANDARD 20)
add_executable(main ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(main kv-lockfree)