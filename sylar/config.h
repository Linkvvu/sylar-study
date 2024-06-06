#pragma once

#include "log.h"
#include <yaml-cpp/yaml.h>
#include <boost/lexical_cast.hpp>

#include <regex>
#include <string>
#include <exception>

namespace sylar {

// ----------------------------------------------------------------------------------------------------
// (可读词汇)类型转换类模板 & 针对STL容器的类模板偏特化

	template <typename From, typename To>
	struct LexicalCast {
		To operator()(const From& from) {
			return boost::lexical_cast<To>(from);
		}
	};

	/// TODO:
	///		处理YAML::load()的异常

	template <typename T>
	struct LexicalCast<std::vector<T>, std::string> {
		std::string operator()(const std::vector<T>& from) {
			YAML::Node node(YAML::NodeType::Sequence);
			for (const auto& item : from) {
				node.push_back(YAML::Load(LexicalCast<T, std::string>()(item)));
			}

			std::ostringstream oss;
			oss	<< node;
			return oss.str();
		}
	};

	template <typename T>
	struct LexicalCast<std::string, std::vector<T>> {
		std::vector<T> operator()(const std::string& from) {
			std::vector<T> result;

			YAML::Node node = YAML::Load(from);
			std::stringstream ss;
			for (const auto& item : node) {
				ss.str("");
				ss << item;
				result.push_back(LexicalCast<std::string, T>()(ss.str()));
			}

			return result;
		}
	};

// =====================================================================================================




	template <typename T, typename FromStrFunctor = LexicalCast<std::string, T>,
							typename ToStrFunctor = LexicalCast<T, std::string>>
	class ConfigVar {
	public:
		static bool IsValidName(const std::string& name);

		explicit ConfigVar(std::string name, const T& def_val, std::string desc = std::string());

		const std::string& GetName() const
		{ return name_; }

		const std::string& GetDescription() const
		{ return desc_; }

		void SetVal(const T& val);

		void FromString(const std::string& str);

		std::string ToString() const;

	private:
		std::string name_;
		std::string desc_;
		T val_;
	};

} // namespace sylar

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
bool sylar::ConfigVar<T, FromStrFunctor, ToStrFunctor>::IsValidName(const std::string& name) {
	std::regex pattern("^[a-zA-Z0-9._]+");
    return std::regex_match(name, pattern);
}

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
sylar::ConfigVar<T, FromStrFunctor, ToStrFunctor>::ConfigVar(std::string name, const T& def_val, std::string desc)
	: name_(std::move(name))
	, desc_(std::move(desc))
	, val_(def_val)
{
	if (!IsValidName(name_)) {
		throw std::invalid_argument("invalid config_var name: " + name_);
	}
}

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
void sylar::ConfigVar<T, FromStrFunctor, ToStrFunctor>::SetVal(const T& val) {
	if (val == val_) {
		return;
	}

	val_ = val;
}

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
void sylar::ConfigVar<T, FromStrFunctor, ToStrFunctor>::FromString(const std::string& str) {
	try {
		SetVal(FromStrFunctor()(str));
	} catch(const boost::bad_lexical_cast& e) {
		SYLAR_LOG_FMT_ERROR(SYLAR_SYS_LOGGER(), "ConfigVar::FromString exception: %s; config-name=%s, string=%s",
				e.what(), name_.c_str(), str.c_str());
	}
}

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
std::string sylar::ConfigVar<T, FromStrFunctor, ToStrFunctor>::ToString() const {
	std::string res;
	try {
		res = ToStrFunctor()(val_);
	} catch (const boost::bad_lexical_cast& e) {
		SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigVar::ToString exception: " << e.what()
										<< "; config-name=" << name_;
	}
    return res;
}
