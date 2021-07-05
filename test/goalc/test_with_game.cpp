#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "game/runtime.h"
#include "goalc/listener/Listener.h"
#include "goalc/compiler/Compiler.h"

#include "inja.hpp"
#include "third-party/json.hpp"
#include "common/util/FileUtil.h"
#include <test/goalc/framework/test_runner.h>
#include "third-party/fmt/core.h"

#include <iostream>
#include <string>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <random>
#include <filesystem>
#include <regex>

class WithGameTests : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    try {
      compiler.run_test_no_load("test/goalc/source_templates/with_game/test-build-game.gc");
    } catch (std::exception& e) {
      fprintf(stderr, "caught exception %s\n", e.what());
      EXPECT_TRUE(false);
    }
    runtime_thread = std::thread((GoalTest::runtime_with_kernel));
    runner.c = &compiler;

    compiler.run_test_from_file("test/goalc/source_templates/with_game/test-load-game.gc");
    compiler.run_test_from_string("(set! *use-old-listener-print* #t)");
  }

  static void TearDownTestSuite() {
    compiler.shutdown_target();
    runtime_thread.join();
  }

  void SetUp() {
    GoalTest::createDirIfAbsent(GoalTest::getTemplateDir(testCategory));
    GoalTest::createDirIfAbsent(GoalTest::getGeneratedDir(testCategory));
  }

  void TearDown() {}

  static std::thread runtime_thread;
  static Compiler compiler;
  static GoalTest::CompilerTestRunner runner;

  std::string testCategory = "with_game";
  inja::Environment env{GoalTest::getTemplateDir(testCategory),
                        GoalTest::getGeneratedDir(testCategory)};
};

std::thread WithGameTests::runtime_thread;
Compiler WithGameTests::compiler;
GoalTest::CompilerTestRunner WithGameTests::runner;

class WithMinimalGameTests : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    try {
      compiler.run_front_end_on_string("(build-kernel)");
    } catch (std::exception& e) {
      fprintf(stderr, "caught exception %s\n", e.what());
      EXPECT_TRUE(false);
    }
    runtime_thread = std::thread((GoalTest::runtime_with_kernel));
    runner.c = &compiler;

    compiler.run_test_from_string("(dgo-load \"kernel\" global #xf #x200000)");

    const auto minimal_files = {"goal_src/engine/math/vector-h.gc"};
    for (auto& file : minimal_files) {
      compiler.run_test_from_string(fmt::format("(ml \"{}\")", file));
    }

    compiler.run_test_from_string("(set! *use-old-listener-print* #t)");
  }

  static void TearDownTestSuite() {
    compiler.shutdown_target();
    runtime_thread.join();
  }

  void SetUp() {
    GoalTest::createDirIfAbsent(GoalTest::getTemplateDir(testCategory));
    GoalTest::createDirIfAbsent(GoalTest::getGeneratedDir(testCategory));
  }

  void TearDown() {}

  static std::thread runtime_thread;
  static Compiler compiler;
  static GoalTest::CompilerTestRunner runner;

  std::string testCategory = "with_game";
  inja::Environment env{GoalTest::getTemplateDir(testCategory),
                        GoalTest::getGeneratedDir(testCategory)};
};

std::thread WithMinimalGameTests::runtime_thread;
Compiler WithMinimalGameTests::compiler;
GoalTest::CompilerTestRunner WithMinimalGameTests::runner;

namespace {
std::vector<std::string> get_test_pass_string(const std::string& name, int count) {
  return {fmt::format("Test \"{}\": {} Passes\n0\n", name, count)};
}
}  // namespace

TEST_F(WithGameTests, ReturnConstant) {
  runner.run_static_test(env, testCategory, "defun-return-constant.static.gc", {"12\n"});
}

TEST_F(WithGameTests, ReturnSymbol) {
  runner.run_static_test(env, testCategory, "defun-return-symbol.static.gc", {"42\n"});
}

TEST_F(WithGameTests, MinMax) {
  runner.run_static_test(env, testCategory, "test-min-max.gc", {"10\n"});
}

TEST_F(WithGameTests, BoxedFloat) {
  runner.run_static_test(env, testCategory, "test-bfloat.gc",
                         {"data 1.2330 print 1.2330 type bfloat\n0\n"});
}

TEST_F(WithGameTests, BasicTypeCheck) {
  runner.run_static_test(env, testCategory, "test-basic-type-check.gc", {"#f#t#t#f#t#f#t#t\n0\n"});
}

TEST_F(WithGameTests, ConditionBoolean) {
  runner.run_static_test(env, testCategory, "test-condition-boolean.gc", {"4\n"});
}

TEST_F(WithGameTests, TypeType) {
  runner.run_static_test(env, testCategory, "test-type-type.gc", {"#t#f\n0\n"});
}

TEST_F(WithGameTests, AccessInlineArray) {
  runner.run_static_test(env, testCategory, "test-access-inline-array.gc", {"1.2345\n0\n"});
}

TEST_F(WithGameTests, FindParentMethod) {
  runner.run_static_test(env, testCategory, "test-find-parent-method.gc", {"\"test pass!\"\n0\n"});
}

TEST_F(WithGameTests, Ref) {
  runner.run_static_test(env, testCategory, "test-ref.gc", {"83\n"});
}

TEST_F(WithGameTests, PairASzie) {
  runner.run_static_test(env, testCategory, "test-pair-asize.gc", {"8\n"});
}

TEST_F(WithGameTests, Last) {
  runner.run_static_test(env, testCategory, "test-last.gc", {"d\n0\n"});
}

TEST_F(WithGameTests, Sort) {
  runner.run_static_test(
      env, testCategory, "test-sort.gc",
      {"(24 16 32 56 72 1234 -34 25 654)\n(1234 654 72 56 32 25 24 16 -34)\n0\n"});
}

TEST_F(WithGameTests, Sort2) {
  runner.run_static_test(
      env, testCategory, "test-sort-2.gc",
      {"(24 16 32 56 72 1234 -34 25 654)\n(-34 16 24 25 32 56 72 654 1234)\n0\n"});
}

TEST_F(WithGameTests, Sort3) {
  runner.run_static_test(
      env, testCategory, "test-sort-3.gc",
      {"(24 16 32 56 72 1234 -34 25 654)\n(-34 16 24 25 32 56 72 654 1234)\n0\n"});
}

TEST_F(WithGameTests, PairLength) {
  runner.run_static_test(env, testCategory, "test-pair-length.gc", {"6\n"});
}

