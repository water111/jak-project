#include <memory>
#include "gtest/gtest.h"
#include "decompiler/Disasm/InstructionParser.h"
#include "decompiler/Disasm/DecompilerLabel.h"
#include "decompiler/Function/Function.h"
#include "decompiler/ObjectFile/ObjectFileDB.h"
#include "decompiler/IR2/variable_naming.h"
#include "decompiler/IR2/cfg_builder.h"
#include "common/goos/PrettyPrinter.h"

using namespace decompiler;

class DecompilerRegressionTest : public ::testing::Test {
 protected:
  static std::unique_ptr<InstructionParser> parser;
  static std::unique_ptr<DecompilerTypeSystem> dts;

  static void SetUpTestCase() {
    parser = std::make_unique<InstructionParser>();
    dts = std::make_unique<DecompilerTypeSystem>();
    dts->parse_type_defs({"decompiler", "config", "all-types.gc"});
  }

  static void TearDownTestCase() {
    parser.reset();
    dts.reset();
    parser.reset();
  }

  struct TestData {
    explicit TestData(int instrs) : func(0, instrs) {}
    Function func;
    LinkedObjectFile file;

    void add_string_at_label(const std::string& label_name, const std::string& data) {
      // first, align segment 1:
      while (file.words_by_seg.at(1).size() % 4) {
        file.words_by_seg.at(1).push_back(LinkedWord(0));
      }

      // add string type tag:
      LinkedWord type_tag(0);
      type_tag.kind = LinkedWord::Kind::TYPE_PTR;
      type_tag.symbol_name = "string";
      file.words_by_seg.at(1).push_back(type_tag);
      int string_start = 4 * int(file.words_by_seg.at(1).size());

      // add size
      file.words_by_seg.at(1).push_back(LinkedWord(int(data.length())));

      // add string:
      std::vector<char> bytes;
      bytes.resize(((data.size() + 1 + 3) / 4) * 4);
      for (size_t i = 0; i < data.size(); i++) {
        bytes[i] = data[i];
      }
      for (size_t i = 0; i < bytes.size() / 4; i++) {
        auto word = ((uint32_t*)bytes.data())[i];
        file.words_by_seg.at(1).push_back(LinkedWord(word));
      }
      for (int i = 0; i < 3; i++) {
        file.words_by_seg.at(1).push_back(LinkedWord(0));
      }
      // will be already null terminated.

      for (auto& label : file.labels) {
        if (label.name == label_name) {
          label.target_segment = 1;
          label.offset = string_start;
          return;
        }
      }

      EXPECT_TRUE(false);
    }
  };

  std::unique_ptr<TestData> make_function(
      const std::string& code,
      const TypeSpec& function_type,
      bool allow_pairs = false,
      const std::vector<std::pair<std::string, std::string>>& strings = {}) {
    dts->type_prop_settings.locked = true;
    dts->type_prop_settings.reset();
    dts->type_prop_settings.allow_pair = allow_pairs;
    auto program = parser->parse_program(code);
    //  printf("prg:\n%s\n\n", program.print().c_str());
    auto test = std::make_unique<TestData>(program.instructions.size());
    test->file.words_by_seg.resize(3);
    test->file.labels = program.labels;
    test->func.ir2.env.file = &test->file;
    test->func.instructions = program.instructions;
    test->func.guessed_name.set_as_global("test-function");

    for (auto& str : strings) {
      test->add_string_at_label(str.first, str.second);
    }

    test->func.basic_blocks = find_blocks_in_function(test->file, 0, test->func);
    test->func.analyze_prologue(test->file);
    test->func.cfg = build_cfg(test->file, 0, test->func);
    EXPECT_TRUE(test->func.cfg->is_fully_resolved());

    auto ops = convert_function_to_atomic_ops(test->func, program.labels);
    test->func.ir2.atomic_ops = std::make_shared<FunctionAtomicOps>(std::move(ops));
    test->func.ir2.atomic_ops_succeeded = true;

    if (test->func.run_type_analysis_ir2(function_type, *dts, test->file, {})) {
      test->func.ir2.has_type_info = true;
    } else {
      EXPECT_TRUE(false);
    }

    test->func.ir2.reg_use = analyze_ir2_register_usage(test->func);
    test->func.ir2.has_reg_use = true;

    auto result =
        run_variable_renaming(test->func, test->func.ir2.reg_use, *test->func.ir2.atomic_ops, *dts);
    if (result.has_value()) {
      test->func.ir2.env.set_local_vars(*result);
    } else {
      EXPECT_TRUE(false);
    }

    build_initial_forms(test->func);
    EXPECT_TRUE(test->func.ir2.top_form);

    return test;
  }

