#include <common/link_types.h>
#include "common/util/FileUtil.h"
#include "gtest/gtest.h"
#include "common/log/log.h"
#include "decompiler/Disasm/OpcodeInfo.h"
#include "decompiler/config.h"
#include "decompiler/ObjectFile/ObjectFileDB.h"
#include "goalc/compiler/Compiler.h"
#include "common/util/Timer.h"
#include <common/util/json_util.h>

namespace fs = std::filesystem;

namespace {

// list of object files to ignore during reference checks
const std::unordered_set<std::string> g_files_to_skip_compiling = {
    "timer",    // accessing timer regs
    "display",  // interrupt handlers
};

// the functions we expect the decompiler to skip
const std::unordered_set<std::string> g_functions_expected_to_reject = {
    // gcommon
    "quad-copy!",  // asm mempcy
    // gkernel
    "set-to-run-bootstrap",     // kernel context switch
    "throw",                    // manually sets fp/t9.
    "throw-dispatch",           // restore context
    "(method 0 catch-frame)",   // save context
    "(method 11 cpu-thread)",   // kernel -> user context switch
    "(method 10 cpu-thread)",   // user -> kernel context switch
    "reset-and-call",           // kernel -> user
    "return-from-thread-dead",  // kernel -> user
    "return-from-thread",       // kernel -> user
    "return-from-exception",    // ps2 exception -> ps2 user
    "run-function-in-process",  // temp while stack vars aren't supported.
    // pskernel
    "kernel-check-hardwired-addresses",  // ps2 ee kernel debug hook
    "kernel-read-function",              // ps2 ee kernel debug hook
    "kernel-write-function",             // ps2 ee kernel debug hook
    "kernel-copy-function",              // ps2 ee kernel debug hook
    // math
    "rand-uint31-gen",  // weird and terrible random generator
    // bounding-box
    "(method 9 bounding-box)",   // handwritten asm loop
    "(method 14 bounding-box)",  // handwritten asm loop
    // trig
    "exp", "atan0", "sincos!", "sincos-rad!",
    // matrix
    "(method 9 matrix)",  // handwritten asm loop
    "matrix-axis-sin-cos!", "matrix-axis-sin-cos-vu!",
    // geometry
    "circle-circle-xz-intersect",  // unused not bothering
    // dma-h
    "dma-count-until-done",  // dma asm loop
    "dma-sync-with-count", "dma-send-no-scratch", "dma-sync-fast",
    // dma
    "symlink2", "symlink3",
    "dma-sync-hang",  // handwritten asm
    "vector=",        // asm branching
    // display
    "vblank-handler",  // asm
    "vif1-handler", "vif1-handler-debug",
    // stats-h
    "(method 11 perf-stat)", "(method 12 perf-stat)",
    // ripple - asm
    "ripple-execute-init", "ripple-create-wave-table", "ripple-apply-wave-table",
    "ripple-matrix-scale",
    // ripple - calls an asm function
    "ripple-execute",

    // sync-info
    "(method 15 sync-info)",         // needs *res-static-buf*
    "(method 15 sync-info-eased)",   // needs *res-static-buf*
    "(method 15 sync-info-paused)",  // needs *res-static-buf*

    // collide-mesh-h
    "(method 11 collide-mesh-cache)",  // asm

    // actor-link-h
    "(method 21 actor-link-info)",  // BUG: sc cfg / cfg-ir bug
    "(method 20 actor-link-info)",

    "debug-menu-item-var-render"  // asm
};

const std::unordered_set<std::string> g_functions_to_skip_compiling = {
    /// GCOMMON
    // these functions are not implemented by the compiler in OpenGOAL, but are in GOAL.
    "abs", "ash", "min", "max", "lognor",
    // weird PS2 specific debug registers:
    "breakpoint-range-set!",
    // inline assembly
    "valid?",

    /// GKERNEL
    // asm
    "(method 10 process)",

    /// GSTATE
    "enter-state",  // stack pointer asm

    /// MATH
    "rand-vu-init", "rand-vu",
    "rand-vu-nostep",  // random hardware

    // trig
    "sin-rad",                    // fpu acc
    "cos-rad",                    // fpu acc
    "atan-series-rad",            // fpu acc
    "vector-rad<-vector-deg!",    // bad decisions on float vs int128
    "vector-rad<-vector-deg/2!",  // bad decisions on float vs int128

    /// VECTOR-H
    "(method 3 vector)",  // this function appears twice, which confuses the compiler.
    "vector4-dot",        // fpu acc

    "(method 3 profile-frame)",  // double definition.

    // dma-disasm
    "disasm-dma-list",  // missing a single cast :(

    // math camera
    "transform-point-vector!", "transform-point-qword!", "transform-point-vector-scale!",

    // display-h
    "put-draw-env",

    // vector
    // bad decisions on float vs int128
    "vector-degf", "vector-degmod", "vector-deg-diff", "vector-degi",

    // geometry
    "calculate-basis-functions-vector!",  // asm requiring manual rewrite
    "curve-evaluate!",                    // asm requiring manual rewrite
    "point-in-triangle-cross",            // logior on floats manual fixup

    // asm
    "invalidate-cache-line",

    // sync-info
    "(method 15 sync-info)",         // needs display stuff first
    "(method 15 sync-info-eased)",   // needs display stuff first
    "(method 15 sync-info-paused)",  // needs display stuff first

    // ripple - calls an asm function
    "ripple-execute",

    "get-task-status",

    // issue - https://github.com/water111/jak-project/issues/396
    "ocean-trans-add-upload-strip", "ocean-trans-add-upload-table"};

// default location for the data. It can be changed with a command line argument.
std::string g_iso_data_path = "";

bool g_dump_mode = false;

std::vector<std::pair<std::string, fs::path>> g_object_files_to_decompile_or_ref_check;

}  // namespace