TEST_F(WithGameTests, Member1) {
  runner.run_static_test(env, testCategory, "test-member-1.gc", {"(c d)\n0\n"});
}

TEST_F(WithGameTests, Member2) {
  runner.run_static_test(env, testCategory, "test-member-2.gc", {"#f\n0\n"});
}

TEST_F(WithGameTests, Assoc1) {
  runner.run_static_test(env, testCategory, "test-assoc-1.gc", {"w\n0\n"});
}

TEST_F(WithGameTests, Assoc2) {
  runner.run_static_test(env, testCategory, "test-assoc-2.gc", {"#f\n0\n"});
}

TEST_F(WithGameTests, Assoce1) {
  runner.run_static_test(env, testCategory, "test-assoce-1.gc", {"x\n0\n"});
}

TEST_F(WithGameTests, Assoce2) {
  runner.run_static_test(env, testCategory, "test-assoce-2.gc", {"x\n0\n"});
}

TEST_F(WithGameTests, Append) {
  runner.run_static_test(env, testCategory, "test-append.gc", {"(a b c d e)\n0\n"});
}

TEST_F(WithGameTests, DeleteList) {
  runner.run_static_test(env, testCategory, "test-delete-list.gc", {"(a b d e)\n0\n"});
}

TEST_F(WithGameTests, DeleteCar) {
  runner.run_static_test(env, testCategory, "test-delete-car.gc", {"((a . b) (e . f))\n#f\n0\n"});
}

TEST_F(WithGameTests, InsertCar) {
  runner.run_static_test(env, testCategory, "test-insert-cons.gc",
                         {"((c . w) (a . b) (e . f))\n0\n"});
}

TEST_F(WithGameTests, InlineArrayClass) {
  runner.run_static_test(env, testCategory, "test-new-inline-array-class.gc", {"2824\n"});
}

TEST_F(WithGameTests, Memcpy) {
  runner.run_static_test(env, testCategory, "test-memcpy.gc", {"13\n"});
}

TEST_F(WithGameTests, Memset) {
  runner.run_static_test(env, testCategory, "test-memset.gc", {"11\n"});
}

TEST_F(WithGameTests, BintegerPrint) {
  runner.run_static_test(env, testCategory, "test-binteger-print.gc", {"-17\n0\n"});
}

TEST_F(WithGameTests, TestTests) {
  runner.run_static_test(env, testCategory, "test-tests.gc",
                         {"Test Failed On Test 0: \"unknown\"\nTest Failed On Test 0: "
                          "\"test\"\nTest \"test-of-test\": 1 Passes\n0\n"});
}

TEST_F(WithGameTests, TypeArrays) {
  runner.run_static_test(env, testCategory, "test-type-arrays.gc",
                         {"Test \"test-type-arrays\": 3 Passes\n0\n"});
}

TEST_F(WithGameTests, NumberComparison) {
  runner.run_static_test(env, testCategory, "test-number-comparison.gc",
                         {"Test \"number-comparison\": 14 Passes\n0\n"});
}

TEST_F(WithGameTests, ApproxPi) {
  runner.run_static_test(env, testCategory, "test-approx-pi.gc",
                         get_test_pass_string("approx-pi", 4));
}

TEST_F(WithGameTests, ApproxPiStack) {
  runner.run_static_test(env, testCategory, "test-approx-pi-stack.gc",
                         get_test_pass_string("approx-pi-stack", 4));
}

TEST_F(WithGameTests, DynamicType) {
  runner.run_static_test(env, testCategory, "test-dynamic-type.gc",
                         get_test_pass_string("dynamic-type", 4));
}

TEST_F(WithGameTests, StringType) {
  runner.run_static_test(env, testCategory, "test-string-type.gc",
                         get_test_pass_string("string-type", 4));
}

TEST_F(WithGameTests, NewString) {
  runner.run_static_test(env, testCategory, "test-new-string.gc",
                         get_test_pass_string("new-string", 5));
}

TEST_F(WithGameTests, AddrOf) {
  runner.run_static_test(env, testCategory, "test-addr-of.gc", get_test_pass_string("addr-of", 2));
}

TEST_F(WithGameTests, SetSelf) {
  runner.run_static_test(env, testCategory, "test-set-self.gc", {"#t\n0\n"});
}

TEST_F(WithGameTests, NewArray) {
  runner.run_static_test(env, testCategory, "test-new-array.gc",
                         get_test_pass_string("new-array", 8));
}

TEST_F(WithGameTests, NewStaticStructureIntegers) {
  runner.run_static_test(env, testCategory, "test-new-static-structure-integers.gc",
                         get_test_pass_string("new-static-structure-integers", 7));
}

TEST_F(WithGameTests, NewStaticBasic) {
  runner.run_static_test(env, testCategory, "test-new-static-basic.gc",
                         get_test_pass_string("new-static-basic", 12));
}

TEST_F(WithGameTests, VectorDot) {
  runner.run_static_test(env, testCategory, "test-vector-dot.gc",
                         get_test_pass_string("vector-dot", 1));
}

TEST_F(WithGameTests, DebuggerMemoryMap) {
  auto mem_map = compiler.listener().build_memory_map();

  // we should have gkernel main segment
  listener::MemoryMapEntry gk_main;
  EXPECT_TRUE(mem_map.lookup("gkernel", MAIN_SEGMENT, &gk_main));
  auto lookup_2 = mem_map.lookup(gk_main.start_addr + 12);
  EXPECT_TRUE(lookup_2.obj_name == "gkernel");
  EXPECT_FALSE(lookup_2.empty);
  EXPECT_EQ(lookup_2.seg_id, MAIN_SEGMENT);
}

TEST_F(WithGameTests, DebuggerDisassemble) {
  auto di = compiler.get_debugger().get_debug_info_for_object("gcommon");
  bool fail = false;
  auto result = di.disassemble_all_functions(&fail);
  // printf("Got\n%s\n", result.c_str());
  EXPECT_FALSE(fail);
}

TEST_F(WithGameTests, GameText) {
  compiler.run_test_from_string("(asm-data-file game-text \"test/test_data/test_game_text.txt\")");
  runner.run_static_test(env, testCategory, "test-game-text.gc",
                         get_test_pass_string("game-text", 5));
}

TEST_F(WithGameTests, GameCount) {
  compiler.run_test_from_string(
      "(asm-data-file game-count \"test/test_data/test_game_counts.txt\")");
  compiler.run_test_from_string("(build-dgos \"test/test_data/test_game_count_dgos.txt\")");
  compiler.run_test_from_string("(dgo-load \"game\" global #xf #x200000)");
  runner.run_static_test(env, testCategory, "test-game-count.gc",
                         get_test_pass_string("game-count", 4));
}