  void test(const std::string& code,
            const std::string& type,
            const std::string& expected,
            bool allow_pairs = false,
            const std::vector<std::pair<std::string, std::string>>& strings = {}) {
    auto ts = dts->parse_type_spec(type);
    auto test = make_function(code, ts, allow_pairs, strings);
    auto expected_form =
        pretty_print::get_pretty_printer_reader().read_from_string(expected, false).as_pair()->car;
    auto actual_form =
        pretty_print::get_pretty_printer_reader()
            .read_from_string(test->func.ir2.top_form->to_form(test->func.ir2.env).print(), false)
            .as_pair()
            ->car;
    if (expected_form != actual_form) {
      printf("Got:\n%s\n\nExpected\n%s\n", actual_form.print().c_str(),
             expected_form.print().c_str());
    }

    EXPECT_TRUE(expected_form == actual_form);
  }
};

std::unique_ptr<InstructionParser> DecompilerRegressionTest::parser;
std::unique_ptr<DecompilerTypeSystem> DecompilerRegressionTest::dts;

TEST_F(DecompilerRegressionTest, StringTest) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L100:\n"
      "    or v0, a0, r0\n"
      "L101:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  auto test = make_function(func, TypeSpec("function", {TypeSpec("none")}), false,
                            {{"L100", "testing-string"}, {"L101", "testing-string-2"}});

  EXPECT_EQ(test->file.get_goal_string_by_label(test->file.get_label_by_name("L100")),
            "testing-string");
  EXPECT_EQ(test->file.get_goal_string_by_label(test->file.get_label_by_name("L101")),
            "testing-string-2");
}

