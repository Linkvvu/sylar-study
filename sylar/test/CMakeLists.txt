find_package(GTest REQUIRED)

enable_testing()
include(GoogleTest)

add_executable(log_test log_test.cpp)
target_link_libraries(log_test PUBLIC sylar)

add_executable(config_test config_test.cpp)
target_include_directories(config_test PUBLIC ${GTEST_INCLUDE_DIRS})
target_link_libraries(config_test PUBLIC sylar GTest::gtest GTest::gtest_main gmock)
gtest_discover_tests(config_test)