TEST_F(WithGameTests, BitFieldAccess) {
  runner.run_static_test(env, testCategory, "test-bitfield-access.gc",
                         {"#tfffffffffffff344f213ffffffffffffffff\n0\n"});
}

TEST_F(WithGameTests, SimpleBitField) {
  runner.run_static_test(env, testCategory, "test-set-bitfield.gc", {"#t50.3432\n0\n"});
}

TEST_F(WithGameTests, StaticBitField) {
  runner.run_static_test(env, testCategory, "test-static-bitfield.gc", {"#t50.3432\n0\n"});
}

TEST_F(WithGameTests, TrickyBitField) {
  runner.run_static_test(env, testCategory, "test-bitfield-tricky-access.gc",
                         get_test_pass_string("bitfield-tricky-access", 14));
}

TEST_F(WithGameTests, Bitfield128) {
  runner.run_static_test(env, testCategory, "test-access-bitfield128.gc",
                         {"-abcdbeef 77777777 66666666 12347890\n"
                          "-abcdbeef 77777777 66666666 00000001\n"
                          "-abcdbeef 77777777 00000002 00000001\n"
                          "-abcdbeef 00000003 00000002 00000001\n"
                          "00000004 00000003 00000002 00000001\n"
                          "12341234 00000007 00000666 -deadbeef\n"
                          "12124545 -92929292 78787878 23232323\n"
                          "00009878 00003333 00002222 00001212\n"
                          "0\n"});
}

TEST_F(WithGameTests, Math) {
  runner.run_static_test(env, testCategory, "test-math.gc", get_test_pass_string("math", 31));
}

TEST_F(WithGameTests, Sqrtf) {
  runner.run_static_test(env, testCategory, "sqrtf.gc", {"2.2360\n0\n"});
}

TEST_F(WithGameTests, StaticPairs) {
  runner.run_static_test(env, testCategory, "test-static-pair-1.gc",
                         {"(1 (w . a) beans 2 (-1 -2) twelve (a . \"test\"))\n0\n"});
}

TEST_F(WithGameTests, FancyStatic) {
  runner.run_static_test(env, testCategory, "test-fancy-static-fields.gc",
                         {"\"name\" 12 12.3400 (a b c) 5 33 4 kernel-context asdf\n0\n"});
}

TEST_F(WithGameTests, IntegerBoxedArray) {
  runner.run_static_test(
      env, testCategory, "test-integer-boxed-array.gc",
      {"0 0  1 2  2 4  3 6  4 8  5 10  6 12  7 14  8 16  9 18  10 20  11 22  12 40 6 array\n0\n"});
}

TEST_F(WithGameTests, StaticBoxedArray) {
  runner.run_static_test(env, testCategory, "test-static-boxed-array.gc",
                         {"4 asdf \"test\" (a b) 0 object 12 12\n0\n"});
}

TEST_F(WithGameTests, SizeOf) {
  runner.run_static_test(env, testCategory, "test-size-of.gc",
                         {"size of dma-bucket is 16\n"
                          "size of ints: 2 4 16\n"
                          "size of stack array is 16\n0\n"});
}

TEST_F(WithGameTests, EnumAndBitfieldTypes) {
  runner.run_static_test(env, testCategory, "test-bitfield-and-enum-types.gc",
                         {"content type: uint16\n"  // runtime type is u16
                          "content type: uint16\n"
                          "bitfield spacing: 2\n"        // u16 spacing
                          "enum spacing: 2\n"            // u16 spacing
                          "bitfield array spacing: 2\n"  // u16 spacing
                          "enum array spacing: 2\n"      // u16 spacing
                          "9\n"                          // 4 + 5
                          "sizes: 2 2\n"                 // size-of should work
                          "0\n"});
}

TEST_F(WithGameTests, Trig) {
  runner.run_static_test(env, testCategory, "test-trig.gc",
                         {"2.0000\n"    // 2 deg
                          "-45.0000\n"  // -45 deg
                          "1.2000\n"
                          "-1.2000\n"
                          "-2.2831\n"  // wrap
                          "1.2831\n"   // wrapped
                          "\n"
                          "0.4999\n"  // sin
                          "1.0000\n"
                          "-0.7071\n"
                          "-0.8659\n"
                          "\n"
                          "0.4999\n"  // sin-rads
                          "1.0000\n"
                          "-0.7071\n"
                          "-0.8660\n"
                          "\n"
                          "0.4999 -0.7071 0.8660 -1.0000\n"  // vector-sin-rad!
                          "\n"
                          "0.8660\n"  // cos-rads
                          "0.0000\n"
                          "0.7071\n"
                          "0.4999\n"
                          "\n"
                          "0.8660 0.7071 0.4999 0.0000\n"  // vector-cos-rad
                          "\n"
                          "0.4999 -0.7071 0.8660 -1.0000\n"  // vector-sincos
                          "0.8660 0.7071 0.4999 0.0000\n"
                          "\n"
                          "0.7071 0.7082\n"   // sincos with cosine bug
                          "-1.0000 0.0047\n"  // sincos, with cosine bug
                          "\n"
                          "sincos!\n"
                          "0.7071 0.7082\n"  // also with cosine bugs
                          "0.4999 0.8665\n"
                          "\n"
                          "0.0174 -3.1066 3.0892 1.2217\n"  // vector-rad<-vector deg
                          "\n"
                          "0.0174 -3.1066 3.0892 1.2217\n"  // with div/2
                          "\n"
                          "0.0000\n"  // tan
                          "1.0000\n"
                          "-0.5773\n"
                          "\n"
                          "0.4636\n"  // atan-rad
                          "0.6154\n"
                          "\n"
                          "0.4636\n"  // atan2
                          "-0.4636\n"
                          "2.6779\n"
                          "-2.6779\n"
                          "\n"
                          "1.0000\n"  // exp
                          "1.2214\n"
                          "2.7182\n"
                          "3.6692\n"
                          "31.5003\n"
                          "0.3678\n"
                          "0.6065\n"
                          "0.0963\n"
                          "\n"
                          "26.5650\n"
                          "-35.2603\n"
                          "139.1074\n"
                          "-116.5650\n"
                          "\n"
                          "44.9913\n"
                          "-59.9970\n"
                          "\n"
                          "0\n"});
}