TEST_F(DecompilerRegressionTest, SimplestTest) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    or v0, a0, r0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function object object)";
  std::string expected = "(set! v0-0 a0-0)";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, FloatingPointBasic) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L345:\n"
      "    daddiu sp, sp, -16\n"
      "    sd fp, 8(sp)\n"
      "    or fp, t9, r0\n"
      "    lwc1 f0, L345(fp)\n"
      "    mtc1 f1, a0\n"
      "    div.s f0, f0, f1\n"
      "    mfc1 v0, f0\n"
      "    ld fp, 8(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 16";
  std::string type = "(function float float)";
  std::string expected =
      "(begin\n"
      "  (set! f0-0 (l.f L345))\n"
      "  (set! f1-0 (gpr->fpr a0-0))\n"
      "  (set! f0-1 (/.s f0-0 f1-0))\n"
      "  (set! v0-0 (fpr->gpr f0-1))\n"
      "  )";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Op3) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L308:\n"
      "    mult3 v0, a0, a1\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function int int int)";
  std::string expected = "(set! v0-0 (*.si a0-0 a1-0))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Division) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L307:\n"
      "    div a0, a1\n"
      "    mflo v0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function int int int)";
  std::string expected = "(set! v0-0 (/.si a0-0 a1-0))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Ash) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L305:\n"
      "    or v1, a0, r0\n"
      "    bgezl a1, L306\n"
      "    dsllv v0, v1, a1\n"

      "    dsubu a0, r0, a1\n"
      "    dsrav v0, v1, a0\n"
      "L306:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0\n"
      "    sll r0, r0, 0\n"
      "    sll r0, r0, 0";
  std::string type = "(function int int int)";
  std::string expected = "(begin (set! v1-0 a0-0) (set! v0-0 (ash.si v1-0 a1-0)))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Abs) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L301:\n"
      "    or v0, a0, r0\n"
      "    bltzl v0, L302\n"
      "    dsubu v0, r0, v0\n"

      "L302:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function int int)";
  std::string expected = "(begin (set! v0-0 a0-0) (set! v0-1 (abs v0-0)))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Min) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    or v0, a0, r0\n"
      "    or v1, a1, r0 \n"
      "    slt a0, v0, v1\n"
      "    movz v0, v1, a0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function int int int)";
  std::string expected = "(begin (set! v0-0 a0-0) (set! v1-0 a1-0) (set! v0-1 (min.si v0-0 v1-0)))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, Max) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L299:\n"
      "    or v0, a0, r0\n"
      "    or v1, a1, r0\n"
      "    slt a0, v0, v1\n"
      "    movn v0, v1, a0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function int int int)";
  std::string expected = "(begin (set! v0-0 a0-0) (set! v1-0 a1-0) (set! v0-1 (max.si v0-0 v1-0)))";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, FormatString) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L343:\n"
      "    daddiu sp, sp, -32\n"
      "    sd ra, 0(sp)\n"
      "    sd fp, 8(sp)\n"
      "    or fp, t9, r0\n"
      "    sq gp, 16(sp)\n"

      "    or gp, a0, r0\n"
      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L343\n"
      "    lwc1 f0, 0(gp)\n"
      "    mfc1 a2, f0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v0, gp, r0 \n"
      "    ld ra, 0(sp)\n"
      "    ld fp, 8(sp)\n"
      "    lq gp, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 32";
  std::string type = "(function bfloat bfloat)";
  std::string expected =
      "(begin\n"
      "  (set! gp-0 a0-0)\n"
      "  (set! t9-0 format)\n"
      "  (set! a0-1 '#t)\n"
      "  (set! a1-0 L343)\n"
      "  (set! f0-0 (l.f gp-0))\n"
      "  (set! a2-0 (fpr->gpr f0-0))\n"
      "  (set! v0-0 (call!))\n"
      "  (set! v0-1 gp-0)\n"
      "  )";
  test(func, type, expected, false, {{"L343", "~f"}});
}

TEST_F(DecompilerRegressionTest, WhileLoop) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L285:\n"
      "    lwu v1, -4(a0)\n"
      "    lw a0, object(s7)\n"

      "L286:\n"
      "    bne v1, a1, L287\n"
      "    or a2, s7, r0\n"

      "    daddiu v1, s7, #t\n"
      "    or v0, v1, r0\n"
      "    beq r0, r0, L288\n"
      "    sll r0, r0, 0\n"

      "    or v1, r0, r0\n"
      "L287:\n"
      "    lwu v1, 4(v1)\n"
      "    bne v1, a0, L286\n"
      "    sll r0, r0, 0\n"
      "    or v0, s7, r0\n"
      "L288:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function basic type symbol)";
  std::string expected =
      "(begin\n"
      "  (set! v1-0 (l.wu (+ a0-0 -4)))\n"
      "  (set! a0-1 object)\n"
      "  (until\n"
      "   (begin (set! v1-0 (l.wu (+ v1-0 4))) (= v1-0 a0-1))\n"
      "   (if\n"
      "    (= v1-0 a1-0)\n"
      "    (return ((begin (set! v1-1 '#t) (set! v0-0 v1-1))) ((set! v1-0 0)))\n"
      "    )\n"
      "   )\n"
      "  (set! v0-1 '#f)\n"
      "  )";
  test(func, type, expected);
}

