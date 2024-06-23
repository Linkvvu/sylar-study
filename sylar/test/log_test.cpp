#include "../log.h"

#include <iostream>

int main() {
	// private constructor -> prevents to create directly
	// should create by singleton sylar::LoggerManager
	//
	// auto test_logger = std::make_shared<sylar::Logger>("test");

	SYLAR_LOG_LEVEL(SYLAR_GET_LOGGER(SYLAR_ROOT_LOGGER_NAME), sylar::LogLevel::kInfo) << "这是一条测试日志 A";
	SYLAR_LOG_DEBUG(SYLAR_ROOT_LOGGER()) << "这是一条测试日志 B";
	SYLAR_LOG_FMT_FATAL(SYLAR_ROOT_LOGGER(), "--- %s ---", "这是一条fmt日志");
}
