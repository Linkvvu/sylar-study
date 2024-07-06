#include "log.h"
#include "config.h"

#include <cstdarg>
#include <cstring>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <unordered_set>
#if defined(__unix__)
	#include <time.h>
#endif

using namespace sylar;
using namespace sylar::base;

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

LogLevel::Level LogLevel::FromString(const std::string& str) {
	if (str == "DEBUG") {
		return Level::kDebug;
	} else if (str == "INFO") {
		return Level::kInfo;
	} else if (str == "WARN") {
		return Level::kWarn;
	} else if (str == "ERROR") {
		return Level::kError;
	} else if (str == "FATAL") {
		return Level::kFatal;
	} else {
		return Level::kUnKnown;
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
		sylar::base::GetTid(),
		0,	///> TODO
		l
	}));
	return new_event;
}

void sylar::base::LogEvent::SetMessage(std::string msg) {
	message_stream << std::move(msg);
}

void sylar::base::LogEvent::SetMessage(const char* fmt, ...) {
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
	std::lock_guard<std::mutex> guard(mutex_);
	formatter_ = formatter;
	hasSpecialFormatter = (this->formatter_ != nullptr);
}

const std::shared_ptr<LogFormatter>& LogAppender::GetFormatter() const {
    std::lock_guard<std::mutex> guard(mutex_);
	return formatter_;
}


LogFormatter::LogFormatter(std::string pattern)
	: pattern_(pattern)
{
	Init();
}

