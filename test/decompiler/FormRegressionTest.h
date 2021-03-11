#pragma once

#include <memory>
#include "gtest/gtest.h"
#include "decompiler/Disasm/InstructionParser.h"
#include "decompiler/util/DecompilerTypeSystem.h"
#include "decompiler/Function/Function.h"
#include "decompiler/ObjectFile/LinkedObjectFile.h"

namespace decompiler {
struct TypeCast;
}

class FormRegressionTest : public ::testing::Test {
 protected:
  static std::unique_ptr<decompiler::InstructionParser> parser;
  static std::unique_ptr<decompiler::DecompilerTypeSystem> dts;

  static void SetUpTestCase();
  static void TearDownTestCase();

  struct TestData {
    explicit TestData(int instrs) : func(0, instrs) {}
    decompiler::Function func;
    decompiler::LinkedObjectFile file;

    void add_string_at_label(const std::string& label_name, const std::string& data);
  };

  std::unique_ptr<TestData> make_function(
      const std::string& code,
      const TypeSpec& function_type,
      bool do_expressions,
      bool allow_pairs = false,
      const std::string& method_name = "",
      const std::vector<std::pair<std::string, std::string>>& strings = {},
      const std::unordered_map<int, std::vector<decompiler::TypeCast>>& casts = {});

  void test(const std::string& code,
            const std::string& type,
            const std::string& expected,
            bool do_expressions,
            bool allow_pairs = false,
            const std::string& method_name = "",
            const std::vector<std::pair<std::string, std::string>>& strings = {},
            const std::unordered_map<int, std::vector<decompiler::TypeCast>>& casts = {});

  void test_no_expr(const std::string& code,
                    const std::string& type,
                    const std::string& expected,
                    bool allow_pairs = false,
                    const std::string& method_name = "",
                    const std::vector<std::pair<std::string, std::string>>& strings = {},
                    const std::unordered_map<int, std::vector<decompiler::TypeCast>>& casts = {}) {
    test(code, type, expected, false, allow_pairs, method_name, strings, casts);
  }

  void test_with_expr(
      const std::string& code,
      const std::string& type,
      const std::string& expected,
      bool allow_pairs = false,
      const std::string& method_name = "",
      const std::vector<std::pair<std::string, std::string>>& strings = {},
      const std::unordered_map<int, std::vector<decompiler::TypeCast>>& casts = {}) {
    test(code, type, expected, true, allow_pairs, method_name, strings, casts);
  }

  void test_final_function(
      const std::string& code,
      const std::string& type,
      const std::string& expected,
      bool allow_pairs = false,
      const std::vector<std::pair<std::string, std::string>>& strings = {},
      const std::unordered_map<int, std::vector<decompiler::TypeCast>>& casts = {});

  std::unordered_map<int, std::vector<decompiler::TypeCast>> parse_cast_json(const std::string& in);
};