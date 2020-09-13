#include "Compiler.h"
#include "goalc/logger/Logger.h"
#include "common/link_types.h"
#include "IR.h"
#include "goalc/regalloc/allocate.h"
#include <chrono>
#include <thread>

using namespace goos;

Compiler::Compiler() {
  init_logger();
  init_settings();
  m_ts.add_builtin_types();
  m_global_env = std::make_unique<GlobalEnv>();
  m_none = std::make_unique<None>(m_ts.make_typespec("none"));

  // todo - compile library
  Object library_code = m_goos.reader.read_from_file({"goal_src", "goal-lib.gc"});
  compile_object_file("goal-lib", library_code, false);
}

void Compiler::execute_repl() {
  while (!m_want_exit) {
    try {
      // 1). get a line from the user (READ)
      std::string prompt;
      if (m_listener.is_connected()) {
        prompt = "gc";
      } else {
        prompt = "g";
      }
      Object code = m_goos.reader.read_from_stdin(prompt);

      // 2). compile
      auto obj_file = compile_object_file("repl", code, m_listener.is_connected());
      if (m_settings.debug_print_ir) {
        obj_file->debug_print_tl();
      }

      if (!obj_file->is_empty()) {
        // 3). color
        color_object_file(obj_file);

        // 4). codegen
        auto data = codegen_object_file(obj_file);

        // 4). send!
        if (m_listener.is_connected()) {
          m_listener.send_code(data);
          if (!m_listener.most_recent_send_was_acked()) {
            gLogger.log(MSG_ERR, "Runtime is not responding. Did it crash?\n");
          }
        }
      }

    } catch (std::exception& e) {
      gLogger.log(MSG_WARN, "REPL Error: %s\n", e.what());
    }
  }

  m_listener.disconnect();
}

Compiler::~Compiler() {
  gLogger.close();
}

void Compiler::init_logger() {
  gLogger.set_file("compiler.txt");  // todo, a better file than this...
  gLogger.config[MSG_COLOR].kind = LOG_FILE;
  gLogger.config[MSG_DEBUG].kind = LOG_IGNORE;
  gLogger.config[MSG_TGT].color = COLOR_GREEN;
  gLogger.config[MSG_TGT_INFO].color = COLOR_BLUE;
  gLogger.config[MSG_WARN].color = COLOR_RED;
  gLogger.config[MSG_ICE].color = COLOR_RED;
  gLogger.config[MSG_ERR].color = COLOR_RED;
}

void Compiler::init_settings() {}

FileEnv* Compiler::compile_object_file(const std::string& name,
                                       goos::Object code,
                                       bool allow_emit) {
  auto file_env = m_global_env->add_file(name);
  Env* compilation_env = file_env;
  if (!allow_emit) {
    compilation_env = file_env->add_no_emit_env();
  }

  file_env->add_top_level_function(
      compile_top_level_function("top-level", std::move(code), compilation_env));

  if (!allow_emit && !file_env->is_empty()) {
    throw std::runtime_error("Compilation generated code, but wasn't supposed to");
  }

  return file_env;
}

std::unique_ptr<FunctionEnv> Compiler::compile_top_level_function(const std::string& name,
                                                                  const goos::Object& code,
                                                                  Env* env) {
  auto fe = std::make_unique<FunctionEnv>(env, name);
  fe->set_segment(TOP_LEVEL_SEGMENT);

  auto result = compile_error_guard(code, fe.get());

  // only move to return register if we actually got a result
  if (!dynamic_cast<const None*>(result)) {
    fe->emit(std::make_unique<IR_Return>(fe->make_gpr(result->type()), result->to_gpr(fe.get())));
  }

  fe->finish();
  return fe;
}

Val* Compiler::compile_error_guard(const goos::Object& code, Env* env) {
  try {
    return compile(code, env);
  } catch (std::runtime_error& e) {
    printf(
        "------------------------------------------------------------------------------------------"
        "-\n");
    auto obj_print = code.print();
    if (obj_print.length() > 80) {
      obj_print = obj_print.substr(0, 80);
      obj_print += "...";
    }
    printf("object: %s\nfrom  : %s\n", obj_print.c_str(),
           m_goos.reader.db.get_info_for(code).c_str());
    throw e;
  }
}

void Compiler::throw_compile_error(const goos::Object& o, const std::string& err) {
  gLogger.log(MSG_ERR, "[Error] Could not compile %s!\nReason: %s\n", o.print().c_str(),
              err.c_str());
  throw std::runtime_error(err);
}

void Compiler::ice(const std::string& error) {
  gLogger.log(MSG_ICE, "[ICE] %s\n", error.c_str());
  throw std::runtime_error("ICE");
}

void Compiler::color_object_file(FileEnv* env) {
  for (auto& f : env->functions()) {
    AllocationInput input;
    for (auto& i : f->code()) {
      input.instructions.push_back(i->to_rai());
      input.debug_instruction_names.push_back(i->print());
    }
    input.max_vars = f->max_vars();
    input.constraints = f->constraints();

    if (m_settings.debug_print_regalloc) {
      input.debug_settings.print_input = true;
      input.debug_settings.print_result = true;
      input.debug_settings.print_analysis = true;
      input.debug_settings.allocate_log_level = 2;
    }

    f->set_allocations(allocate_registers(input));
  }
}

std::vector<u8> Compiler::codegen_object_file(FileEnv* env) {
  CodeGenerator gen(env);
  return gen.run();
}

std::vector<std::string> Compiler::run_test(const std::string& source_code) {
  try {
    if (!m_listener.is_connected()) {
      for (int i = 0; i < 1000; i++) {
        m_listener.connect_to_target();
        std::this_thread::sleep_for(std::chrono::microseconds(10000));
        if (m_listener.is_connected()) {
          break;
        }
      }
      if (!m_listener.is_connected()) {
        throw std::runtime_error("Compiler::run_test couldn't connect!");
      }
    }

    auto code = m_goos.reader.read_from_file({source_code});
    auto compiled = compile_object_file("test-code", code, true);
    if (compiled->is_empty()) {
      return {};
    }
    color_object_file(compiled);
    auto data = codegen_object_file(compiled);
    m_listener.record_messages(ListenerMessageKind::MSG_PRINT);
    m_listener.send_code(data);
    if (!m_listener.most_recent_send_was_acked()) {
      gLogger.log(MSG_ERR, "Runtime is not responding after sending test code. Did it crash?\n");
    }
    return m_listener.stop_recording_messages();
  } catch (std::exception& e) {
    fmt::print("[Compiler] Failed to compile test program {}: {}\n", source_code, e.what());
    return {};
  }
}

std::vector<std::string> Compiler::run_test_no_load(const std::string& source_code) {
  auto code = m_goos.reader.read_from_file({source_code});
  compile_object_file("test-code", code, true);
  return {};
}

void Compiler::shutdown_target() {
  if (m_listener.is_connected()) {
    m_listener.send_reset(true);
  }
}

void Compiler::typecheck(const goos::Object& form,
                         const TypeSpec& expected,
                         const TypeSpec& actual,
                         const std::string& error_message) {
  (void)form;
  m_ts.typecheck(expected, actual, error_message, true, true);
}