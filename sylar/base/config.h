#pragma once

#include "log.h"
#include <yaml-cpp/yaml.h>
#include <boost/lexical_cast.hpp>

#include <regex>
#include <string>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace sylar {
namespace base {

// ----------------------------------------------------------------------------------------------------
// (可读词汇)类型转换类模板 & 针对STL容器的类模板偏特化

/**
 * @brief 转换格式标签，表示通过Yaml格式作为媒介进行转换
 * 		  用于对仿函数sylar::base::LexicalCast的Yaml转换格式进行偏特化
 */
struct YamlTag {};

/**
 * @brief 仿函数，提供由From至To的类型转换功能
 * @note 默认实现基于boost::lexical_cast，From与To必须支持"stream operator"
 * @tparam From  原类型
 * @tparam To  目标类型
 * @tparam Tag  转换格式标签
 */
template <typename From, typename To, typename Tag = nullptr_t>
struct LexicalCast {
	To operator()(const From& from) {
		return boost::lexical_cast<To>(from);
	}
};

template <typename T>
struct LexicalCast<std::vector<T>, std::string, YamlTag> {
	std::string operator()(const std::vector<T>& from) {
		// Create a root sequence node
		YAML::Node node(YAML::NodeType::Sequence);

		// 1. Convert each item to the yaml document
		// 2. Load the yaml document and save as sub YAML::Node
		for (const auto& item : from) {
			node.push_back(YAML::Load(LexicalCast<T, std::string, YamlTag>()(item)));
		}

		// convert the root YAML::Node to string which yaml document
		std::ostringstream oss;
		oss	<< node;
		return oss.str();
	}
};

template <typename T>
struct LexicalCast<std::string, std::vector<T>, YamlTag> {
	std::vector<T> operator()(const std::string& from) {
		std::vector<T> result;

		// Load the yaml document
		YAML::Node node = YAML::Load(from);

		// Make sure the document is sequence type
		if (!node.IsSequence()) {
			throw std::logic_error("expect yaml document is sequence, but it's not");
		}

		// 1. Parse each item as sub yaml document
		// 2. Create the T instance by each yaml document
		// 3. Append the T instance to the result set
		std::stringstream ss;
		for (const auto& item : node) {
			ss.str("");
			ss << item;
			result.push_back(LexicalCast<std::string, T, YamlTag>()(ss.str()));
		}

		return result;
	}
};

template <typename T>
struct LexicalCast<std::list<T>, std::string, YamlTag> {
	std::string operator()(const std::list<T>& from) {
		YAML::Node node(YAML::NodeType::Sequence);
		for (const auto& item : from) {
			node.push_back(YAML::Load(LexicalCast<T, std::string, YamlTag>()(item)));
		}

		std::ostringstream oss;
		oss	<< node;
		return oss.str();
	}
};

template <typename T>
struct LexicalCast<std::string, std::list<T>, YamlTag> {
	std::list<T> operator()(const std::string& from) {
		std::list<T> result;

		YAML::Node node = YAML::Load(from);
		if (!node.IsSequence()) {
			throw std::logic_error("expect yaml document is sequence, but it's not");
		}

		std::ostringstream oss;
		for (const auto& item : node) {
			oss.str("");
			oss << item;
			result.push_back(LexicalCast<std::string, T, YamlTag>()(oss.str()));
		}

		return result;
	}
};

template <typename T>
struct LexicalCast<std::string, std::set<T>, YamlTag> {
	std::set<T> operator()(const std::string& from) {
		std::set<T> result;

		YAML::Node node = YAML::Load(from);
		if (!node.IsSequence()) {
			throw std::logic_error("expect yaml document is sequence, but it's not");
		}

		std::ostringstream oss;
		for (const auto& item : node) {
			oss.str("");
			oss << item;
			result.emplace(LexicalCast<std::string, T, YamlTag>()(oss.str()));
		}

		return result;
	}
};

template <typename T>
struct LexicalCast<std::set<T>, std::string, YamlTag> {
	std::string operator()(const std::set<T>& from) {
		YAML::Node node(YAML::NodeType::Sequence);
		for (const auto& item : from) {
			node.push_back(YAML::Load(LexicalCast<T, std::string, YamlTag>()(item)));
		}

		std::ostringstream oss;
		oss << node;
		return oss.str();
	}
};

template <typename T>
struct LexicalCast<std::string, std::unordered_set<T>, YamlTag> {
	std::unordered_set<T> operator()(const std::string& from) {
		std::unordered_set<T> result;

		YAML::Node node = YAML::Load(from);
		if (!node.IsSequence()) {
			throw std::logic_error("expect yaml document is sequence, but it's not");
		}

		std::stringstream ss;
		for (const auto& item : node) {
			ss.str("");
			ss << item;
			result.emplace(LexicalCast<std::string, T, YamlTag>()(ss.str()));
		}

		return result;
	}
};

template <typename T>
struct LexicalCast<std::unordered_set<T>, std::string, YamlTag> {
	std::string operator()(const std::unordered_set<T>& from) {
		YAML::Node node(YAML::NodeType::Sequence);
		for (const auto& item : from) {
			node.push_back(YAML::Load(LexicalCast<T, std::string, YamlTag>()(item)));
		}

		std::ostringstream oss;
		oss << node;
		return oss.str();
	}
};

template <typename KeyT, typename ValT>
struct LexicalCast<std::string, std::map<KeyT, ValT>, YamlTag> {
	std::map<KeyT, ValT> operator()(const std::string& from) {
		std::map<KeyT, ValT> result;

		YAML::Node node = YAML::Load(from);
		if (!node.IsMap()) {
			throw std::logic_error("expect yaml document is a map, but it's not");
		}

		for (const auto& pair : node) {
			std::stringstream oss_for_val;
			oss_for_val << pair.second;
			result.emplace(LexicalCast<std::string, KeyT, YamlTag>()(pair.first.Scalar()), LexicalCast<std::string, ValT, YamlTag>()(oss_for_val.str()));
		}

		return result;
	}
};

template <typename KeyT, typename ValT>
struct LexicalCast<std::map<KeyT, ValT>, std::string, YamlTag> {
	std::string operator()(const std::map<KeyT, ValT>& from) {
		YAML::Node node(YAML::NodeType::Map);
		for (const auto& pair : from) {
			auto yaml_key_str = LexicalCast<KeyT, std::string, YamlTag>()(pair.first);
			auto yaml_val_str = LexicalCast<ValT, std::string, YamlTag>()(pair.second);
			node[yaml_key_str] = YAML::Load(yaml_val_str);
		}

		std::ostringstream oss;
		oss << node;
		return oss.str();
	}
};

template <typename KeyT, typename ValT>
struct LexicalCast<std::string, std::unordered_map<KeyT, ValT>, YamlTag> {
	std::unordered_map<KeyT, ValT> operator()(const std::string& from) {
		std::unordered_map<KeyT, ValT> result;

		YAML::Node node = YAML::Load(from);
		if (!node.IsMap()) {
			std::logic_error("expect yaml document is a map, but it's not");
		}

		for (const auto& pair : node) {
			std::ostringstream oss;
			oss << pair.second;
			result.emplace(LexicalCast<std::string, KeyT, YamlTag>()(pair.first.Scalar()), LexicalCast<std::string, ValT, YamlTag>()(oss.str()));
		}

		return result;
	}
};

template <typename KeyT, typename ValT>
struct LexicalCast<std::unordered_map<KeyT, ValT>, std::string, YamlTag> {
	std::string operator()(const std::unordered_map<KeyT, ValT>& from) {
		YAML::Node node(YAML::NodeType::Map);
		for (const auto& pair : from) {
			auto yaml_key_str = LexicalCast<KeyT, std::string, YamlTag>()(pair.first);
			auto yaml_val_str = LexicalCast<ValT, std::string, YamlTag>()(pair.second);
			node[yaml_key_str] = YAML::Load(yaml_val_str);
		}

		std::ostringstream oss;
		oss << node;
		return oss.str();
	}
};

// =====================================================================================================

class AbsConfigVar {
public:
	static bool IsValidName(const std::string& name);

