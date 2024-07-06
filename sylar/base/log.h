#pragma once

#include "singleton.hpp"
#include "this_thread.h"

#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <cassert>
#include <sstream>
#include <fstream>
#include <iostream>
#include <functional>
#include <unordered_map>

#define SYLAR_ROOT_LOGGER_NAME "SYLAR"
#define SYLAR_SYSTEM_LOGGER_NAME "SYS"

#define SYLAR_LOG_LEVEL(logger, level)	\
	if (level >= logger->GetLevel()) 	\
		sylar::base::LogEventWrapper(std::shared_ptr<sylar::base::LogEvent>(new sylar::base::LogEvent({	\
			logger,																		\
			std::ostringstream(),														\
			std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),		\
			__LINE__,			\
			__FILE__,			\
			sylar::base::GetTid(),	\
			0,					\
			level				\
		}))).GetEvent()->message_stream

#define SYLAR_LOG_DEBUG(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::base::LogLevel::Level::kDebug)

#define SYLAR_LOG_INFO(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::base::LogLevel::Level::kInfo)

#define SYLAR_LOG_WARN(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::base::LogLevel::Level::kWarn)

#define SYLAR_LOG_ERROR(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::base::LogLevel::Level::kError)

#define SYLAR_LOG_FATAL(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::base::LogLevel::Level::kFatal)

