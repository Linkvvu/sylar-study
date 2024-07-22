add_executable(coroutine_test coroutine_test.cpp)
target_link_libraries(coroutine_test PUBLIC ${PROJECT_NAME})

add_executable(thread_test thread_test.cpp)
target_link_libraries(thread_test PUBLIC ${PROJECT_NAME})

add_executable(scheduler_test scheduler_test.cpp)
target_link_libraries(scheduler_test PUBLIC ${PROJECT_NAME})

add_executable(epoll_poller_test epoll_poller_test.cpp)
target_link_libraries(epoll_poller_test PUBLIC ${PROJECT_NAME})

add_executable(hook_test hook_test.cpp)
target_link_libraries(hook_test PUBLIC ${PROJECT_NAME})