TEST_F(WithGameTests, Vector) {
  runner.run_static_test(env, testCategory, "test-vector.gc",
                         {"[     -4.0000] [      8.0000] [     -4.0000] [      0.0000]\n"
                          "[      3.0000] [      4.0000] [      5.0000] [      1.0000]\n"
                          "[      5.0000] [     12.0000] [     21.0000] [      1.0000]\n"
                          "[     11.0000] [     14.0000] [     17.0000] [      1.0000]\n"
                          "[     -9.0000] [    -10.0000] [    -11.0000] [      1.0000]\n"
                          "[      0.2000] [      0.3333] [      0.4285] [      1.0000]\n"
                          "[     -2.0000] [     -4.0000] [     -6.0000] [      1.0000]\n"
                          "[      3.0000] [      4.0000] [      5.0000] [      1.0000]\n"
                          "[     11.0000] [     14.0000] [     17.0000] [      1.0000]\n"
                          "[     -9.0000] [    -10.0000] [    -11.0000] [      1.0000]\n"
                          "[     -0.5000] [     -1.0000] [     -1.5000] [      1.0000]\n"
                          "[     -1.0000] [     -2.0000] [     -3.0000] [      1.0000]\n"
                          "part 2\n"
                          "[     -1.0000] [     -2.0000] [     -3.0000] [      4.0000]\n"
                          "#f #t #t #f\n"
                          "4.0000\n"
                          "part 3\n"
                          "[      1.5000] [      2.5000] [      3.5000] [      1.0000]\n"
                          "[      5.0000] [      6.0000] [      7.0000] [      1.0000]\n"
                          "7.0000\n"
                          "49.0000\n"
                          "7.0000\n"
                          "[     14.0000] [     21.0000] [     42.0000] [     10.0000]\n"
                          "3.5000\n"
                          "-1 2 -3 4\n"
                          "[     -1.0000] [      2.0000] [     -3.0000] [      4.0000]\n"
                          "0.0000 720.0000 360.0000 0.0000\n"
                          "-20.0006 20.0006 20.0006 20.0006\n"
                          "0\n"});
}

TEST_F(WithGameTests, InlinedPackedBasics) {
  runner.run_static_test(env, testCategory, "inlined-packed-basics.gc",
                         {"rec stride: 48\n"
                          "offset of float: 40\n"
                          "offset: 16\n"
                          "offset2: 184\n"
                          "array: #x0\n"
                          "first: #x0\n"
                          "0\n"});
}

TEST_F(WithGameTests, PartialDefineTypeField) {
  runner.run_static_test(env, testCategory, "test-partial-define-type-field.gc",
                         {"#f\n"
                          "0\n"});
}

// VECTOR FLOAT TESTS

// ---- One off Tests

TEST_F(WithGameTests, VFOuterProduct) {
  runner.run_static_test(env, testCategory, "test-vector-outer-product.gc",
                         {"(-4.0000, 8.0000, -4.0000, 999.0000)\n0\n"});
}

TEST_F(WithGameTests, VFLoadAndStore) {
  runner.run_static_test(env, testCategory, "test-vf-load-and-store.gc", {"2.0000\n0\n"});
}

TEST_F(WithGameTests, VFSimpleMath) {
  runner.run_static_test(env, testCategory, "test-basic-vector-math.gc", {"54.0000\n0\n"});
}

TEST_F(WithGameTests, VFLoadStatic) {
  runner.run_static_test(env, testCategory, "test-load-static-vector.gc", {"5.3000\n0\n"});
}

TEST_F(WithGameTests, XMMSpill) {
  runner.run_static_test(env, testCategory, "test-xmm-spill.gc", {"253.0000\n0\n"});
}

TEST_F(WithGameTests, BoxedArrayIndex) {
  runner.run_static_test(env, testCategory, "test-boxed-array-index.gc", {"18\n0\n"});
}

TEST_F(WithGameTests, LocalVars) {
  runner.run_static_test(env, testCategory, "test-local-vars.gc",
                         {"y is \"test\", x is 12, z is 3.2000\n0\n"});
}

TEST_F(WithGameTests, ShortCircuit) {
  runner.run_static_test(env, testCategory, "test-short-circuit.gc",
                         get_test_pass_string("short-circuit", 13));
}

TEST_F(WithGameTests, VectorFloatToInt) {
  runner.run_static_test(env, testCategory, "test-vector-int-float-conversions.gc",
                         {"1.0000 -2.0000 3.0000 4.0000\n"
                          "1 -2 3 4\n"
                          "1.0000 -2.0000 3.0000 4.0000\n"
                          "0\n"});
}

TEST_F(WithGameTests, PWShifts) {
  runner.run_static_test(env, testCategory, "test-pw-shifts.gc",
                         {"ffffffffaafffff0 ffffffffbbfffff0 ffffffffccfffff0 ffffffffddfffff0\n"
                          "ffffffffeabffffc ffffffffeefffffc fffffffff33ffffc fffffffff77ffffc\n"
                          "ffffffffafffff00 ffffffffbfffff00 ffffffffcfffff00 ffffffffdfffff00\n"
                          "2bffffc0 2fffffc0 33ffffc0 37ffffc0\n"
                          "0\n"});
}

TEST_F(WithGameTests, StaticArray) {
  runner.run_static_test(env, testCategory, "test-static-array.gc",
                         {"1 2 -10\n"
                          "0\n"});
}

TEST_F(WithGameTests, StaticInlineArray) {
  runner.run_static_test(
      env, testCategory, "test-static-inline-array.gc",
      {"test-basic-for-static-inline test-basic-for-static-inline #x4 #x4 \"hello\"\n"
       "#x0 #x0 \"hello\"\n"
       "0\n"});
}

TEST_F(WithGameTests, StaticArrayField) {
  runner.run_static_test(env, testCategory, "test-static-array-field.gc",
                         {"\"ghjkl\"\n"
                          "0\n"});
}

TEST_F(WithGameTests, TypeReference) {
  runner.run_static_test(env, testCategory, "test-type-ref.gc",
                         {"string #t basic some-unknown-type 20 0\n"
                          "0\n"});
}

TEST_F(WithGameTests, StaticFieldInlineArray) {
  runner.run_static_test(env, testCategory, "test-static-field-inline-arrays.gc",
                         {"\"second\" \"first\"\n"
                          "basic-elt #x4 #x4\n"
                          "two\n"
                          "\"second\" \"first\"\n"
                          "basic-elt #x4 #x4\n"
                          "two\n"
                          "0\n"});
}

