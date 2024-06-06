#include "../config.h"
#include <gtest/gtest.h>

TEST(BasicLexicalCast, IntTest) {
	sylar::ConfigVar<int> int_config_var("int", 0);
	ASSERT_EQ(int_config_var.ToString(), "0");
	int_config_var.FromString("1");
	ASSERT_EQ(int_config_var.ToString(), "1");
}

TEST(StlLexicalCast, VectorTest) {
	sylar::ConfigVar<std::vector<int>> vec_int_config_var("vec_int", {0, 10, 100});
	ASSERT_EQ(vec_int_config_var.ToString(), "- 0\n- 10\n- 100");
	vec_int_config_var.FromString("[1000, -1000, 9999999]");
	ASSERT_EQ(vec_int_config_var.ToString(), "- 1000\n- -1000\n- 9999999");
}
