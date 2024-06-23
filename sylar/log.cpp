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
		std::shared_ptr<Logger>(nullptr),
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
	event_->trigger->Log(event_);
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

class TextFormatterItem : public LogFormatter::AbsFormatterItem {
public:
	explicit TextFormatterItem(std::string str)
		: str_(str)
		{}

	virtual ~TextFormatterItem() noexcept override = default;

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
		assert(event->trigger);
		return event->trigger->GetName();
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

void LogAppender::SetFormatter(std::shared_ptr<LogFormatter> formatter) {
	using namespace std;
	swap(this->formatter_, formatter);

	this->formatter_ ? hasSpecialFormatter = true : hasSpecialFormatter = false;
}


LogFormatter::LogFormatter(std::string pattern)
	: pattern_(pattern)
{
	init();
}

void LogFormatter::init() {
	enum class Event : size_t {
		kPercent,	// %
		kAlphaD,	// d
		kBrace,		// {
		kBackBrace,	// }
		kOther		// others
	};

	enum class State : size_t {
		kStart,
		kTextBegin,
		kTextKeep,
		kPercentAfterText,
		kSinglePercent,
		kTryAddItem,
		kDoublePercent,
		kWaitBrace,
		kTimeFormatBegin,
		kTimeFormatKeep,
		kTimeFormatEnd,
		kInvalid
	};

	static auto get_event_func = [](char c) -> Event {
		switch (c) {
		case '%':
			return Event::kPercent;
		case 'd':
			return Event::kAlphaD;
		case '{':
			return Event::kBrace;
		case '}':
			return Event::kBackBrace;
		default:
			return Event::kOther;
		}
	};

	// 状态转移表 - [state][event]
	static State transform_table[(size_t)State::kInvalid + 1][(size_t)Event::kOther + 1] {
		{State::kSinglePercent, State::kTextBegin, State::kTextBegin, State::kTextBegin, State::kTextBegin},	// Start
		{State::kPercentAfterText, State::kTextKeep, State::kTextKeep, State::kTextKeep, State::kTextKeep},		// TextBegin
		{State::kPercentAfterText, State::kTextKeep, State::kTextKeep, State::kTextKeep, State::kTextKeep},		// TextKeep
		{State::kDoublePercent, State::kWaitBrace, State::kInvalid, State::kInvalid, State::kTryAddItem},		// PercentAfterText
		{State::kDoublePercent, State::kWaitBrace, State::kInvalid, State::kInvalid, State::kTryAddItem},		// SinglePercent
		{State::kSinglePercent, State::kTextBegin, State::kTextBegin, State::kTextBegin, State::kTextBegin},// TryAddItem
		{State::kSinglePercent, State::kTextBegin, State::kTextBegin, State::kTextBegin, State::kTextBegin},// DoublePercent
		{State::kInvalid, State::kInvalid, State::kTimeFormatBegin, State::kInvalid, State::kInvalid},				// WaitBrace
		{State::kTimeFormatKeep, State::kTimeFormatKeep, State::kTimeFormatKeep, State::kTimeFormatEnd, State::kTimeFormatKeep},// TimeFormatBegin
		{State::kTimeFormatKeep, State::kTimeFormatKeep, State::kTimeFormatKeep, State::kTimeFormatEnd, State::kTimeFormatKeep},// TimeFormatKeep
		{State::kSinglePercent, State::kTextBegin, State::kTextBegin, State::kTextBegin, State::kTextBegin},// TimeFormatEnd
		{State::kInvalid, State::kInvalid, State::kInvalid, State::kInvalid, State::kInvalid}					// Invalid
	};

	State state = State::kStart;
	std::pair<size_t, size_t> range {0, 0};	// [begin, end)
	for (size_t i = 0; i < pattern_.size(); ++i) {
		state = transform_table[(size_t)state][(size_t)get_event_func(pattern_[i])];

		switch (state) {
		case State::kStart:
		case State::kTextKeep:
		case State::kSinglePercent:
		case State::kWaitBrace:
		case State::kTimeFormatKeep:
			continue;
		case State::kTextBegin:
		case State::kTimeFormatBegin:
			range.first = i;
			break;
		case State::kPercentAfterText:
			range.second = i;
			HandlePercentAfterTextState(range.first, range.second);
			range = {0, 0};
			break;
		case State::kTryAddItem:
			HandleTryAddItemState(pattern_[i]);
			break;
		case State::kDoublePercent:
			HandleDoublePercentState();
			break;
		case State::kTimeFormatEnd:
			range.second = i;
			HandleTimeFormatEndState(range.first, range.second);
			range = {0, 0};
			break;
		case State::kInvalid:
			throw std::invalid_argument("invalid log formatter pattern");
		}
	}
}

static std::unordered_map<char, std::shared_ptr<LogFormatter::AbsFormatterItem>> s_formatter_item_set {
	{'m', std::make_shared<MsgFormatterItem>()},
	{'c', std::make_shared<LoggerNameFormatterItem>()},
	{'L', std::make_shared<logLevelFormatterItem>()},
	{'l', std::make_shared<LineNumFormatterItem>()},
	{'t', std::make_shared<TabFormatterItem>()},
	{'n', std::make_shared<NewLineFormatterItem>()},
	{'f', std::make_shared<FileNameFormatterItem>()},
	{'T', std::make_shared<ThreadIdFormatterItem>()},
	{'R', std::make_shared<RoutineIdFormatterItem>()},
	{'%', std::make_shared<TextFormatterItem>("%")},
};

void LogFormatter::HandleDoublePercentState() {
	items_.push_back(s_formatter_item_set['%']);
}

void LogFormatter::HandlePercentAfterTextState(size_t begin, size_t end) {
	std::string text = pattern_.substr(begin, end - begin);
	items_.emplace_back(std::make_shared<TextFormatterItem>(std::move(text)));
}

void LogFormatter::HandleTryAddItemState(char c) {
	items_.push_back(s_formatter_item_set[c]);
}

void sylar::LogFormatter::HandleTimeFormatEndState(size_t begin, size_t end) {
	std::string time_format = pattern_.substr(begin + 1, end - begin - 1);
	items_.emplace_back(std::make_shared<DateTimeFormatterItem>(std::move(time_format)));
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

	this->targetOutStream_ << formatter_->Format(event);
}

Logger::Logger(std::string name)
	: name_(std::move(name))
	{}

void Logger::Log(const std::shared_ptr<LogEvent>& event) const {
	if (event->level >= level_) {
		if (appenderArray_.empty()) {
			assert(parent_);
			parent_->Log(event);
			return;
		}

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
void Logger::AddAppender(std::shared_ptr<LogAppender> appender) {
	if (!appender->HasSpecialFormatter()) {
		appender->SetFormatter(this->GetFormatter());
	}
	appenderArray_.push_back(std::move(appender));
}

void Logger::SetFormatter(const std::shared_ptr<LogFormatter>& formatter) {
	if (formatter_ == formatter) {
		return;
	}

	formatter_ = formatter;

	for (auto& appender : appenderArray_) {
		if (appender->HasSpecialFormatter() == false) {
			appender->formatter_ = formatter;
		}
	}
}

LoggerManager::LoggerManager()
	: rootLogger_(std::shared_ptr<Logger>(new Logger(SYLAR_ROOT_LOGGER_NAME)))
	, loggers_()
{
	rootLogger_->SetFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%t%T%t%R%t[%L]%t[%c]%t%f:%l%t%m%n"));
	rootLogger_->AddAppender(std::make_shared<StreamLogAppender>(std::cout));

	loggers_[rootLogger_->GetName()] = rootLogger_;
}

std::shared_ptr<Logger> LoggerManager::GetLogger(const std::string& name) {
	auto it = loggers_.find(name);
	if (it != loggers_.end()) {
		return it->second;
	}

	return InitLoggerAndAppend(name);
}

std::shared_ptr<Logger> LoggerManager::InitLoggerAndAppend(const std::string& name) {
	assert(loggers_.count(name) == 0);

	// creates and initializes
	auto new_logger = std::shared_ptr<Logger>(new Logger(SYLAR_ROOT_LOGGER_NAME));
	new_logger->SetParent(rootLogger_);
	new_logger->SetFormatter(rootLogger_->GetFormatter());

	// appends
	loggers_[name] = new_logger;

	return new_logger;
}