#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...)	\
    if (level >= logger->GetLevel())	\
		sylar::base::LogEventWrapper(std::shared_ptr<sylar::base::LogEvent>(new sylar::base::LogEvent({	\
					logger,																		\
					std::ostringstream(),														\
					std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),		\
					__LINE__,			\
					__FILE__,			\
					sylar::base::GetTid(),	\
					0,					\
					level				\
				}))).GetEvent()->SetMessage(fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::base::LogLevel::kDebug, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_INFO(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::base::LogLevel::kInfo, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_WARN(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::base::LogLevel::kWarn, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::base::LogLevel::kError, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::base::LogLevel::kFatal, fmt, __VA_ARGS__)

#define SYLAR_ROOT_LOGGER()	\
	sylar::base::Singleton<sylar::base::LoggerManager>::GetInstance().GetLogger(SYLAR_ROOT_LOGGER_NAME)

#define SYLAR_GET_LOGGER(name)	\
	sylar::base::Singleton<sylar::base::LoggerManager>::GetInstance().GetLogger(name)

#define SYLAR_SYS_LOGGER()	\
	SYLAR_GET_LOGGER(SYLAR_SYSTEM_LOGGER_NAME)

namespace sylar {
namespace base {

class LoggerManager;	/// forward declaration
class LogAppender;		/// forward declaration
class Logger;			/// forward declaration

struct LogLevel {
	enum Level {
		kUnKnown,
		kDebug,
		kInfo,
		kWarn,
		kError,
		kFatal
	};

	static std::string ToString(Level l);
	static LogLevel::Level FromString(const std::string& str);
};



struct LogEvent {
	static std::shared_ptr<LogEvent> NewLogEvent(std::string msg, LogLevel::Level l = LogLevel::kUnKnown);

	void SetMessage(std::string msg);

	void SetMessage(const char* fmt, ...);

	std::string GetMessage() const
	{ return message_stream.str(); }

	std::shared_ptr<Logger> trigger;
	std::ostringstream message_stream;
	std::time_t time;
	std::uint32_t line_num;
	const char* file_name;
	::pid_t thread_id;
	unsigned long routine_id;
	LogLevel::Level level;
};



class LogEventWrapper {
public:
	explicit LogEventWrapper(std::shared_ptr<LogEvent>&& event)
		: event_(std::move(event))
		{}

	~LogEventWrapper();

	std::shared_ptr<LogEvent>& GetEvent()
	{ return event_; }

private:
	std::shared_ptr<LogEvent> event_;
};


/// @brief 日志格式化器
/// @note lock-free结构, 因为Formatter被构造完成后将不会再改变
class LogFormatter {
public:
	explicit LogFormatter(std::string pattern);

	std::string Format(const std::shared_ptr<LogEvent>& event) const;

	struct AbsFormatterItem {
		explicit AbsFormatterItem() = default;
		virtual ~AbsFormatterItem() noexcept = default;
		virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const = 0;
	};

private:
	/// @brief 根据pattern初始化FormatterItems
	/// @throw std::invalid_argument  无效的pattern
	void Init();
	void HandleDoublePercentState();
	void HandlePercentAfterTextState(size_t begin, size_t end);
	void HandleEndingWithText(size_t begin);
	bool HandleTryAddItemState(char c);
	void HandleTimeFormatEndState(size_t begin, size_t end);

private:
	const std::string pattern_;
	std::vector<std::shared_ptr<AbsFormatterItem>> items_;
};



class Logger {
	friend LoggerManager;
	explicit Logger(std::string name);

public:
	void Log(const std::shared_ptr<LogEvent>& event) const;

	void Debug(std::string msg) const
	{ Log(LogEvent::NewLogEvent(std::move(msg), LogLevel::kDebug)); }

	void Info(std::string msg) const
	{ Log(LogEvent::NewLogEvent(std::move(msg), LogLevel::kInfo)); }

	void Warn(std::string msg) const
	{ Log(LogEvent::NewLogEvent(std::move(msg), LogLevel::kWarn)); }

	void Error(std::string msg) const
	{ Log(LogEvent::NewLogEvent(std::move(msg), LogLevel::kError)); }

	void Fatal(std::string msg) const
	{ Log(LogEvent::NewLogEvent(std::move(msg), LogLevel::kFatal)); }

	void AddAppender(std::shared_ptr<LogAppender> appender);

	void ClearAllAppender();

	const std::shared_ptr<LogFormatter>& GetFormatter() const;

	void SetFormatter(const std::shared_ptr<LogFormatter>& formatter);

	const std::shared_ptr<Logger>& GetParent() const;

	/// @throw std::runtime_error  检测到将要发生循环引用
	void SetParent(std::shared_ptr<Logger> parent);

	void SetLogLevel(LogLevel::Level l)
	{ level_ = l; }

	const std::string& GetName() const
	{ return name_; }

	LogLevel::Level GetLevel() const
	{ return level_; }

private:
	const std::string name_;
	LogLevel::Level level_ = LogLevel::Level::kDebug;
	std::vector<std::shared_ptr<LogAppender>> appenderArray_;
	std::shared_ptr<LogFormatter> formatter_;
	std::shared_ptr<Logger> parent_;
	mutable std::mutex mutex_;
};


/// @brief 日志追加器(thread-safe)，指定日志输出位置
class LogAppender {
	friend void Logger::SetFormatter(const std::shared_ptr<LogFormatter>& formatter);
	friend void Logger::AddAppender(std::shared_ptr<LogAppender> appender);
public:
	virtual void Log(const std::shared_ptr<LogEvent>& event) const = 0;

	void SetFormatter(std::shared_ptr<LogFormatter> formatter);

	const std::shared_ptr<LogFormatter>& GetFormatter() const;

	/// @note 该函数没有加锁以保证flag的“最新状态”，这是可以接受的，
	///		  因为“设置完formatter后，还未来得及更新flag就再次被
	///		  其他线程访问的概率很小，并且formatter被覆盖相对来说
	///		  是可以接受的”
	bool HasSpecialFormatter() const
	{ return hasSpecialFormatter; }

	void SetLogLevel(LogLevel::Level l)
	{ level_ = l; }

	LogLevel::Level GetLogLevel() const
	{ return level_; }

	virtual ~LogAppender() noexcept = default;

protected:
	LogLevel::Level level_;
	bool hasSpecialFormatter = false;
	std::shared_ptr<LogFormatter> formatter_;
	mutable std::mutex mutex_;
};



class StreamLogAppender : public LogAppender {
public:
	explicit StreamLogAppender(std::ostream& out_stream);

	virtual void Log(const std::shared_ptr<LogEvent>& event) const override;

private:
	std::ostream& targetOutStream_;
};

class FileStreamLogAppender : public StreamLogAppender {
public:

	/// @throw std::runtime_error  打开文件失败
	explicit FileStreamLogAppender(std::string filename);
	virtual ~FileStreamLogAppender() noexcept override;

private:
	std::string filename_;
	std::ofstream ofs_;
};

class LoggerManager {
	explicit LoggerManager();
	LoggerManager(const LoggerManager&) = delete;
	LoggerManager& operator=(const LoggerManager&) = delete;

	friend base::Singleton<LoggerManager>;

public:
	/**
	 * @brief Get the Logger object from the set
		*
		* @param name  目标Logger唯一ID名
		* @return std::shared_ptr<Logger>  返回目标Logger示例，若目标Logger不存在，则创建实例并返回
		*/
	std::shared_ptr<Logger> GetLogger(const std::string& name);
	void RemoveLogger(const std::string& name);

private:
	std::shared_ptr<Logger> InitLoggerAndAppend(const std::string& name);

private:
	std::shared_ptr<Logger> rootLogger_;
	std::unordered_map<std::string, std::shared_ptr<Logger>> loggers_;
	std::mutex mutex_;
};

} // namespace base
} // namespace sylar
