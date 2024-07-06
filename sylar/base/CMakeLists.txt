project(sylar_base)

find_package(Boost REQUIRED)
find_package(yaml-cpp REQUIRED)

set(
    SYLAR_BASE_SRC
    log.cpp
    config.cpp
    this_thread.cpp
    debug.cpp
)

source_group(${PROJECT_NAME} FILES ${SYLAR_BASE_SRC})

add_library(${PROJECT_NAME} SHARED ${SYLAR_BASE_SRC})
target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIRS} ${YAML_CPP_INCLUDE_DIR})
target_link_libraries(
	${PROJECT_NAME}
	PUBLIC
	yaml-cpp::yaml-cpp
)

if(ENABLE_TEST MATCHES ON)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()