	explicit AbsConfigVar(std::string name, std::string desc);

	~AbsConfigVar() noexcept = default;

	const std::string& GetName() const
	{ return name_; }

	const std::string& GetDescription() const
	{ return desc_; }

	virtual void FromString(const std::string& str) = 0;

	virtual std::string ToString() const = 0;

private:
	std::string name_;
	std::string desc_;
};



template <typename T, typename FromStrFunctor = LexicalCast<std::string, T, YamlTag>,
						typename ToStrFunctor = LexicalCast<T, std::string, YamlTag>>
class ConfigVar : public AbsConfigVar {
public:
	using Monitor = std::function<void(const T& old, const T& now)>;

	explicit ConfigVar(std::string name, const T& def_val, std::string desc = std::string());

	~ConfigVar() noexcept = default;

	uint64_t AddMonitor(Monitor m);

	void RemoveMonitor(uint64_t monitor_id) {
		std::lock_guard<std::mutex> guard(mutex_);
		monitors_.erase(monitor_id);
	}

	const T& GetValue() const
	{ std::lock_guard<std::mutex> guard(mutex_); return val_; }

	void SetVal(const T& val);

	virtual void FromString(const std::string& str) override;

	virtual std::string ToString() const override;

private:
	std::vector<Monitor> GetAllMonitor() const;

private:
	T val_;
	std::map<uint64_t, Monitor> monitors_;
	mutable std::mutex mutex_;
};


/**
 * @brief 单例配置管理类
 */
class ConfigManager {
	friend sylar::base::Singleton<ConfigManager>;
	ConfigManager() = default;
	ConfigManager(const ConfigManager&) = delete;
	ConfigManager& operator=(const ConfigManager&) = delete;

public:
	void LoadFromFile(const char* path);

