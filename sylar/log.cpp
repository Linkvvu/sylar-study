#include "log.h"

#include <cstdarg>
#include <cstring>
#include <cassert>
#include <iomanip>
#include <iostream>

#if defined(__unix__)
	#include <time.h>
#endif

using namespace sylar;

std::string LogLevel::ToString(Level l) {
    switch (l) {
	case Level::kDebug:
		return "DEBUG";
	case Level::kInfo:
		return "INFO";
	case Level::kWarn:
		return "WARN";
	case Level::kError:
		return "ERROR";
	case Level::kFatal:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

std::shared_ptr<LogEvent> LogEvent::NewLogEvent(std::string msg, LogLevel::Level l) {
	std::ostringstream oss(std::move(msg));
    std::shared_ptr<LogEvent> new_event(new LogEvent({
		std::make_shared<Logger>(nullptr),
		std::move(oss),
		std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
		__LINE__,
		__FILE__,
		::pthread_self(),
		0,	///> TODO
		l
	}));
	return new_event;
}

void sylar::LogEvent::SetMessage(std::string msg) {
	message_stream << std::move(msg);
}

void sylar::LogEvent::SetMessage(const char* fmt, ...) {
	va_list al;
	va_start(al, fmt);

	/// @note vasprintf() is a part of GNU extension, reference: https://linux.die.net/man/3/vasprintf
	///		  C standard lib: vfprintf(), reference: https://zh.cppreference.com/w/c/io/vfprintf

	char* buf = nullptr;
	int len = vasprintf(&buf, fmt, al);
	if (len != -1) {
		message_stream << std::string(buf, len);
		free(buf);
	}

	va_end(al);
}

LogEventWrapper::~LogEventWrapper() {
	event_->agent_logger->Log(event_);
}

// ----------------------------------------------------------------------------------------------------
// The defines for Formatter item

class MsgFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit MsgFormatterItem() = default;
	virtual ~MsgFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return event->GetMessage();
	}
};

class RawStrFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit RawStrFormatterItem(std::string str)
		: str_(str)
		{}

	virtual ~RawStrFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return str_;
	}

	const std::string str_;
};

class DateTimeFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit DateTimeFormatterItem(std::string pattern = "%Y-%m-%d %H:%M:%S")
		: pattern_(pattern)
		{}

	virtual ~DateTimeFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		std::stringstream oss;

		std::tm now_tm {};
		::tm* p_res = nullptr;
	#if defined(_WIN32)
		p_res = ::localtime_s(&now_tm, &event->time);
	#elif defined(__unix)
		p_res = ::localtime_r(&event->time, &now_tm);
	#endif
		if (!p_res) {
			return "error time";
		}

		oss << std::put_time(&now_tm, pattern_.c_str());
		return oss.str();
	}

	const std::string pattern_;
};

class NewLineFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit NewLineFormatterItem() = default;
	virtual ~NewLineFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return "\r\n";
	}
};

class LineNumFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit LineNumFormatterItem() = default;
	virtual ~LineNumFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return std::to_string(event->line_num);
	}
};

class TabFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit TabFormatterItem() = default;
	virtual ~TabFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return "\t";
	}
};

class FileNameFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit FileNameFormatterItem() = default;
	virtual ~FileNameFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		assert(event->file_name);
		return std::string(event->file_name);
	}
};

class ThreadIdFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit ThreadIdFormatterItem() = default;
	virtual ~ThreadIdFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return std::to_string(event->thread_id);
	}
};

class LoggerNameFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit LoggerNameFormatterItem() = default;
	virtual ~LoggerNameFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		assert(event->agent_logger);
		return event->agent_logger->GetName();
	}
};

class logLevelFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit logLevelFormatterItem() = default;
	virtual ~logLevelFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return LogLevel::ToString(event->level);
	}
};

class RoutineIdFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit RoutineIdFormatterItem() = default;
	virtual ~RoutineIdFormatterItem() noexcept override = default;

	virtual std::string DoFormat(const std::shared_ptr<LogEvent>& event) const {
		return std::to_string(event->routine_id);
	}
};

// =====================================================================================================

LogFormatter::LogFormatter(std::string pattern)
	: pattern_(pattern)
{
	init();
}

