#include "../log.h"

#include <iostream>

int main() {
	auto formatter = std::make_shared<sylar::LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%t%T%t%R%t[%L]%t[%c]%t%m %f:%l%t%n");
	std::shared_ptr<sylar::Logger> logger = std::make_shared<sylar::Logger>();
	std::shared_ptr<sylar::StreamLogAppender> cout_appender = std::make_shared<sylar::StreamLogAppender>(std::cout);
	cout_appender->SetFormatter(formatter);
	logger->AddAppender(cout_appender);
	std::shared_ptr<sylar::LogEvent> loge = std::make_shared<sylar::LogEvent>();
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::kInfo) << "这是一条测试日志";
}