TEST_F(WithGameTests, I128Simple) {
  runner.run_static_test(env, testCategory, "test-i128-simple.gc",
                         {"[0] #x707172737475767778797a7b7c7d7e7f\n"
                          "[1] #x606162636465666768696a6b6c6d6e6f\n"
                          "[2] #x505152535455565758595a5b5c5d5e5f\n"
                          "[3] #x404142434445464748494a4b4c4d4e4f\n"
                          "[4] #x303132333435363738393a3b3c3d3e3f\n"
                          "[5] #x202122232425262728292a2b2c2d2e2f\n"
                          "[6] #x101112131415161718191a1b1c1d1e1f\n"
                          "[7] #x000102030405060708090a0b0c0d0e0f\n"
                          "12344321\n"});
}

// TODO - add tests

TEST_F(WithGameTests, Pextlw) {
  runner.run_static_test(env, testCategory, "test-pextlw.gc",
                         {"#x07060504171615140302010013121110\n"
                          "#x0f0e0d0c1f1e1d1c0b0a09081b1a1918\n"
                          "#x07060504030201001716151413121110\n"
                          "#x1f1e1d1c1b1a19180f0e0d0c0b0a0908\n"
                          "#x0d0c0908050401001d1c191815141110\n"
                          "#xffffffff00000000ffffffff00000000\n"
                          "#x00090000000000fefffffffe000002ff\n"
                          "0\n"});
}

TEST_F(WithGameTests, Matrix) {
  runner.run_static_test(env, testCategory, "test-matrix.gc",
                         {"mat-mult\n"
                          "\t[     80.0000] [     70.0000] [     60.0000] [     50.0000]\n"
                          "\t[    240.0000] [    214.0000] [    188.0000] [    162.0000]\n"
                          "\t[    400.0000] [    358.0000] [    316.0000] [    274.0000]\n"
                          "\t[    560.0000] [    502.0000] [    444.0000] [    386.0000]\n"
                          "transpose\n"
                          "\t[      1.0000] [      5.0000] [      9.0000] [     13.0000]\n"
                          "\t[      2.0000] [      6.0000] [     10.0000] [     14.0000]\n"
                          "\t[      3.0000] [      7.0000] [     11.0000] [     15.0000]\n"
                          "\t[      4.0000] [      8.0000] [     12.0000] [     16.0000]\n"
                          "inv-4x4\n"
                          "\t[      1.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      1.0000] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      1.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "axis-angle\n"
                          "\t[      0.9961] [      0.0506] [      0.0715] [      0.0000]\n"
                          "\t[     -0.0393] [      0.9876] [     -0.1516] [      0.0000]\n"
                          "\t[     -0.0783] [      0.1482] [      0.9858] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "\n"
                          "\t[      1.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.9848] [     -0.1736] [      0.0000]\n"
                          "\t[      0.0000] [      0.1736] [      0.9848] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "\n"
                          "\t[      0.9848] [      0.0000] [      0.1736] [      0.0000]\n"
                          "\t[      0.0000] [      1.0000] [      0.0000] [      0.0000]\n"
                          "\t[     -0.1736] [      0.0000] [      0.9848] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "\n"
                          "\t[      0.9848] [     -0.1736] [      0.0000] [      0.0000]\n"
                          "\t[      0.1736] [      0.9848] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      1.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "\n"
                          "\t[      1.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      1.0000] [      0.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      1.0000] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      1.0000]\n"
                          "\n"
                          "3x3-inverse\n"
                          "\t[      0.0952] [      0.0238] [     -0.0833] [      0.0000]\n"
                          "\t[      0.3333] [     -0.1666] [      0.0833] [      0.0000]\n"
                          "\t[      0.0476] [      0.2619] [      0.0833] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "\n"
                          "\t[      0.0952] [      0.3333] [      0.0476] [      0.0000]\n"
                          "\t[      0.0238] [     -0.1666] [      0.2619] [      0.0000]\n"
                          "\t[     -0.0833] [      0.0833] [      0.0833] [      0.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "transform-many\n"
                          "vec-array\n"
                          "\t[    -13.0000] [      5.0000] [     10.0000] [      1.0000]\n"
                          "\t[    -22.0000] [     14.0000] [     28.0000] [      1.0000]\n"
                          "\t[    -31.0000] [     23.0000] [     46.0000] [      1.0000]\n"
                          "\t[    -40.0000] [     32.0000] [     64.0000] [      1.0000]\n"
                          "\t[    -49.0000] [     41.0000] [     82.0000] [      1.0000]\n"
                          "\t[    -58.0000] [     50.0000] [    100.0000] [      1.0000]\n"
                          "\t[    -67.0000] [     59.0000] [    118.0000] [      1.0000]\n"
                          "\t[    -76.0000] [     68.0000] [    136.0000] [      1.0000]\n"
                          "\t[    -85.0000] [     77.0000] [    154.0000] [      1.0000]\n"
                          "\t[    -94.0000] [     86.0000] [    172.0000] [      1.0000]\n"
                          "\t[   -103.0000] [     95.0000] [    190.0000] [      1.0000]\n"
                          "\t[      0.0000] [      0.0000] [      0.0000] [      0.0000]\n"
                          "0\n"});
}

TEST_F(WithGameTests, WeirdMultiply) {
  runner.run_static_test(env, testCategory, "test-weird-multiplies.gc",
                         {"2 100000002\n"
                          "100000000 100000000\n"
                          "55555552 -3 7ffffffffffffffb -5\n"
                          "0\n"});
}

TEST_F(WithGameTests, Function128) {
  runner.run_static_test(
      env, testCategory, "test-function128.gc",
      {"#<vector       1.0000       2.0000       3.0000       4.0000 @ #x400000003f800000>\n"
       "#<vector       1.0000      20.0000       3.0000       4.0000 @ #x41a000003f800000>\n"
       "#<vector      10.0000       2.0000       3.0000       4.0000 @ #x4000000041200000>\n"
       " 0 1 2 3 4 5 6 7 8 9 a b c d e\n"
       "arg0: 1 arg2: 2\n"
       "0\n"});
}

TEST_F(WithGameTests, AddrOfVar) {
  runner.run_static_test(env, testCategory, "test-addr-of-var.gc",
                         {"x: 25 y: 35 z: 35\n"
                          "x: 13 y: 35 z: 15\n"
                          "0\n"});
}

TEST_F(WithGameTests, SoundName) {
  runner.run_static_test(env, testCategory, "test-sound-name.gc",
                         {"#t #f #f\n"
                          "0\n"});
}

TEST_F(WithGameTests, StaticLambda) {
  runner.run_static_test(env, testCategory, "test-static-lambda.gc", {"Add: 30 sub: -10\n0\n"});
}

