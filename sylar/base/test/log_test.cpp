#include "../log.h"

#include <iostream>

int main() {
	// private constructor -> prevents to create directly
	// should create by singleton sylar::LoggerManager
	//
	// auto test_logger = std::make_shared<sylar::Logger>("test");
	using namespace sylar;

	SYLAR_LOG_LEVEL(SYLAR_GET_LOGGER(SYLAR_ROOT_LOGGER_NAME), base::LogLevel::kInfo) << "这是一条测试日志 A" << std::endl;
	SYLAR_LOG_DEBUG(SYLAR_ROOT_LOGGER()) << "这是一条测试日志 B" << std::endl;
	SYLAR_LOG_FMT_FATAL(SYLAR_ROOT_LOGGER(), "--- %s ---\n", "这是一条fmt日志");
}