// Note - this test looks weird because or's aren't fully processed at this point.
TEST_F(DecompilerRegressionTest, Or) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L280:\n"
      "    lw v1, object(s7)\n"

      "L281:\n"
      "    bne a0, a1, L282\n"
      "    or a2, s7, r0\n"

      "    daddiu v1, s7, #t\n"
      "    or v0, v1, r0\n"
      "    beq r0, r0, L284\n"
      "    sll r0, r0, 0\n"

      "    or v1, r0, r0\n"

      "L282:\n"
      "    lwu a0, 4(a0)\n"
      "    dsubu a2, a0, v1\n"
      "    daddiu a3, s7, 8\n"
      "    movn a3, s7, a2\n"
      "    bnel s7, a3, L283\n"
      "    or a2, a3, r0\n"

      "    daddiu a2, s7, 8\n"
      "    movn a2, s7, a0\n"

      "L283:\n"
      "    beq s7, a2, L281\n"
      "    sll r0, r0, 0\n"

      "    or v0, s7, r0\n"

      "L284:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function type type symbol)";
  std::string expected =
      "(begin\n"
      "  (set! v1-0 object)\n"
      "  (until\n"
      "   (begin\n"
      "    (or\n"
      "     (begin\n"
      "      (set! a0-0 (l.wu (+ a0-0 4)))\n"
      "      (set! a3-0 (= a0-0 v1-0))\n"
      "      (truthy a3-0)\n"  // this sets a2-0, the unused result of the OR. it gets a separate
                               // variable because it's not used.
      "      )\n"
      "     (set! a2-1 (zero? a0-0))\n"  // so this should be a2-1.
      "     )\n"
      "    (truthy a2-1)\n"
      "    )\n"
      "   (if\n"
      "    (= a0-0 a1-0)\n"
      "    (return ((begin (set! v1-1 '#t) (set! v0-0 v1-1))) ((set! v1-0 0)))\n"
      "    )\n"
      "   )\n"
      "  (set! v0-1 '#f)\n"
      "  )";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, DynamicMethodAccess) {
  std::string func =
      "    sll r0, r0, 0\n"

      "L275:\n"
      "    dsll v1, a1, 2\n"
      "    daddu v1, v1, a0\n"
      "    lwu v1, 16(v1)\n"

      "L276:\n"
      "    lw a2, object(s7)\n"
      "    bne a0, a2, L277\n"
      "    or a2, s7, r0\n"

      "    lw v1, nothing(s7)\n"
      "    or v0, v1, r0\n"
      "    beq r0, r0, L279\n"
      "    sll r0, r0, 0\n"

      "    or v1, r0, r0\n"

      "L277:\n"
      "    lwu a0, 4(a0)\n"
      "    dsll a2, a1, 2\n"
      "    daddu a2, a2, a0\n"
      "    lwu v0, 16(a2)\n"
      "    bne v0, r0, L278\n"
      "    or a2, s7, r0\n"

      "    lw v1, nothing(s7)\n"
      "    or v0, v1, r0\n"
      "    beq r0, r0, L279\n"
      "    sll r0, r0, 0\n"

      "    or v1, r0, r0\n"

      "L278:\n"
      "    beq v0, v1, L276\n"
      "    sll r0, r0, 0\n"

      "    or v1, s7, r0\n"

      "L279:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function type int function)";
  std::string expected =
      "(begin\n"
      "  (set! v1-0 (sll a1-0 2))\n"
      "  (set! v1-1 (+ v1-0 a0-0))\n"
      "  (set! v1-2 (l.wu (+ v1-1 16)))\n"  // get the method of the given type.
      "  (until\n"
      "   (!= v0-1 v1-2)\n"  // actually goes after the body, so it's fine to refer to v0-1/v1-2
      "   (if\n"
      "    (begin\n"
      "     (if\n"
      "      (begin (set! a2-0 object) (= a0-0 a2-0))\n"  // if we reached the top
      "      (return ((begin (set! v1-3 nothing) (set! v0-0 v1-3))) ((set! v1-2 0)))\n"  // return
                                                                                         // nothing.
      "      )\n"
      "     (set! a0-0 (l.wu (+ a0-0 4)))\n"  // get next parent type
      "     (set! a2-2 (sll a1-0 2))\n"       // fancy access
      "     (set! a2-3 (+ a2-2 a0-0))\n"
      "     (set! v0-1 (l.wu (+ a2-3 16)))\n"  // get method (in v0-1, the same var as loop
                                               // condition)
      "     (zero? v0-1)\n"                    // is it defined?
      "     )\n"
      "    (return ((begin (set! v1-4 nothing) (set! v0-2 v1-4))) ((set! v1-2 0)))\n"  // also
                                                                                       // return
                                                                                       // nothing.
      "    )\n"
      "   )\n"
      "  (set! v1-5 '#f)\n"
      "  )";
  test(func, type, expected);
}

