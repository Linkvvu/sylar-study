#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>

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



namespace sylar {

	class Logger;	/// forward declaration

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
		explicit Logger(std::string name = "ROOT-Logger");

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
	};

} // namespace sylar
