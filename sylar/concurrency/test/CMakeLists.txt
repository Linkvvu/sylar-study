add_executable(coroutine_test coroutine_test.cpp)
target_link_libraries(coroutine_test PUBLIC ${PROJECT_NAME})

add_executable(thread_test thread_test.cpp)
target_link_libraries(thread_test PUBLIC ${PROJECT_NAME})
