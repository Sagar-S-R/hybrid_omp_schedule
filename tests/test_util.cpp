/// @file test_util.cpp
/// Tests for cpulist parsing and string helpers.

#include "minitest.hpp"

#include "hybrid_sched/util.hpp"

#include <string>
#include <vector>

TEST("util: parse_cpulist empty") {
    const auto v = hs::util::parse_cpulist("");
    CHECK(v.empty());
}

TEST("util: parse_cpulist single") {
    const auto v = hs::util::parse_cpulist("5");
    CHECK_EQ(v.size(), std::size_t{1});
    CHECK_EQ(v[0], 5);
}

TEST("util: parse_cpulist range") {
    const auto v = hs::util::parse_cpulist("0-3");
    CHECK_EQ(v.size(), std::size_t{4});
    CHECK_EQ(v[0], 0);
    CHECK_EQ(v[3], 3);
}

TEST("util: parse_cpulist mixed") {
    const auto v = hs::util::parse_cpulist("0-3,8,10-11");
    CHECK_EQ(v.size(), std::size_t{7});
    CHECK_EQ(v[0], 0);
    CHECK_EQ(v[3], 3);
    CHECK_EQ(v[4], 8);
    CHECK_EQ(v[5], 10);
    CHECK_EQ(v[6], 11);
}

TEST("util: parse_cpulist deduplication") {
    const auto v = hs::util::parse_cpulist("0,0,1,1-2");
    CHECK_EQ(v.size(), std::size_t{3});
    CHECK_EQ(v[0], 0);
    CHECK_EQ(v[1], 1);
    CHECK_EQ(v[2], 2);
}

TEST("util: parse_cpulist invalid returns empty") {
    const auto v = hs::util::parse_cpulist("abc");
    CHECK(v.empty());
}

TEST("util: format_cpulist roundtrip") {
    const std::string original = "0-3,8,10-11";
    const auto v = hs::util::parse_cpulist(original);
    const std::string formatted = hs::util::format_cpulist(v);
    const auto v2 = hs::util::parse_cpulist(formatted);
    CHECK_EQ(v.size(), v2.size());
    for (std::size_t i = 0; i < v.size(); ++i) CHECK_EQ(v[i], v2[i]);
}

TEST("util: trim") {
    CHECK_EQ(hs::util::trim("  hello  "), std::string("hello"));
    CHECK_EQ(hs::util::trim("\t\n"), std::string(""));
    CHECK_EQ(hs::util::trim("no spaces"), std::string("no spaces"));
}

TEST("util: split") {
    const auto parts = hs::util::split("a,b,c", ',');
    CHECK_EQ(parts.size(), std::size_t{3});
    CHECK_EQ(parts[0], std::string("a"));
    CHECK_EQ(parts[2], std::string("c"));
}

TEST("util: split single element") {
    const auto parts = hs::util::split("hello", ',');
    CHECK_EQ(parts.size(), std::size_t{1});
    CHECK_EQ(parts[0], std::string("hello"));
}