TEST_F(WithGameTests, MethodReplace) {
  runner.run_static_test(env, testCategory, "test-method-replace.gc",
                         {"relocate! foo: 123 heap: 1 name: 2\n0\n"});
}

TEST_F(WithGameTests, Behaviors) {
  runner.run_static_test(env, testCategory, "test-behaviors.gc",
                         {"function self: 123\n"
                          "method obj: 456 self: 123\n0\n"});
}

TEST(TypeConsistency, TypeConsistency) {
  Compiler compiler;
  compiler.enable_throw_on_redefines();
  compiler.run_test_no_load("test/goalc/source_templates/with_game/test-build-game.gc");
  compiler.run_test_no_load("decompiler/config/all-types.gc");
}

struct VectorFloatRegister {
  float x = 0;
  float y = 0;
  float z = 0;
  float w = 0;

  void setJson(nlohmann::json& data, std::string vectorKey) {
    data[fmt::format("{}x", vectorKey)] = x;
    data[fmt::format("{}y", vectorKey)] = y;
    data[fmt::format("{}z", vectorKey)] = z;
    data[fmt::format("{}w", vectorKey)] = w;
  }

  float getBroadcastElement(emitter::Register::VF_ELEMENT bc, float defValue) {
    switch (bc) {
      case emitter::Register::VF_ELEMENT::X:
        return x;
      case emitter::Register::VF_ELEMENT::Y:
        return y;
      case emitter::Register::VF_ELEMENT::Z:
        return z;
      case emitter::Register::VF_ELEMENT::W:
        return w;
      default:
        return defValue;
    }
  }

  std::string toGOALFormat() {
    std::string answer = fmt::format("({:.4f}, {:.4f}, {:.4f}, {:.4f})", x, y, z, w);
    // {fmt} formats negative 0 as "-0.000", just going to flip any negative zeros to positives as I
    // don't think is an OpenGOAL issue
    // Additionally, GOAL doesn't have -/+ Inf it seems, so replace with NaN. -nan is also just NaN
    return std::regex_replace(std::regex_replace(answer, std::regex("-0.0000"), "0.0000"),
                              std::regex("nan|inf|-nan|-inf"), "NaN");
  }

  std::string toGOALFormat(float) {
    std::string answer = fmt::format("{:.4f}", x);
    // {fmt} formats negative 0 as "-0.000", just going to flip any negative zeros to positives as I
    // don't think is an OpenGOAL issue
    // Additionally, GOAL doesn't have -/+ Inf it seems, so replace with NaN
    return std::regex_replace(std::regex_replace(answer, std::regex("-0.0000"), "0.0000"),
                              std::regex("nan|inf|-nan|-inf"), "NaN");
  }
};

struct VectorFloatTestCase {
  VectorFloatRegister dest = {11, 22, 33, 44};
  int destinationMask = -1;
  emitter::Register::VF_ELEMENT bc = emitter::Register::VF_ELEMENT::NONE;

  std::string getOperationBroadcast() {
    switch (bc) {
      case emitter::Register::VF_ELEMENT::X:
        return ".x";
      case emitter::Register::VF_ELEMENT::Y:
        return ".y";
      case emitter::Register::VF_ELEMENT::Z:
        return ".z";
      case emitter::Register::VF_ELEMENT::W:
        return ".w";
      default:
        return "";
    }
  }

  virtual VectorFloatRegister getExpectedResult() = 0;
  virtual void setJson(nlohmann::json& data, std::string func) = 0;

  virtual ~VectorFloatTestCase() = default;
};

struct VectorFloatTestCase_TwoOperand : VectorFloatTestCase {
  VectorFloatRegister input1 = {1.5, -1.5, 0.0, 100.5};
  VectorFloatRegister input2 = {-5.5, -0.0, 10.0, 7.5};

  std::function<float(float, float)> operation;

  VectorFloatRegister getExpectedResult() {
    VectorFloatRegister expectedResult;
    expectedResult.x = destinationMask & 0b0001
                           ? operation(input1.x, input2.getBroadcastElement(bc, input2.x))
                           : dest.x;
    expectedResult.y = destinationMask & 0b0010
                           ? operation(input1.y, input2.getBroadcastElement(bc, input2.y))
                           : dest.y;
    expectedResult.z = destinationMask & 0b0100
                           ? operation(input1.z, input2.getBroadcastElement(bc, input2.z))
                           : dest.z;
    expectedResult.w = destinationMask & 0b1000
                           ? operation(input1.w, input2.getBroadcastElement(bc, input2.w))
                           : dest.w;
    return expectedResult;
  }

  void setJson(nlohmann::json& data, std::string func) {
    input1.setJson(data, "v1");
    input2.setJson(data, "v2");
    dest.setJson(data, "dest");
    data["operation"] = fmt::format(func);
    if (destinationMask == -1) {
      data["destinationMask"] = false;
    } else {
      data["destinationMask"] = fmt::format("{:b}", destinationMask);
    }
  }
};

std::vector<VectorFloatTestCase_TwoOperand> vectorMathCaseGen_TwoOperand() {
  std::vector<VectorFloatTestCase_TwoOperand> cases = {};
  for (int i = 0; i <= 15; i++) {
    VectorFloatTestCase_TwoOperand testCase = VectorFloatTestCase_TwoOperand();
    testCase.destinationMask = i;
    cases.push_back(testCase);
    // Re-add each case with each broadcast variant
    for (int j = 0; j < 4; j++) {
      VectorFloatTestCase_TwoOperand testCaseBC = VectorFloatTestCase_TwoOperand();
      testCaseBC.destinationMask = i;
      testCaseBC.bc = static_cast<emitter::Register::VF_ELEMENT>(j);
      cases.push_back(testCaseBC);
    }
  }
  return cases;
}

class VectorFloatParameterizedTestFixtureWithRunner_TwoOperand
    : public WithMinimalGameTests,
      public ::testing::WithParamInterface<VectorFloatTestCase_TwoOperand> {
 protected:
  std::string templateFile = "test-vector-math-2-operand.template.gc";
};

// NOTE - an excellent article -
// https://www.sandordargo.com/blog/2019/04/24/parameterized-testing-with-gtest