	std::shared_ptr<AbsConfigVar> FindConfigVarBase(const std::string& name) const;

	/**
	 * @brief 在config集合中通过名称查找对应的目标配置变量，并尝试转换为T类型
	 *
	 * @tparam T  目标配置变量的具体类型
	 * @param name  目标配置变量的名称
	 * @return 若没有找到目标或转换操作失败，返回NULL，否则返回转换后的配置变量实例
	 */
	template <typename T>
	std::shared_ptr<ConfigVar<T>> Find(const std::string& name) const;

	/**
	 * @brief 向config集合中添加或替换已存在的配置变量
	 *
	 * @tparam T  目标配置变量的具体类型
	 * @param name  目标配置变量的名称
	 * @param def_val  目标配置变量的默认值
	 * @param desc  目标配置变量的描述信息
	 * @return 若name无效返回NULL，否则返回新添加或替换的配置变量实例
	 */
	template <typename T>
	std::shared_ptr<ConfigVar<T>> AddOrUpdate(const std::string& name, const T& def_val, const std::string& desc = "");

private:
	std::unordered_map<std::string, std::shared_ptr<AbsConfigVar>> configs_;
	mutable std::mutex mutex_;
};

} // namespace base
} // namespace sylar

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
std::vector<typename sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::Monitor>
sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::GetAllMonitor() const {
    std::vector<Monitor> result;
	std::transform(monitors_.begin(), monitors_.end(),
		std::back_inserter(result),
		[](const std::pair<uint64_t, Monitor>& pair) {
			return pair.second;
		}
	);
	return result;
}

template<typename T, typename FromStrFunctor, typename ToStrFunctor>
uint64_t sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::AddMonitor(Monitor m) {
	static uint16_t next_id = 0;
	std::lock_guard<std::mutex> guard(mutex_);

	monitors_[next_id] = std::move(m);
    return next_id++;
}