void LogFormatter::init() {
	static std::unordered_map<char, std::shared_ptr<AbsFormatterItem>> ls_formatter_item_set {
		{'m', std::make_shared<MsgFormatterItem>()},
		{'c', std::make_shared<LoggerNameFormatterItem>()},
		{'L', std::make_shared<logLevelFormatterItem>()},
		{'l', std::make_shared<LineNumFormatterItem>()},
		{'t', std::make_shared<TabFormatterItem>()},
		{'n', std::make_shared<NewLineFormatterItem>()},
		{'f', std::make_shared<FileNameFormatterItem>()},
		{'T', std::make_shared<ThreadIdFormatterItem>()},
		{'R', std::make_shared<RoutineIdFormatterItem>()},
	};

	enum class State {
		kText,		// 普通文本状态
		kPercent,	// 遇到%后的状态，准备解析格式标识符
		kBrace,		// 遇到{后的状态，准备解析时间格式字符串
	};

	std::vector<std::shared_ptr<AbsFormatterItem>> temp_items;

	State state = State::kText;
	for (size_t i = 0; i < pattern_.size(); ++i) {
		char ch = pattern_[i];

		switch (state)
		{
		case State::kText:
			if (ch == '%') {
				state = State::kPercent;
			} else if (ch == '{') {
				state = State::kBrace;
			} else {
				// get complete raw string
				size_t end = i + 1;	// range: [i, end)
				while (end < pattern_.size() && pattern_[end] != '%' && pattern_[end] != '{') {
					end++;
				}

				// create a RawStrFormatterItem instance
				std::string raw_str = pattern_.substr(i, end - i);
				temp_items.emplace_back(new RawStrFormatterItem(std::move(raw_str)));

				// update index and continue
				i = end - 1;
				continue;
			}
			break;
		case State::kPercent:
			if (ch == 'd') {
				if (i + 1 < pattern_.size() && pattern_[i + 1] == '{') {
					i += 1;
					state = State::kBrace;
				} else {
					std::cerr << "Invalid datetime pattern: " << pattern_;
					available_ = false;
					return;
				}
			} else {
				auto it = ls_formatter_item_set.find(ch);
				if (it != ls_formatter_item_set.end()) {
					temp_items.push_back(it->second);
				} else {
					std::cerr << "Unexpected character '"<< ch << "', ignore it." << std::endl;
				}
				state = State::kText;
			}
			break;
		case State::kBrace:
			// get complete datetime format
			size_t end = i + 1;	// range: [i, end)
			while (end < pattern_.size() && pattern_[end] != '}') {
				end++;
			}

			if (end >= pattern_.size()) {
				// 没有解析到 '}'
				std::cerr << "Invalid datetime pattern: " << pattern_;
				available_ = false;
				return;
			}

			// create a DateTimeFormatterItem instance
			std::string datetime_str = pattern_.substr(i, end - i);
			temp_items.emplace_back(new DateTimeFormatterItem(std::move(datetime_str)));

			// update index and continue
			i = end;	// skip '}'
			state = State::kText;
			continue;
		}
	}

	items_.swap(temp_items);
}

std::string LogFormatter::Format(const std::shared_ptr<LogEvent>& event) const {
	std::string str;
	for (const auto& item : items_) {
		str += item->DoFormat(event);
	}
	return str;
}

StreamLogAppender::StreamLogAppender(std::ostream& out_stream)
	: LogAppender()
	, targetOutStream_(out_stream)
	{}

void StreamLogAppender::Log(const std::shared_ptr<LogEvent>& event) const {
	assert(event && formatter_);
	targetOutStream_ << formatter_->Format(event);
}

Logger::Logger(std::string name)
	: name_(name)
	{}

void Logger::Log(const std::shared_ptr<LogEvent>& event) const {
	if (event->level >= level_) {
		for (const auto& appender : appenderArray_) {
			appender->Log(event);
		}
	}
}

/**
 * @brief
 * @param appender
 * @todo 考虑线程安全
 */
void Logger::AddAppender(std::shared_ptr<const LogAppender> appender) {
	appenderArray_.push_back(std::move(appender));
}

LoggerManager::LoggerManager()
	: defaultLogger_(std::make_shared<Logger>())
	, loggers_()
{
	auto default_appender = std::make_shared<StreamLogAppender>(std::cout);
	auto std_out_formatter = std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%t%T%t%R%t[%L]%t[%c]%t%f:%l%t%m%n");
	default_appender->SetFormatter(std_out_formatter);
	defaultLogger_->AddAppender(default_appender);

	loggers_[defaultLogger_->GetName()] = defaultLogger_;

	auto init_loggers = [&default_appender](const std::vector<std::shared_ptr<Logger>>& loggers) {
		for (const auto& logger_ptr : loggers) {
			logger_ptr->AddAppender(default_appender);
		}
	};

	init(std::move(init_loggers));
}

std::shared_ptr<Logger> LoggerManager::GetLogger(std::string name) const {
	auto it = loggers_.find(std::move(name));
	if (it == loggers_.end()) {
		SYLAR_LOG_WARN(SYLAR_DEF_LOGGER()) << "Has not logger named '" << name << "', returns the default logger.";
		return defaultLogger_;
	}

	return it->second;
}

void LoggerManager::AddLogger(std::shared_ptr<Logger> newer, std::shared_ptr<Logger>* older) {
	const std::string& name = newer->GetName();
	auto it = loggers_.find(name);
	if (it != loggers_.end() && older) {
		*older = loggers_[name];
	}
	loggers_[name] = newer;
}

void LoggerManager::init(std::function<void(const std::vector<std::shared_ptr<Logger>>& appenders)> func) {
	auto system_logger = std::make_shared<Logger>("System");
	func({system_logger});
	AddLogger(std::move(system_logger));
}