std::string replaceFirstOccurrence(std::string& s,
                                   const std::string& toReplace,
                                   const std::string& replaceWith) {
  std::size_t pos = s.find(toReplace);
  if (pos == std::string::npos)
    return s;
  return s.replace(pos, toReplace.length(), replaceWith);
}

int main(int argc, char** argv) {
  lg::initialize();

  // Determine the files to decompile and reference check by scanning the reference directory
  // All relevant files are assumed to end with `_REF.g[c|d]`
  // First rough order them
  std::vector<std::pair<std::string, fs::path>> reference_files_rough_order;
  for (auto& p : fs::recursive_directory_iterator(
           file_util::get_file_path({"test", "decompiler", "reference"}))) {
    if (p.is_regular_file()) {
      std::string file_name = fs::path(p.path()).replace_extension().filename().string();
      if (file_name.find("_REF") == std::string::npos) {
        continue;
      }
      std::string object_name = replaceFirstOccurrence(file_name, "_REF", "");
      reference_files_rough_order.push_back({object_name, p.path()});
    }
  }
  // use the all_objs.json file to place them in the correct build order
  auto j = parse_commented_json(
      file_util::read_text_file(file_util::get_file_path({"goal_src", "build", "all_objs.json"})),
      "all_objs.json");
  for (auto& x : j) {
    auto mapped_name = x[0].get<std::string>();
    for (auto& p : reference_files_rough_order) {
      if (p.first == mapped_name) {
        g_object_files_to_decompile_or_ref_check.push_back(p);
        break;
      }
    }
  }

  // look for an argument that's not a gtest option
  bool got_arg = false;
  for (int i = 1; i < argc; i++) {
    auto arg = std::string(argv[i]);
    if (arg == "--dump-mode") {
      g_dump_mode = true;
      continue;
    }
    if (arg.length() > 2 && arg[0] == '-' && arg[1] == '-') {
      continue;
    }
    if (got_arg) {
      printf("You can only specify a single path for ISO data\n");
      return 1;
    }
    g_iso_data_path = arg;
    lg::warn("Using path {} for iso_data", g_iso_data_path);
    got_arg = true;
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

class OfflineDecompilation : public ::testing::Test {
 protected:
  static std::unique_ptr<decompiler::ObjectFileDB> db;
  static std::unique_ptr<decompiler::Config> config;
  static void SetUpTestCase() {
    // global setup
    file_util::init_crc();
    decompiler::init_opcode_info();
    config = std::make_unique<decompiler::Config>(decompiler::read_config_file(
        file_util::get_file_path({"decompiler", "config", "jak1_ntsc_black_label.jsonc"})));

    std::unordered_set<std::string> object_files;
    for (auto& p : g_object_files_to_decompile_or_ref_check) {
      object_files.insert(p.first);
    }
    config->allowed_objects = object_files;
    // don't try to do this because we can't write the file
    config->generate_symbol_definition_map = false;

    std::vector<std::string> dgos = {"CGO/KERNEL.CGO", "CGO/ENGINE.CGO", "CGO/GAME.CGO",
                                     "DGO/BEA.DGO"};
    std::vector<std::string> dgo_paths;
    if (g_iso_data_path.empty()) {
      for (auto& x : dgos) {
        dgo_paths.push_back(file_util::get_file_path({"iso_data", x}));
      }
    } else {
      for (auto& x : dgos) {
        dgo_paths.push_back(file_util::combine_path(g_iso_data_path, x));
      }
    }

    db = std::make_unique<decompiler::ObjectFileDB>(dgo_paths, config->obj_file_name_map_file,
                                                    std::vector<std::string>{},
                                                    std::vector<std::string>{}, *config);

    // basic processing to find functions/data/disassembly
    db->process_link_data(*config);
    db->find_code(*config);
    db->process_labels();

    // fancy decompilation.
    db->analyze_functions_ir2({}, *config);
  }

  static void TearDownTestCase() {
    db.reset();
    config.reset();
  }
};

std::unique_ptr<decompiler::ObjectFileDB> OfflineDecompilation::db;
std::unique_ptr<decompiler::Config> OfflineDecompilation::config;

/*!
 * Check that the most basic disassembly into files/functions/instructions has succeeded.
 */
TEST_F(OfflineDecompilation, CheckBasicDecode) {
  int obj_count = 0;
  db->for_each_obj([&](decompiler::ObjectFileData& obj) {
    obj_count++;
    auto& stats = obj.linked_data.stats;
    // make sure we decoded all instructions
    EXPECT_EQ(stats.code_bytes / 4, stats.decoded_ops);
    // make sure all FP uses are properly recognized
    EXPECT_EQ(stats.n_fp_reg_use, stats.n_fp_reg_use_resolved);
  });

  EXPECT_EQ(obj_count, config->allowed_objects.size());
}

/*!
 * Not a super great test, but check that we find functions, methods, and logins.
 * This is a test of ir2_top_level_pass, which isn't tested as part of the normal decompiler
 tests.
 */
TEST_F(OfflineDecompilation, FunctionDetect) {
  int function_count = 0;  // global functions
  int method_count = 0;    // methods
  int login_count = 0;     // top-level logins
  int unknown_count = 0;   // unknown functions, like anonymous lambdas

  db->for_each_function(
      [&](decompiler::Function& func, int segment_id, decompiler::ObjectFileData&) {
        if (segment_id == TOP_LEVEL_SEGMENT) {
          EXPECT_EQ(func.guessed_name.kind, decompiler::FunctionName::FunctionKind::TOP_LEVEL_INIT);
        } else {
          EXPECT_NE(func.guessed_name.kind, decompiler::FunctionName::FunctionKind::TOP_LEVEL_INIT);
        }
        switch (func.guessed_name.kind) {
          case decompiler::FunctionName::FunctionKind::GLOBAL:
            function_count++;
            break;
          case decompiler::FunctionName::FunctionKind::METHOD:
            method_count++;
            break;
          case decompiler::FunctionName::FunctionKind::TOP_LEVEL_INIT:
            login_count++;
            break;
          case decompiler::FunctionName::FunctionKind::UNIDENTIFIED:
            unknown_count++;
            break;
          default:
            assert(false);
        }
      });

  // one login per object file
  EXPECT_EQ(config->allowed_objects.size(), login_count);
}

TEST_F(OfflineDecompilation, AsmFunction) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (func.suspected_asm) {
      if (g_functions_expected_to_reject.find(func.guessed_name.to_string()) ==
          g_functions_expected_to_reject.end()) {
        lg::error("Function {} was marked as asm, but wasn't expected.",
                  func.guessed_name.to_string());
        failed_count++;
      }
    }
  });
  EXPECT_EQ(failed_count, 0);
}

