#include "config.h"

using namespace sylar;

bool sylar::AbsConfigVar::IsValidName(const std::string& name) {
	std::regex pattern("^[a-zA-Z0-9._]+$");
    return std::regex_match(name, pattern);
}

AbsConfigVar::AbsConfigVar(std::string name, std::string desc)
	: name_(std::move(name))
	, desc_(std::move(desc))
{
	if (!IsValidName(name_)) {
		throw std::invalid_argument("invalid config_var name: " + name_);
	}
}

static void ListAllNode(const YAML::Node& root, std::vector<std::pair<const std::string, const YAML::Node>>& node_set, const std::string& name = "") {
	if (AbsConfigVar::IsValidName(name) || name.empty()) {
		if (!name.empty()) {
			node_set.emplace_back(name, root);
		}
		if (root.IsMap()) {
			for (auto it = root.begin(); it != root.end(); ++it) {
				ListAllNode(it->second, node_set, name.empty() ? it->first.Scalar() : name + '.' + it->first.Scalar());
			}
		}
	} else {
		throw std::invalid_argument("Failed to parse the yaml document, has unexpected character");
	}
}

void sylar::ConfigManager::LoadFromFile(const char* path) {
	YAML::Node doc_root = YAML::LoadFile(path);
	std::vector<std::pair<const std::string, const YAML::Node>> node_set;

	try {
		ListAllNode(doc_root, node_set);
	} catch (const std::exception& e) {
		SYLAR_LOG_FMT_ERROR(SYLAR_SYS_LOGGER(), "ConfigManager::LoadFromFile exception: %s; filename=%s", e.what(), path);
		return;
	}

	for (const auto& pair : node_set) {
		std::shared_ptr<AbsConfigVar> cur_config_variable = FindConfigVarBase(pair.first);
		if (cur_config_variable) {
			if (pair.second.IsScalar()) {
				cur_config_variable->FromString(pair.second.Scalar());
			} else {
				std::ostringstream oss;
				oss << pair.second;
				cur_config_variable->FromString(oss.str());
			}
		}
	}
}

std::shared_ptr<AbsConfigVar> ConfigManager::FindConfigVarBase(const std::string& name) const {
	auto it = configs_.find(name);
	return it == configs_.end() ? nullptr : it->second;
}