template <typename T, typename FromStrFunctor, typename ToStrFunctor>
sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::ConfigVar(std::string name, const T& def_val, std::string desc)
	: AbsConfigVar(std::move(name), std::move(desc))
	, val_(def_val)
	{}

template <typename T, typename FromStrFunctor, typename ToStrFunctor>
void sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::SetVal(const T& val) {
	std::vector<Monitor> monitors;
	T old;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (val == val_) {
			return;
		}

		old = std::move(val_);
		val_ = val;

		monitors = GetAllMonitor();
	}

	for (const auto& monitor : monitors) {
		monitor(old, val);
	}
}

template <typename T, typename FromStrFunctor, typename ToStrFunctor>
void sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::FromString(const std::string& str) {
	try {
		SetVal(FromStrFunctor()(str));
	} catch (const boost::bad_lexical_cast& e) {
		SYLAR_LOG_FMT_ERROR(SYLAR_SYS_LOGGER(), "ConfigVar::FromString lexical cast exception: %s; config-name=%s, invalid yaml doc=%s",
				e.what(), GetName().c_str(), str.c_str());
	} catch (const YAML::ParserException& e) {
		SYLAR_LOG_FMT_ERROR(SYLAR_SYS_LOGGER(), "ConfigVar::FromString parsing yaml exception: %s; config-name=%s, invalid yaml doc: %s",
				e.what(), GetName().c_str(), str.c_str());
	} catch (const std::exception& e) {
		SYLAR_LOG_FMT_ERROR(SYLAR_SYS_LOGGER(), "ConfigVar::FromString unexpected exception: %s; config-name=%s, yaml doc: %s",
			e.what(), GetName().c_str(), str.c_str());
	}
}

template <typename T, typename FromStrFunctor, typename ToStrFunctor>
std::string sylar::base::ConfigVar<T, FromStrFunctor, ToStrFunctor>::ToString() const {
	std::string res;
	T moment_value;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		moment_value = val_;
	}

	try {
		res = ToStrFunctor()(std::move(moment_value));
	} catch (const boost::bad_lexical_cast& e) {
		SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigVar::ToString lexical cast exception: " << e.what()
										<< "; config-name=" << GetName();
	} catch (const YAML::ParserException& e) {
		SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigVar::ToString parsing yaml exception: " << e.what()
										<< "; config-name=" << GetName();
	} catch (const std::exception& e) {
		SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigVar::ToString unexpected exception: " << e.what()
										<< "; config-name=" << GetName();
	}
    return res;
}

template <typename T>
std::shared_ptr<sylar::base::ConfigVar<T>> sylar::base::ConfigManager::Find(const std::string& name) const {
	std::lock_guard<std::mutex> guard(mutex_);

	auto it = configs_.find(name);
	if (it != configs_.end()) {
		auto target = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
		if (!target) {
			SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigManager::Find config name=" << name << ", exist but can not convert to "
											<< "type: " << typeid(T).name();
			return nullptr;
		}
		return target;
	}
	return nullptr;
}

template<typename T>
std::shared_ptr<sylar::base::ConfigVar<T>> sylar::base::ConfigManager::AddOrUpdate(const std::string& name, const T& def_val, const std::string& desc) {
	if (!AbsConfigVar::IsValidName(name)) {
		SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "ConfigManager::AddOrUpdate invalid config name=" << name;
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(mutex_);

	auto it = configs_.find(name);
	if (it != configs_.end()) {
		SYLAR_LOG_INFO(SYLAR_SYS_LOGGER()) << "ConfigManager::AddOrUpdate do update, config name= " << name;
	}
	auto new_instance = std::make_shared<sylar::base::ConfigVar<T>>(name, def_val, desc);
	configs_[name] = new_instance;
	return new_instance;
}
