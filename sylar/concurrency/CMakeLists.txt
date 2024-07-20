project(sylar_concurrency)

set(
  SYLAR_CONCURRENCY_SRC
  coroutine.cpp
  thread.cpp
  scheduler.cpp
  epoll_poller.cpp
  notifier.cpp
  timer_manager.cpp
)

source_group(${PROJECT_NAME} FILES ${SYLAR_CONCURRENCY_SRC})

add_library(${PROJECT_NAME} SHARED ${SYLAR_CONCURRENCY_SRC})
target_link_libraries(
	${PROJECT_NAME}
	PUBLIC
  pthread
  sylar_base
)

if(ENABLE_TEST MATCHES ON)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