TEST_F(DecompilerRegressionTest, SimpleLoopMergeCheck) {
  std::string func =
      "    sll r0, r0, 0\n"

      "L272:\n"
      "    addiu v1, r0, 0\n"
      "    beq r0, r0, L274\n"
      "    sll r0, r0, 0\n"

      "L273:\n"
      "    sll r0, r0, 0\n"
      "    sll r0, r0, 0\n"
      "    lw a0, 2(a0)\n"
      "    daddiu v1, v1, 1\n"

      "L274:\n"
      "    slt a2, v1, a1\n"
      "    bne a2, r0, L273\n"
      "    sll r0, r0, 0\n"

      "    or v1, s7, r0\n"
      "    or v1, s7, r0\n"
      "    lw v0, -2(a0)\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function pair int)";
  std::string expected =
      "(begin\n"
      "  (set! v1-0 0)\n"
      "  (while\n"
      "   (<.si v1-0 a1-0)\n"
      "   (nop!)\n"
      "   (nop!)\n"
      "   (set! a0-0 (l.w (+ a0-0 2)))\n"  // should have merged
      "   (set! v1-0 (+ v1-0 1))\n"        // also should have merged
      "   )\n"
      "  (set! v1-1 '#f)\n"
      "  (set! v1-2 '#f)\n"
      "  (set! v0-0 (l.w (+ a0-0 -2)))\n"
      "  )";
  test(func, type, expected, true);
}

TEST_F(DecompilerRegressionTest, And) {
  std::string func =
      "    sll r0, r0, 0\n"

      "L266:\n"
      "    daddiu v1, s7, -10\n"
      "    bne a0, v1, L267\n"
      "    sll r0, r0, 0\n"

      "    addiu v0, r0, 0\n"
      "    beq r0, r0, L271\n"
      "    sll r0, r0, 0\n"

      "L267:\n"
      "    lw v1, 2(a0)\n"
      "    addiu v0, r0, 1\n"
      "    beq r0, r0, L269\n"
      "    sll r0, r0, 0\n"

      "L268:\n"
      "    daddiu v0, v0, 1\n"
      "    lw v1, 2(v1)\n"

      "L269:\n"
      "    daddiu a0, s7, -10\n"
      "    dsubu a0, v1, a0\n"
      "    daddiu a1, s7, 8\n"
      "    movz a1, s7, a0\n"
      "    beql s7, a1, L270\n"
      "    or a0, a1, r0\n"

      "    dsll32 a0, v1, 30\n"
      "    slt a1, a0, r0\n"
      "    daddiu a0, s7, 8\n"
      "    movz a0, s7, a1\n"

      "L270:\n"
      "    bne s7, a0, L268\n"
      "    sll r0, r0, 0\n"

      "    or v1, s7, r0\n"

      "L271:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function pair int)";
  std::string expected =
      "(cond\n"
      "  ((begin (set! v1-0 '()) (= a0-0 v1-0)) (set! v0-0 0))\n"  // should be a case, not a return
      "  (else\n"
      "   (set! v1-1 (l.w (+ a0-0 2)))\n"  // v1-1 iteration.
      "   (set! v0-1 1)\n"                 // v0-1 count
      "   (while\n"
      "    (begin\n"
      "     (and\n"
      "      (begin (set! a0-1 '()) (set! a1-0 (!= v1-1 a0-1)) (truthy a1-0))\n"  // check v1-1
      "      (begin (set! a0-3 (sll v1-1 62)) (set! a0-2 (<0.si a0-3)))\n"        // check v1-1
      "      )\n"
      "     (truthy a0-2)\n"  // this variable doesn't appear, but is set by the and.
      "     )\n"
      "    (set! v0-1 (+ v0-1 1))\n"        // merged (and the result)
      "    (set! v1-1 (l.w (+ v1-1 2)))\n"  // also merged.
      "    )\n"
      "   (set! v1-2 '#f)\n"  // while's false, I think.
      "   )\n"
      "  )";
  test(func, type, expected, true);
}

