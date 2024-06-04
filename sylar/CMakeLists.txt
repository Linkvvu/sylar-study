set(SYLAR_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/log.cpp
)

source_group(sylar FILES ${SYLAR_SRC})

add_library(${CMAKE_PROJECT_NAME} SHARED ${SYLAR_SRC})

if(ENABLE_TEST MATCHES ON)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
