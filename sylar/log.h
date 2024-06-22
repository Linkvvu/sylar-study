#pragma once

#include "util.h"

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <functional>
#include <unordered_map>

#define SYLAR_DEF_LOGGER_NAME "DEFAULT-LOGGER"

#define SYLAR_LOG_LEVEL(logger, level)	\
	if (level >= logger->GetLevel()) 	\
		sylar::LogEventWrapper(std::shared_ptr<sylar::LogEvent>(new sylar::LogEvent({	\
			logger,																		\
			std::ostringstream(),														\
			std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),		\
			__LINE__,			\
			__FILE__,			\
			::pthread_self(),	\
			0,					\
			level				\
		}))).GetEvent()->message_stream

#define SYLAR_LOG_DEBUG(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::Level::kDebug)

#define SYLAR_LOG_INFO(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::Level::kInfo)

#define SYLAR_LOG_WARN(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::Level::kWarn)

#define SYLAR_LOG_ERROR(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::Level::kError)

#define SYLAR_LOG_FATAL(logger)	\
	SYLAR_LOG_LEVEL(logger, sylar::LogLevel::Level::kFatal)

#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...)	\
    if (level >= logger->GetLevel())	\
		sylar::LogEventWrapper(std::shared_ptr<sylar::LogEvent>(new sylar::LogEvent({	\
					logger,																		\
					std::ostringstream(),														\
					std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),		\
					__LINE__,			\
					__FILE__,			\
					::pthread_self(),	\
					0,					\
					level				\
				}))).GetEvent()->SetMessage(fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::kDebug, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_INFO(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::kInfo, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_WARN(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::kWarn, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::kError, fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...)	\
	SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::kFatal, fmt, __VA_ARGS__)

#define SYLAR_DEF_LOGGER()	\
	sylar::Singleton<sylar::LoggerManager>::GetInstance().GetLogger(SYLAR_DEF_LOGGER_NAME)

#define SYLAR_SYS_LOGGER()	\
	sylar::Singleton<sylar::LoggerManager>::GetInstance().GetLogger("System")


namespace sylar {

	class LoggerManager;	/// forward declaration
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
	};



	struct LogEvent {
		static std::shared_ptr<LogEvent> NewLogEvent(std::string msg, LogLevel::Level l = LogLevel::kUnKnown);

		void SetMessage(std::string msg);

		void SetMessage(const char* fmt, ...);

		std::string GetMessage() const
		{ return message_stream.str(); }

		std::shared_ptr<Logger> agent_logger;
		std::ostringstream message_stream;
		std::time_t time;
		std::uint32_t line_num;
		const char* file_name;
		unsigned long thread_id;
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
		void init();

	private:
		const std::string pattern_;
		std::vector<std::shared_ptr<AbsFormatterItem>> items_;
		bool available_ = true;
	};



	class LogAppender {
	public:
		virtual void Log(const std::shared_ptr<LogEvent>& event) const = 0;

		void SetFormatter(std::shared_ptr<LogFormatter> formatter)
		{ formatter_ = std::move(formatter); }

		const std::shared_ptr<LogFormatter>& GetFormatter() const
		{ return formatter_; }

		virtual ~LogAppender() noexcept = default;

	protected:
		/// TODO:
		///		level filed

		std::shared_ptr<LogFormatter> formatter_;
	};



	class StreamLogAppender : public LogAppender {
	public:
		explicit StreamLogAppender(std::ostream& out_stream);

		virtual void Log(const std::shared_ptr<LogEvent>& event) const override;

	private:
		std::ostream& targetOutStream_;
	};



	class Logger {
	public:
		explicit Logger(std::string name = SYLAR_DEF_LOGGER_NAME);

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

		void AddAppender(std::shared_ptr<const LogAppender> appender);

		const std::string& GetName() const
		{ return name_; }

		LogLevel::Level GetLevel() const
		{ return level_; }

	private:

	private:
		const std::string name_;
		LogLevel::Level level_ = LogLevel::Level::kDebug;
		std::vector<std::shared_ptr<const LogAppender>> appenderArray_;
		std::shared_ptr<LogFormatter> formatter_;
		std::shared_ptr<Logger> parent_;	///> parent logger. @note avoids circle dependence
	};



	class LoggerManager {
		explicit LoggerManager();
		LoggerManager(const LoggerManager&) = delete;
		LoggerManager& operator=(const LoggerManager&) = delete;

		friend Singleton<LoggerManager>;

	public:
		/**
		 * @brief Get the Logger object from the set
		 *
		 * @param name
		 * @return std::shared_ptr<Logger>
		 */
		std::shared_ptr<Logger> GetLogger(std::string name) const;

		/**
		 * @brief Add a new logger to the set.
		 *
		 * @param newer  The newer logger
		 * @param older  Point to the older logger(Passing out), if exist
		 */
		void AddLogger(std::shared_ptr<Logger> newer, std::shared_ptr<Logger>* older = nullptr);

	private:
		void init(std::function<void(const std::vector<std::shared_ptr<Logger>>& appenders)> func);

	private:
		std::shared_ptr<Logger> defaultLogger_;
		std::unordered_map<std::string, std::shared_ptr<Logger>> loggers_;
	};

} // namespace sylar
