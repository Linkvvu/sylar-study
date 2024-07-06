find_package(GTest REQUIRED)

enable_testing()
include(GoogleTest)

add_executable(log_test log_test.cpp)
target_link_libraries(log_test PUBLIC ${PROJECT_NAME})

add_executable(config_test config_test.cpp)
target_include_directories(config_test PUBLIC ${GTEST_INCLUDE_DIRS})
target_link_libraries(config_test PUBLIC ${PROJECT_NAME} GTest::gtest GTest::gtest_main gmock)

add_executable(debug_test debug_test.cpp)
target_link_libraries(debug_test PUBLIC ${PROJECT_NAME})

gtest_discover_tests(config_test)