// --- 2 Operand VF Operations

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperand, VF_ADD_XYZW_DEST) {
  VectorFloatTestCase_TwoOperand testCase = GetParam();
  testCase.operation = [](float x, float y) { return x + y; };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".add{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-add{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperand, VF_SUB_XYZW_DEST) {
  VectorFloatTestCase_TwoOperand testCase = GetParam();
  testCase.operation = [](float x, float y) { return x - y; };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".sub{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-sub{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperand, VF_MUL_XYZW_DEST) {
  VectorFloatTestCase_TwoOperand testCase = GetParam();
  testCase.operation = [](float x, float y) { return x * y; };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".mul{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-mul{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperand, VF_MIN_XYZW_DEST) {
  VectorFloatTestCase_TwoOperand testCase = GetParam();
  testCase.operation = [](float x, float y) { return fmin(x, y); };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".min{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-min{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperand, VF_MAX_XYZW_DEST) {
  VectorFloatTestCase_TwoOperand testCase = GetParam();
  testCase.operation = [](float x, float y) { return fmax(x, y); };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".max{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-max{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

INSTANTIATE_TEST_SUITE_P(WithGameTests_VectorFloatTests,
                         VectorFloatParameterizedTestFixtureWithRunner_TwoOperand,
                         ::testing::ValuesIn(vectorMathCaseGen_TwoOperand()));

// --- 1 Operand VF Operations

struct VectorFloatTestCase_SingleOperand : VectorFloatTestCase {
  VectorFloatRegister input1 = {1.5, -1.5, 0.0, 100.5};

  std::function<float(float)> operation;

  VectorFloatRegister getExpectedResult() {
    VectorFloatRegister expectedResult;
    expectedResult.x =
        destinationMask & 0b0001 ? operation(input1.getBroadcastElement(bc, input1.x)) : dest.x;
    expectedResult.y =
        destinationMask & 0b0010 ? operation(input1.getBroadcastElement(bc, input1.y)) : dest.y;
    expectedResult.z =
        destinationMask & 0b0100 ? operation(input1.getBroadcastElement(bc, input1.z)) : dest.z;
    expectedResult.w =
        destinationMask & 0b1000 ? operation(input1.getBroadcastElement(bc, input1.w)) : dest.w;
    return expectedResult;
  }

  void setJson(nlohmann::json& data, std::string func) {
    input1.setJson(data, "v1");
    dest.setJson(data, "dest");
    data["operation"] = fmt::format(func);
    if (destinationMask == -1) {
      data["destinationMask"] = false;
    } else {
      data["destinationMask"] = fmt::format("{:b}", destinationMask);
    }
  }
};

std::vector<VectorFloatTestCase_SingleOperand> vectorMathCaseGen_SingleOperand_NoBroadcast() {
  std::vector<VectorFloatTestCase_SingleOperand> cases = {};
  for (int i = 0; i <= 15; i++) {
    VectorFloatTestCase_SingleOperand testCase = VectorFloatTestCase_SingleOperand();
    testCase.destinationMask = i;
    cases.push_back(testCase);
  }
  return cases;
}

class VectorFloatParameterizedTestFixtureWithRunner_SingleOperand
    : public WithMinimalGameTests,
      public ::testing::WithParamInterface<VectorFloatTestCase_SingleOperand> {
 protected:
  std::string templateFile = "test-vector-math-1-operand.template.gc";
};

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_SingleOperand, VF_ABS_DEST) {
  VectorFloatTestCase_SingleOperand testCase = GetParam();
  testCase.operation = [](float x) { return fabs(x); };

  nlohmann::json data;
  testCase.setJson(data, ".abs.vf");

  std::string outFile = runner.test_file_name("vector-math-abs-{}.generated.gc");
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

INSTANTIATE_TEST_SUITE_P(WithGameTests_VectorFloatTests,
                         VectorFloatParameterizedTestFixtureWithRunner_SingleOperand,
                         ::testing::ValuesIn(vectorMathCaseGen_SingleOperand_NoBroadcast()));

// --- 2 Operand With ACC VF Operations
// TODO - these pollute tests, it would be nicer long-term to move these into the framework
// namespace

struct VectorFloatTestCase_TwoOperandACC : VectorFloatTestCase {
  VectorFloatRegister input1 = {1.5, -1.5, 0.0, 100.5};
  VectorFloatRegister input2 = {-5.5, -0.0, 10.0, 7.5};
  VectorFloatRegister acc = {-15.5, -0.0, 20.0, 70.5};

  std::function<float(float, float, float)> operation;

  VectorFloatRegister getExpectedResult() {
    VectorFloatRegister expectedResult;
    expectedResult.x = destinationMask & 0b0001
                           ? operation(input1.x, input2.getBroadcastElement(bc, input2.x), acc.x)
                           : dest.x;
    expectedResult.y = destinationMask & 0b0010
                           ? operation(input1.y, input2.getBroadcastElement(bc, input2.y), acc.y)
                           : dest.y;
    expectedResult.z = destinationMask & 0b0100
                           ? operation(input1.z, input2.getBroadcastElement(bc, input2.z), acc.z)
                           : dest.z;
    expectedResult.w = destinationMask & 0b1000
                           ? operation(input1.w, input2.getBroadcastElement(bc, input2.w), acc.w)
                           : dest.w;
    return expectedResult;
  }

  void setJson(nlohmann::json& data, std::string func) {
    input1.setJson(data, "v1");
    input2.setJson(data, "v2");
    acc.setJson(data, "acc");
    dest.setJson(data, "dest");
    data["operation"] = fmt::format(func);
    if (destinationMask == -1) {
      data["destinationMask"] = false;
    } else {
      data["destinationMask"] = fmt::format("{:b}", destinationMask);
    }
  }
};

// TODO - unnecessary duplication for these generation methods, use some templates (only the type
// changes)
std::vector<VectorFloatTestCase_TwoOperandACC> vectorMathCaseGen_TwoOperandACC() {
  std::vector<VectorFloatTestCase_TwoOperandACC> cases = {};
  for (int i = 0; i <= 15; i++) {
    VectorFloatTestCase_TwoOperandACC testCase = VectorFloatTestCase_TwoOperandACC();
    testCase.destinationMask = i;
    cases.push_back(testCase);
    // Re-add each case with each broadcast variant
    for (int j = 0; j < 4; j++) {
      VectorFloatTestCase_TwoOperandACC testCaseBC = VectorFloatTestCase_TwoOperandACC();
      testCaseBC.destinationMask = i;
      testCaseBC.bc = static_cast<emitter::Register::VF_ELEMENT>(j);
      cases.push_back(testCaseBC);
    }
  }
  return cases;
}

class VectorFloatParameterizedTestFixtureWithRunner_TwoOperandACC
    : public WithMinimalGameTests,
      public ::testing::WithParamInterface<VectorFloatTestCase_TwoOperandACC> {
 protected:
  std::string templateFile = "test-vector-math-2-operand-acc.template.gc";
};

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperandACC, VF_MUL_ADD_XYZW_DEST) {
  VectorFloatTestCase_TwoOperandACC testCase = GetParam();
  testCase.operation = [](float x, float y, float acc) { return (x * y) + acc; };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".add.mul{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-add-mul{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperandACC, VF_MUL_SUB_XYZW_DEST) {
  VectorFloatTestCase_TwoOperandACC testCase = GetParam();
  testCase.operation = [](float x, float y, float acc) { return acc - (x * y); };

  nlohmann::json data;
  testCase.setJson(data, fmt::format(".sub.mul{}.vf", testCase.getOperationBroadcast()));

  std::string outFile = runner.test_file_name(
      fmt::format("vector-math-sub-mul{}-{{}}.generated.gc", testCase.getOperationBroadcast()));
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat())});
}

