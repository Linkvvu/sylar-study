#include "../config.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace sylar;

class ConfigEnv : public testing::Test {
public:
	explicit ConfigEnv()
		: config_(base::Singleton<base::ConfigManager>::GetInstance())
		{}

	virtual void SetUp() override {
		std::set<double> tmp = {1.1, 2.1};
		config_.AddOrUpdate<std::string>("person.name", "unknown name", "person's name");
		config_.AddOrUpdate("person.age", 0, "person's age");
		config_.AddOrUpdate<std::vector<std::string>>("person.email", {"test@163.com", "test@gmail.com"}, "person's email");
		config_.AddOrUpdate<std::list<std::string>>("person.phone", {"110", "120", "911"}, "person's phone");
		config_.AddOrUpdate<std::set<double>>("std.set", {1.1, 2.2, 3.3}, "");
		config_.AddOrUpdate<std::map<int, std::string>>("std.map", {{1, "X"}, {2, "Y"}, {3, "Z"}}, "");
	}

protected:
	base::ConfigManager& config_;
};

TEST(BasicLexicalCast, IntTest) {
	base::ConfigVar<int> int_config_var("int", 0);
	ASSERT_EQ(int_config_var.ToString(), "0");
	int_config_var.FromString("1");
	ASSERT_EQ(int_config_var.ToString(), "1");
}

TEST(StlLexicalCast, VectorTest) {
	base::ConfigVar<std::vector<int>> vec_int_config_var("vec_int", {0, 10, 100});
	ASSERT_EQ(vec_int_config_var.ToString(), "- 0\n- 10\n- 100");
	vec_int_config_var.FromString("[1000, -1000, 9999999]");
	ASSERT_EQ(vec_int_config_var.ToString(), "- 1000\n- -1000\n- 9999999");
}

TEST(LoadLoggerConfig, Basic) {
	base::Singleton<base::ConfigManager>::GetInstance()
		.LoadFromFile("/home/haovvu/projs/sylar-study/conf/loggers.yaml");
	SYLAR_LOG_DEBUG(SYLAR_GET_LOGGER("std_out_logger")) << "Test for the strand output stream Logger" << std::endl;
	SYLAR_LOG_INFO(SYLAR_GET_LOGGER("file_logger")) << "Test for the file Logger" << std::endl;
}

TEST_F(ConfigEnv, LoadConfigsFromFile) {
	config_.LoadFromFile("/home/haovvu/projs/sylar-study/conf/config.yml");
	EXPECT_EQ(config_.Find<std::string>("person.name")->GetValue(), "CXX");
	EXPECT_EQ(config_.Find<int>("person.age")->GetValue(), 22);
	EXPECT_THAT(config_.Find<std::vector<std::string>>("person.email")->GetValue(),
		::testing::UnorderedElementsAre(std::string("wuhaocoding@163.com"), std::string("wuhaocoding@gmail.com")));
	EXPECT_THAT(config_.Find<std::list<std::string>>("person.phone")->GetValue(),
		::testing::UnorderedElementsAre(std::string("110"), "911", std::string("120")));
	EXPECT_THAT(config_.Find<std::set<double>>("std.set")->GetValue(),
		::testing::UnorderedElementsAre(10.10, 20.20));

	auto map = config_.Find<std::map<int, std::string>>("std.map")->GetValue();
	EXPECT_THAT(map, ::testing::ElementsAre(std::make_pair(1, std::string("XXX")),
			std::make_pair(2, std::string("YYY")),
			std::make_pair(3, std::string("ZZZ"))));
}
