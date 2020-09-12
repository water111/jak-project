#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "game/runtime.h"
#include "goalc/listener/Listener.h"
#include "goalc/compiler/Compiler.h"

TEST(CompilerAndRuntime, ConstructCompiler) {
  Compiler compiler;
}

TEST(CompilerAndRuntime, StartRuntime) {
  std::thread runtime_thread([]() { exec_runtime(0, nullptr); });

  listener::Listener listener;
  while (!listener.is_connected()) {
    listener.connect_to_target();
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
  }

  listener.send_reset(true);
  runtime_thread.join();
}

TEST(CompilerAndRuntime, SendProgram) {
  std::thread runtime_thread([]() { exec_runtime(0, nullptr); });
  Compiler compiler;
  compiler.run_test("goal_src/test/test-return-integer-1.gc");
  compiler.shutdown_target();
  runtime_thread.join();
}

namespace {
std::string escaped_string(const std::string& in) {
  std::string result;
  for (auto x : in) {
    switch (x) {
      case '\n':
        result.append("\\n");
        break;
      case '\t':
        result.append("\\t");
        break;
      default:
        result.push_back(x);
    }
  }
  return result;
}

struct CompilerTestRunner {
  Compiler* c = nullptr;

  struct Test {
    std::vector<std::string> expected, actual;
    std::string test_name;
    bool auto_pass = false;
  };

  std::vector<Test> tests;

  void run_test(const std::string& test_file,
                const std::vector<std::string>& expected,
                MatchParam<int> truncate = {}) {
    fprintf(stderr, "Testing %s\n", test_file.c_str());
    auto result = c->run_test("goal_src/test/" + test_file);
    if (!truncate.is_wildcard) {
      for (auto& x : result) {
        x = x.substr(0, truncate.value);
      }
    }

    EXPECT_EQ(result, expected);
    tests.push_back({expected, result, test_file, false});
  }

  void run_always_pass(const std::string& test_file) {
    c->run_test("goal_src/test/" + test_file);
    tests.push_back({{}, {}, test_file, true});
  }

  void print_summary() {
    fmt::print("~~ Compiler Test Summary for {} tests... ~~\n", tests.size());
    int passed = 0;
    int passable = 0;
    int auto_pass = 0;
    for (auto& test : tests) {
      if (test.auto_pass) {
        auto_pass++;
        fmt::print("[{:40}] AUTO-PASS!\n", test.test_name);
      } else {
        passable++;
        if (test.expected == test.actual) {
          fmt::print("[{:40}] PASS!\n", test.test_name);
          passed++;
        } else {
          fmt::print("[{:40}] FAIL!\n", test.test_name);
          fmt::print("expected:\n");
          for (auto& x : test.expected) {
            fmt::print(" \"{}\"\n", escaped_string(x));
          }
          fmt::print("result:\n");
          for (auto& x : test.actual) {
            fmt::print(" \"{}\"\n", escaped_string(x));
          }
        }
      }
    }
    fmt::print("Total: passed {}/{} passable tests, {} auto-passed\n", passed, passable, auto_pass);
  }
};

}  // namespace

TEST(CompilerAndRuntime, BuildGame) {
  std::thread runtime_thread([]() { exec_runtime(0, nullptr); });
  Compiler compiler;

  fprintf(stderr, "about to run test\n");

  try {
    compiler.run_test("goal_src/test/test-build-game.gc");
  } catch (std::exception& e) {
    fprintf(stderr, "caught exception %s\n", e.what());
    EXPECT_TRUE(false);
  }
  fprintf(stderr, "DONE!\n");

  // todo, tests after loading the game.

  compiler.shutdown_target();
  runtime_thread.join();
}

TEST(CompilerAndRuntime, CompilerTests) {
  std::thread runtime_thread([]() { exec_runtime(0, nullptr); });
  Compiler compiler;
  CompilerTestRunner runner;
  runner.c = &compiler;

  runner.run_test("test-return-integer-1.gc", {"4886718345\n"});
  runner.run_test("test-return-integer-2.gc", {"23\n"});
  runner.run_test("test-return-integer-3.gc", {"-17\n"});
  runner.run_test("test-return-integer-4.gc", {"-2147483648\n"});
  runner.run_test("test-return-integer-5.gc", {"-2147483649\n"});
  runner.run_test("test-return-integer-6.gc", {"0\n"});
  runner.run_test("test-return-integer-7.gc", {"-123\n"});
  runner.run_test("test-conditional-compilation-1.gc", {"3\n"});
  // todo, test-conditional-compilation-2.gc
  // these numbers match the game's memory layout for where the symbol table lives.
  // it's probably not 100% needed to get this exactly, but it's a good sign that the global
  // heap lives in the right spot because there should be no divergence in memory layout when its
  // built.  This also checks that #t, #f get "hashed" to the correct spot.
  runner.run_test("test-get-symbol-1.gc", {"1342756\n"});  // 0x147d24 in hex
  runner.run_test("test-get-symbol-2.gc", {"1342764\n"});  // 0x147d2c in hex
  runner.run_test("test-define-1.gc", {"17\n"});
  runner.run_test("test-nested-blocks-1.gc", {"7\n"});
  runner.run_test("test-nested-blocks-2.gc", {"8\n"});
  runner.run_test("test-nested-blocks-3.gc", {"7\n"});
  runner.run_test("test-goto-1.gc", {"3\n"});
  runner.run_test("test-defglobalconstant-1.gc", {"17\n"});
  runner.run_test("test-defglobalconstant-2.gc", {"18\n"});
  runner.run_test("test-simple-function-call.gc", {"30\n"});
  runner.run_test("test-application-lambda-1.gc", {"2\n"});
  runner.run_test("test-let-1.gc", {"30\n"});
  runner.run_test("test-let-star-1.gc", {"30\n"});
  runner.run_always_pass("test-string-constant-1.gc");

  std::string expected = "\"test string!\"";
  runner.run_test("test-string-constant-2.gc", {expected}, expected.size());
  runner.run_test("test-defun-return-constant.gc", {"12\n"});
  runner.run_test("test-defun-return-symbol.gc", {"42\n"});
  runner.run_test("test-function-return-arg.gc", {"23\n"});
  runner.run_test("test-nested-function-call.gc", {"2\n"});

  // math
  runner.run_test("test-add-int-constants.gc", {"13\n"});
  runner.run_test("test-add-int-vars.gc", {"7\n"});
  runner.run_test("test-add-int-multiple.gc", {"15\n"});
  runner.run_test("test-add-int-multiple-2.gc", {"15\n"});
  runner.run_test("test-add-function-returns.gc", {"21\n"});
  runner.run_test("test-sub-1.gc", {"4\n"});
  runner.run_test("test-sub-2.gc", {"4\n"});
  runner.run_test("test-mul-1.gc", {"-12\n"});

  compiler.shutdown_target();
  runtime_thread.join();
  runner.print_summary();
}