INSTANTIATE_TEST_SUITE_P(WithGameTests_VectorFloatTests,
                         VectorFloatParameterizedTestFixtureWithRunner_TwoOperandACC,
                         ::testing::ValuesIn(vectorMathCaseGen_TwoOperandACC()));

// ---- Two Operand Quotient Register Operations

struct VectorFloatTestCase_TwoOperandQuotient : VectorFloatTestCase {
  VectorFloatRegister input1 = {1.5, -1.5, 0.0, 100.5};
  VectorFloatRegister input2 = {-5.5, -0.0, 10.0, 10.0};

  int fsf = 0;
  int ftf = 0;

  std::function<float(float, float)> operation;

  VectorFloatRegister getExpectedResult() {
    float operand1 =
        input1.getBroadcastElement(static_cast<emitter::Register::VF_ELEMENT>(fsf), input1.x);
    float operand2 =
        input2.getBroadcastElement(static_cast<emitter::Register::VF_ELEMENT>(ftf), input2.x);
    float result = operation(operand1, operand2);
    VectorFloatRegister expectedResult;
    expectedResult.x = result;
    expectedResult.y = result;
    expectedResult.z = result;
    expectedResult.w = result;
    return expectedResult;
  }

  void setJson(nlohmann::json& data, std::string func) {
    input1.setJson(data, "v1");
    input2.setJson(data, "v2");
    dest.setJson(data, "dest");
    data["operation"] = fmt::format(func);
    data["ftf"] = fmt::format("{:b}", ftf);
    data["fsf"] = fmt::format("{:b}", fsf);
  }
};

std::vector<VectorFloatTestCase_TwoOperandQuotient> vectorMathCaseGen_TwoOperandQuotient() {
  std::vector<VectorFloatTestCase_TwoOperandQuotient> cases = {};
  for (int i = 0; i <= 3; i++) {
    VectorFloatTestCase_TwoOperandQuotient testCase = VectorFloatTestCase_TwoOperandQuotient();
    testCase.fsf = i;
    for (int j = 0; j <= 3; j++) {
      testCase.ftf = j;
      cases.push_back(testCase);
    }
  }
  return cases;
}

class VectorFloatParameterizedTestFixtureWithRunner_TwoOperandQuotient
    : public WithMinimalGameTests,
      public ::testing::WithParamInterface<VectorFloatTestCase_TwoOperandQuotient> {
 protected:
  std::string templateFile = "test-vector-math-division.template.gc";
};

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_TwoOperandQuotient, VF_DIV_FTF_FSF) {
  VectorFloatTestCase_TwoOperandQuotient testCase = GetParam();
  testCase.operation = [](float x, float y) { return x / y; };

  nlohmann::json data;
  testCase.setJson(data, ".div.vf");

  std::string outFile = runner.test_file_name("vector-math-div-{}.generated.gc");
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat(
                                              testCase.getExpectedResult().x))});
}

INSTANTIATE_TEST_SUITE_P(WithGameTests_VectorFloatTests,
                         VectorFloatParameterizedTestFixtureWithRunner_TwoOperandQuotient,
                         ::testing::ValuesIn(vectorMathCaseGen_TwoOperandQuotient()));

// ---- Single Operand Quotient Register Operations

struct VectorFloatTestCase_OneOperandQuotient : VectorFloatTestCase {
  VectorFloatRegister input1 = {2, -2, 0.0, 100};

  int ftf = 0;

  std::function<float(float)> operation;

  VectorFloatRegister getExpectedResult() {
    float operand1 =
        input1.getBroadcastElement(static_cast<emitter::Register::VF_ELEMENT>(ftf), input1.x);
    float result = operation(operand1);
    VectorFloatRegister expectedResult;
    expectedResult.x = result;
    expectedResult.y = result;
    expectedResult.z = result;
    expectedResult.w = result;
    return expectedResult;
  }

  void setJson(nlohmann::json& data, std::string func) {
    input1.setJson(data, "v1");
    dest.setJson(data, "dest");
    data["operation"] = fmt::format(func);
    data["ftf"] = fmt::format("{:b}", ftf);
  }
};

std::vector<VectorFloatTestCase_OneOperandQuotient> vectorMathCaseGen_OneOperandQuotient() {
  std::vector<VectorFloatTestCase_OneOperandQuotient> cases = {};
  for (int i = 0; i <= 3; i++) {
    VectorFloatTestCase_OneOperandQuotient testCase = VectorFloatTestCase_OneOperandQuotient();
    testCase.ftf = i;
    cases.push_back(testCase);
  }
  return cases;
}

class VectorFloatParameterizedTestFixtureWithRunner_OneOperandQuotient
    : public WithMinimalGameTests,
      public ::testing::WithParamInterface<VectorFloatTestCase_OneOperandQuotient> {
 protected:
  std::string templateFile = "test-vector-math-sqrt.template.gc";
};

TEST_P(VectorFloatParameterizedTestFixtureWithRunner_OneOperandQuotient, VF_SQRT_FTF) {
  VectorFloatTestCase_OneOperandQuotient testCase = GetParam();
  testCase.operation = [](float x) { return sqrt(x); };

  nlohmann::json data;
  testCase.setJson(data, ".sqrt.vf");

  std::string outFile = runner.test_file_name("vector-math-sqrt-{}.generated.gc");
  env.write(templateFile, data, outFile);
  runner.run_test(testCategory, outFile,
                  {fmt::format("{}\n0\n", testCase.getExpectedResult().toGOALFormat(
                                              testCase.getExpectedResult().x))});
}

INSTANTIATE_TEST_SUITE_P(WithGameTests_VectorFloatTests,
                         VectorFloatParameterizedTestFixtureWithRunner_OneOperandQuotient,
                         ::testing::ValuesIn(vectorMathCaseGen_OneOperandQuotient()));
