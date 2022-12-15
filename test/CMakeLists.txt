
file(GLOB SRC_TEST ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${PROJECT_NAME}-test ${SRC_TEST})
target_link_libraries(${PROJECT_NAME}-test kv-lockfree )

if(LINUX)
  find_package(Threads REQUIRED)
  if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET ${PROJECT_NAME}-test PROPERTY COMPILE_OPTIONS "-pthread")
    set_property(TARGET ${PROJECT_NAME}-test PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
  endif()
  if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(${PROJECT_NAME}-test "${CMAKE_THREAD_LIBS_INIT}")
  endif()
endif()

add_subdirectory(libs/gtest)
target_link_libraries(${PROJECT_NAME}-test "GTest::gtest_main" "GTest::gtest")