TEST_F(DecompilerRegressionTest, FunctionCall) {
  // nmember
  std::string func =
      "    sll r0, r0, 0\n"

      "L252:\n"
      "    daddiu sp, sp, -48\n"
      "    sd ra, 0(sp)\n"
      "    sq s5, 16(sp)\n"
      "    sq gp, 32(sp)\n"

      "    or s5, a0, r0\n"
      "    or gp, a1, r0\n"
      "    beq r0, r0, L254\n"
      "    sll r0, r0, 0\n"

      "L253:\n"
      "    lw gp, 2(gp)\n"

      "L254:\n"
      "    daddiu v1, s7, -10\n"
      "    dsubu v1, gp, v1\n"
      "    daddiu a0, s7, 8\n"
      "    movn a0, s7, v1\n"
      "    bnel s7, a0, L255\n"
      "    or v1, a0, r0\n"

      "    lw t9, name=(s7)\n"
      "    lw a0, -2(gp)\n"
      "    or a1, s5, r0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"

      "L255:\n"
      "    beq s7, v1, L253\n"
      "    sll r0, r0, 0\n"

      "    or v1, s7, r0\n"
      "    daddiu v1, s7, -10\n"
      "    beq gp, v1, L256\n"
      "    or v0, s7, r0\n"

      "    or v0, gp, r0\n"

      "L256:\n"
      "    ld ra, 0(sp)\n"
      "    lq gp, 32(sp)\n"
      "    lq s5, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 48";
  std::string type = "(function basic object object)";
  std::string expected =
      "(if\n"  // this if needs regrouping.
      "  (begin\n"
      "   (set! s5-0 a0-0)\n"  // s5-0 is the thing to check
      "   (set! gp-0 a1-0)\n"  // gp-0 is the list
      "   (while\n"
      "    (begin\n"
      "     (or\n"
      "      (begin (set! v1-0 '()) (set! a0-1 (= gp-0 v1-0)) (truthy a0-1))\n"  // got empty list.
      "      (begin\n"
      "       (set! t9-0 name=)\n"
      "       (set! a0-2 (l.w (+ gp-0 -2)))\n"
      "       (set! a1-1 s5-0)\n"
      "       (set! v0-0 (call!))\n"
      "       (set! v1-1 v0-0)\n"  // name match
      "       )\n"
      "      )\n"
      "     (not v1-1)\n"  // no name match AND no empty list.
      "     )\n"
      "    (set! gp-0 (l.w (+ gp-0 2)))\n"  // get next (merged)
      "    )\n"
      "   (set! v1-2 '#f)\n"  // while loop thing
      "   (set! v1-3 '())\n"  //
      "   (!= gp-0 v1-3)\n"   // IF CONDITION
      "   )\n"
      "  (set! v0-2 gp-0)\n"  // not empty, so return the result
      "  )";                  // the (set! v0 #f) from the if is added later.
  test(func, type, expected, true);
}