void LogFormatter::Init() {
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

	State current_state = State::kStart;
	std::pair<size_t, size_t> range {0, 0};	// [begin, end)
	for (size_t i = 0; i < pattern_.size(); ++i) {
		current_state = transform_table[(size_t)current_state][(size_t)get_event_func(pattern_[i])];

		switch (current_state) {
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
			if (!HandleTryAddItemState(pattern_[i])) {
				/// FIXME:
				/// 	使用异常代替错误日志，在LogFormatter构造函数调用端接收异常，
				///     并记录详细的、带有上下文的错误日志
				SYLAR_LOG_FMT_ERROR(
					SYLAR_ROOT_LOGGER(),
					"invalid FormatterItem id [%%%c] when initialize Formatter, ignore it",
					pattern_[i]
				);
			}
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

	if (current_state == State::kTextBegin || current_state == State::kTextKeep) {
		HandleEndingWithText(range.first);
	} else if (current_state == State::kPercentAfterText
		|| current_state == State::kSinglePercent
		|| current_state == State::kWaitBrace
		|| current_state == State::kTimeFormatBegin
		|| current_state == State::kTimeFormatKeep)
	{
		throw std::invalid_argument("invalid log formatter pattern");
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

void LogFormatter::HandleEndingWithText(size_t begin) {
	std::string text = pattern_.substr(begin);
	items_.emplace_back(std::make_shared<TextFormatterItem>(std::move(text)));
}

bool LogFormatter::HandleTryAddItemState(char c) {
	auto target_it = s_formatter_item_set.find(c);
	if (target_it != s_formatter_item_set.end()) {
		items_.push_back(target_it->second);
		return true;
	}
	return false;
}

void sylar::base::LogFormatter::HandleTimeFormatEndState(size_t begin, size_t end) {
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

	if (event->level >= this->level_) {
		std::lock_guard<std::mutex> guard(mutex_);

		this->targetOutStream_ << formatter_->Format(event);
	}
}

Logger::Logger(std::string name)
	: name_(std::move(name))
	{}

void Logger::Log(const std::shared_ptr<LogEvent>& event) const {
	if (event->level >= level_) {
		bool parent_do_log = false;
		std::vector<std::shared_ptr<sylar::base::LogAppender>> duplicate;
		{
			std::lock_guard<std::mutex> guard(mutex_);

			if (appenderArray_.empty()) {
				assert(parent_);
				parent_do_log = true;
			} else {
				// gets a duplicate of appenders to shorten the critical section
				// cause logging is expensive
				duplicate = appenderArray_;
			}
		}

		if (parent_do_log) {
			parent_->Log(event);
  		} else {
			for (const auto& appender : duplicate)
				appender->Log(event);
		}
	}
}

void Logger::AddAppender(std::shared_ptr<LogAppender> appender) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (!appender->HasSpecialFormatter()) {
		std::lock_guard<std::mutex> guard(appender->mutex_);
		appender->formatter_ = formatter_;
	}
	appenderArray_.push_back(std::move(appender));
}

void Logger::ClearAllAppender() {
	std::lock_guard<std::mutex> guard(mutex_);
	appenderArray_.clear();
}

const std::shared_ptr<LogFormatter>& Logger::GetFormatter() const {
	std::lock_guard<std::mutex> guard(mutex_);
    return formatter_;
}

void Logger::SetFormatter(const std::shared_ptr<LogFormatter>& formatter) {
	std::lock_guard<std::mutex> guard(mutex_);

	formatter_ = formatter;

	for (auto& appender : appenderArray_) {
		if (appender->HasSpecialFormatter() == false) {
			std::lock_guard<std::mutex> guard(appender->mutex_);
			appender->formatter_ = formatter;
			// 设置flag为false，以避免小概率的覆盖事件，导致状态不一致
			appender->hasSpecialFormatter = false;
		}
	}
}

const std::shared_ptr<Logger>& Logger::GetParent() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return parent_;
}


void Logger::SetParent(std::shared_ptr<Logger> parent) {
	// avoids cyclic dependency
	auto current = parent;
    while (current) {
        if (current.get() == this) {
            throw std::runtime_error("Cyclic dependency detected!");
        }
        current = current->GetParent();
    }

	std::lock_guard<std::mutex> guard(mutex_);
	parent_ = std::move(parent);
}

LoggerManager::LoggerManager()
	: rootLogger_(std::shared_ptr<Logger>(new Logger(SYLAR_ROOT_LOGGER_NAME)))
	, loggers_()
{
	rootLogger_->SetFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%t%T%t%R%t[%L]%t[%c]%t%f:%l%t%m"));
	rootLogger_->AddAppender(std::make_shared<StreamLogAppender>(std::cout));

	loggers_[rootLogger_->GetName()] = rootLogger_;
}

std::shared_ptr<Logger> LoggerManager::GetLogger(const std::string& name) {
	std::lock_guard<std::mutex> guard(mutex_);

	auto it = loggers_.find(name);
	if (it != loggers_.end()) {
		return it->second;
	}

	return InitLoggerAndAppend(name);
}

void LoggerManager::RemoveLogger(const std::string& name) {
	std::lock_guard<std::mutex> guard(mutex_);
	loggers_.erase(name);
}

std::shared_ptr<Logger> LoggerManager::InitLoggerAndAppend(const std::string& name) {
	assert(loggers_.count(name) == 0);

	// creates and initializes
	auto new_logger = std::shared_ptr<Logger>(new Logger(name));
	new_logger->SetParent(rootLogger_);
	new_logger->SetFormatter(rootLogger_->GetFormatter());

	// appends
	loggers_[name] = new_logger;

	return new_logger;
}


// ----------------------------------------------------------------------------------------------------
// Log config defines(helpers)

struct LogAppenderDefine {
	constexpr static const char* kLevelConfField = "level";
	constexpr static const char* kFormatPatternConfField = "format_pattern";
	constexpr static const char* kTypeConfField = "type";
	constexpr static const char* kMetaConfField = "meta";
	constexpr static const char* kConsoleTypeConfFieldVal = "console";
	constexpr static const char* kFileTypeConfFieldVal = "file";
	constexpr static const char* kStdOutConfFieldVal = "out";
	constexpr static const char* kStdErrConfFieldVal = "error";

	std::shared_ptr<LogAppender> GenerateInstance() const;

	bool operator==(const LogAppenderDefine& another) const {
		return level == another.level
			&& format_pattern == another.format_pattern
			&& type == another.type
			&& meta == another.meta;
	}

	LogLevel::Level level;
	std::string format_pattern;
	std::string type;
	/// @brief meta info for related type
	/// @todo 将meta结构化，而不仅限于字符串，如当type值为file时，
	///		  meta不仅用于存储文件路径，而且可以存储文件打开标志
	std::string meta;
};

struct LoggerConfDefine {
	constexpr static const char* kNameConfField = "name";
	constexpr static const char* kLevelConfField = "level";
	constexpr static const char* kFormatPatternConfField = "format_pattern";
	constexpr static const char* kAppendersConfField = "appenders";

	bool operator==(const LoggerConfDefine& another) const {
		return name == another.name
			&& level == another.level
			&& format_pattern == another.format_pattern
			&& appender_defs == another.appender_defs;
	}

	bool operator!=(const LoggerConfDefine& another) const {
		return !this->operator==(another);
	}

	std::shared_ptr<Logger> GenerateInstance() const;

	std::string name;
	LogLevel::Level level;
	std::string format_pattern;
	std::vector<LogAppenderDefine> appender_defs;
};

namespace std {

template <>
struct hash<LoggerConfDefine> {
	std::size_t operator()(const LoggerConfDefine& logger_define) const {
		return ((std::hash<std::string>()(logger_define.name)
			^ std::hash<int>()(logger_define.level))
			& std::hash<std::string>()(logger_define.format_pattern));
	}
};

}

namespace sylar {
namespace base {

template <>
struct LexicalCast<std::string, LogAppenderDefine, YamlTag> {
	LogAppenderDefine operator()(const std::string& from) {
		LogAppenderDefine log_appender_def;
		YAML::Node node = YAML::Load(from);
		if (!node.IsMap()) {
			throw std::logic_error("expect yaml document is map, but it's not");
		}

		// gets appenders's format pattern if exist
		if (node[LogAppenderDefine::kFormatPatternConfField].IsDefined()
			&& node[LogAppenderDefine::kFormatPatternConfField].IsScalar()
			&& !node[LogAppenderDefine::kFormatPatternConfField].Scalar().empty())
		{
			log_appender_def.format_pattern = node[LogAppenderDefine::kFormatPatternConfField].Scalar();
		} else {
			log_appender_def.format_pattern = "";
		}

		// gets appender's level
		log_appender_def.level =
			node[LogAppenderDefine::kLevelConfField].IsDefined() && node[LogAppenderDefine::kLevelConfField].IsScalar()
			? LogLevel::FromString(node[LogAppenderDefine::kLevelConfField].Scalar())
			: LogLevel::kUnKnown;

		// gets appender's type
		if (!node[LogAppenderDefine::kTypeConfField].IsDefined()
			|| !node[LogAppenderDefine::kTypeConfField].IsScalar()
			|| node[LogAppenderDefine::kTypeConfField].Scalar().empty())
		{
			throw std::logic_error("logger config error: appender must has a valid type");
		} else {
			log_appender_def.type = node[LogAppenderDefine::kTypeConfField].Scalar();
		}

		// gets appender's meta info
		if (!node[LogAppenderDefine::kMetaConfField].IsDefined()
			|| !node[LogAppenderDefine::kMetaConfField].IsScalar()
			|| node[LogAppenderDefine::kMetaConfField].Scalar().empty())
		{
			throw std::logic_error("logger config error: appender must has a valid meta info");
		} else {
			log_appender_def.meta = node[LogAppenderDefine::kMetaConfField].Scalar();
		}

		return log_appender_def;
	}
};

template <>
struct LexicalCast<std::string, LoggerConfDefine, YamlTag> {
	LoggerConfDefine operator()(const std::string& from) {
		LoggerConfDefine log_conf_def;
		YAML::Node node = YAML::Load(from);
		if (!node.IsMap()) {
			throw std::logic_error("expect yaml document is map, but it's not");
		}

		// gets logger's name
		if (!node[LoggerConfDefine::kNameConfField].IsDefined()
			|| !node[LoggerConfDefine::kNameConfField].IsScalar())
		{
			throw std::logic_error("logger config error: logger name is null or invalid yaml document");
		}
		log_conf_def.name = node[LoggerConfDefine::kNameConfField].Scalar();

		// gets logger's level
		log_conf_def.level =
			node[LoggerConfDefine::kLevelConfField].IsDefined() && node[LoggerConfDefine::kLevelConfField].IsScalar()
			? LogLevel::FromString(node[LoggerConfDefine::kLevelConfField].Scalar())
			: LogLevel::kUnKnown;

		// gets logger's format pattern if exist
		if (!node[LoggerConfDefine::kFormatPatternConfField].IsDefined()
			|| !node[LoggerConfDefine::kFormatPatternConfField].IsScalar()
			|| node[LoggerConfDefine::kFormatPatternConfField].Scalar().empty())
		{
			SYLAR_LOG_FMT_INFO(SYLAR_ROOT_LOGGER(),
				"Format pattern of logger [%s] is null or invalid yaml document, "
				"ready to use the root logger's formatter\n",
				log_conf_def.name.c_str()
			);
			log_conf_def.format_pattern = "";
		} else {
			log_conf_def.format_pattern =
				node[LoggerConfDefine::kFormatPatternConfField].Scalar();
		}

		// gets logger's appender defines if exist
		if (node[LoggerConfDefine::kAppendersConfField].IsDefined()) {
			std::ostringstream oss;
			oss << node[LoggerConfDefine::kAppendersConfField];

			log_conf_def.appender_defs =
				LexicalCast<std::string, std::vector<LogAppenderDefine>, YamlTag>()(
					oss.str()
				);
		}

		return log_conf_def;
	}
};

template <>
struct LexicalCast<LogAppenderDefine, std::string, YamlTag> {
	std::string operator()(const LogAppenderDefine& from) {
		YAML::Node appender_node(YAML::NodeType::Map);
		appender_node[LogAppenderDefine::kFormatPatternConfField] = from.format_pattern;
		appender_node[LogAppenderDefine::kLevelConfField] = LogLevel::ToString(from.level);
		std::ostringstream oss;
		oss << appender_node;
		return oss.str();
	}
};

template <>
struct LexicalCast<LoggerConfDefine, std::string, YamlTag> {
	std::string operator()(const LoggerConfDefine& from) {
		YAML::Node node(YAML::NodeType::Map);
		node[LoggerConfDefine::kNameConfField] = from.name;
		if (from.level != LogLevel::kUnKnown)
			node[LoggerConfDefine::kLevelConfField] = LogLevel::ToString(from.level);
		node[LoggerConfDefine::kFormatPatternConfField] = from.format_pattern;

		const auto& appender_defs_doc = LexicalCast<std::vector<LogAppenderDefine>, std::string, YamlTag>()(from.appender_defs);
		node[LoggerConfDefine::kAppendersConfField] = YAML::Load(appender_defs_doc);

		std::ostringstream oss;
		oss << node;
		return oss.str();
	}
};

FileStreamLogAppender::FileStreamLogAppender(std::string filename)
	: StreamLogAppender(ofs_)
	, filename_(std::move(filename))
	, ofs_(filename_, std::ios::out | std::ios::ate)
{
	if (!ofs_.is_open()) {
		throw std::runtime_error("failed to open file, file: \"" + filename_ + "\"");
	}
}

FileStreamLogAppender::~FileStreamLogAppender() noexcept {
	ofs_.close();
}

} // namespace base
} // namespace sylar

// ----------------------------------------------------------------------------------------------------
// 初始化配置文件指定的Loggers

struct __InitLoggersHelper {
	__InitLoggersHelper() {
		auto logs_conf = base::Singleton<ConfigManager>::GetInstance()
				.AddOrUpdate("loggers", std::unordered_set<LoggerConfDefine>(), "loggers config");

		logs_conf->AddMonitor(
			[](const std::unordered_set<LoggerConfDefine>& old, const std::unordered_set<LoggerConfDefine>& now) {
				// create or update Loggers
				for (const auto& item : now) {
					std::shared_ptr<Logger> logger;
					auto it = old.find(item);
					if (it == old.end()) {
						item.GenerateInstance();
					}
				}

				// remove the missing Loggers
				/// FIXME:
				/// 	使用更高效的方法
				for (const auto& item : old) {
					auto it = std::find_if(now.begin(), now.end(),
						[&item](const LoggerConfDefine& define) {
							return item.name == define.name;
						}
					);
					if (it == now.end()) {
						base::Singleton<LoggerManager>::GetInstance().RemoveLogger(item.name);
					}
				}
			}
		);
	}
};

static __InitLoggersHelper s_init_logger_helper {};

std::shared_ptr<LogAppender> LogAppenderDefine::GenerateInstance() const {
	std::shared_ptr<LogAppender> result;

	// create the concrete appender
	if (this->type == LogAppenderDefine::kConsoleTypeConfFieldVal) {
		if (this->meta == LogAppenderDefine::kStdOutConfFieldVal) {
			result = std::make_shared<StreamLogAppender>(std::cout);
		} else if (this->meta == LogAppenderDefine::kStdErrConfFieldVal) {
			result = std::make_shared<StreamLogAppender>(std::cerr);
		}
	} else if (this->type == LogAppenderDefine::kFileTypeConfFieldVal) {
		try {
			result = std::make_shared<FileStreamLogAppender>(meta);
		} catch (const std::runtime_error& e) {
			SYLAR_LOG_FMT_ERROR(SYLAR_ROOT_LOGGER(),
				"catch a runtime exception when generate the file LogAppender, detail: %s",
				e.what()
			);
			// result is null now
			return result;
		}
	} else {
		SYLAR_LOG_FMT_ERROR(SYLAR_ROOT_LOGGER(),
			"logger config error: the Appender specifies a invalid type [%s], ignore it", type.c_str());
		return result;
	}

	// init the formatter if need
	if (!format_pattern.empty()) {
		auto formatter = std::make_shared<LogFormatter>(format_pattern);
		result->SetFormatter(std::move(formatter));
	}

	// init the appender's level
	result->SetLogLevel(level);

    return result;
}

std::shared_ptr<Logger> LoggerConfDefine::GenerateInstance() const {
	assert(!name.empty());
	auto logger = base::Singleton<LoggerManager>::GetInstance().GetLogger(name);
	logger->SetLogLevel(level);

	format_pattern.empty()
		? logger->SetFormatter(SYLAR_ROOT_LOGGER()->GetFormatter())
		: logger->SetFormatter(std::make_shared<LogFormatter>(format_pattern));

	logger->ClearAllAppender();
	for (const auto& appender_define : appender_defs) {
		auto generated_appender = appender_define.GenerateInstance();
		if (generated_appender) {
			logger->AddAppender(std::move(generated_appender));
		}
	}

	/// FIXME:
	///		Consider to set parent for Logger by config file

	return logger;
}
