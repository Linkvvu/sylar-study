find_package(Boost REQUIRED)
find_package(yaml-cpp REQUIRED)

set(SYLAR_SRC
	${CMAKE_CURRENT_SOURCE_DIR}/log.cpp
)

source_group(sylar FILES ${SYLAR_SRC})

add_library(${CMAKE_PROJECT_NAME} SHARED ${SYLAR_SRC})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIRS})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC yaml-cpp)
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC yaml-cpp)

if(ENABLE_TEST MATCHES ON)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