/*!
 * Test that all functions pass CFG build stage.
 */
TEST_F(OfflineDecompilation, CfgBuild) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.cfg || !func.cfg->is_fully_resolved()) {
        lg::error("Function {} failed cfg", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

/*!
 * Test that all functions pass the atomic op construction stage
 */
TEST_F(OfflineDecompilation, AtomicOp) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.atomic_ops || !func.ir2.atomic_ops_succeeded) {
        lg::error("Function {} failed atomic ops", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

/*!
 * Test that all functions pass the type analysis stage
 */
TEST_F(OfflineDecompilation, TypeAnalysis) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.env.has_type_analysis() || !func.ir2.env.types_succeeded) {
        lg::error("Function {} failed types", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

TEST_F(OfflineDecompilation, RegisterUse) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.env.has_reg_use()) {
        lg::error("Function {} failed reg use", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

TEST_F(OfflineDecompilation, VariableSSA) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.env.has_local_vars()) {
        lg::error("Function {} failed ssa", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

TEST_F(OfflineDecompilation, Structuring) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.top_form) {
        lg::error("Function {} failed structuring", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

TEST_F(OfflineDecompilation, Expressions) {
  int failed_count = 0;
  db->for_each_function([&](decompiler::Function& func, int, decompiler::ObjectFileData&) {
    if (!func.suspected_asm) {
      if (!func.ir2.expressions_succeeded) {
        lg::error("Function {} failed expressions", func.guessed_name.to_string());
        failed_count++;
      }
    }
  });

  EXPECT_EQ(failed_count, 0);
}

namespace {
void strip_trailing_newlines(std::string& in) {
  while (!in.empty() && in.back() == '\n') {
    in.pop_back();
  }
}
}  // namespace

TEST_F(OfflineDecompilation, Reference) {
  for (auto& file : g_object_files_to_decompile_or_ref_check) {
    auto& obj_l = db->obj_files_by_name.at(file.first);
    ASSERT_EQ(obj_l.size(), 1);

    std::string src = db->ir2_final_out(obj_l.at(0));

    /*     if (file == "gstring") {
           fmt::print("{}\n", src);
         }*/

    lg::info("Comparing {}...", file.first);

    // NOTE - currently only handles .gc files!
    auto reference = file_util::read_text_file(file.second.string());

    strip_trailing_newlines(reference);
    strip_trailing_newlines(src);

    if (g_dump_mode) {
      if (reference != src) {
        file_util::create_dir_if_needed("./failures");
        file_util::write_text_file("./failures/" + file.first + "_REF.gc", src);
        EXPECT_TRUE(false);
      }
    } else {
      EXPECT_EQ(reference, src);
    }
  }
}

namespace {
int line_count(const std::string& str) {
  int result = 0;
  for (auto& c : str) {
    if (c == '\n') {
      result++;
    }
  }
  return result;
}
}  // namespace

TEST_F(OfflineDecompilation, Compile) {
  Compiler compiler;

  compiler.run_front_end_on_file({"decompiler", "config", "all-types.gc"});
  compiler.run_front_end_on_file({"test", "decompiler", "reference", "decompiler-macros.gc"});

  Timer timer;
  int total_lines = 0;
  for (auto& file : g_object_files_to_decompile_or_ref_check) {
    if (g_files_to_skip_compiling.find(file.first) != g_files_to_skip_compiling.end()) {
      continue;
    }

    lg::info("Compiling {}...", file.first);

    auto& obj_l = db->obj_files_by_name.at(file.first);
    ASSERT_EQ(obj_l.size(), 1);

    std::string src = db->ir2_final_out(obj_l.at(0), g_functions_to_skip_compiling);
    total_lines += line_count(src);

    compiler.run_full_compiler_on_string_no_save(src);
  }
  auto time = timer.getSeconds();
  lg::info("Total Lines Compiled: {}. Lines/second: {:.1f}\n", total_lines,
           (float)total_lines / time);
}
