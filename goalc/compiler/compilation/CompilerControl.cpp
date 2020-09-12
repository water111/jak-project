#include "goalc/compiler/Compiler.h"
#include "goalc/compiler/IR.h"
#include "common/util/Timer.h"
#include "common/util/DgoWriter.h"
#include "common/util/FileUtil.h"

Val* Compiler::compile_exit(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {}, {});
  if (m_listener.is_connected()) {
    m_listener.send_reset(false);
  }
  m_want_exit = true;
  return get_none();
}

Val* Compiler::compile_seval(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  try {
    for_each_in_list(rest, [&](const goos::Object& o) {
      m_goos.eval_with_rewind(o, m_goos.global_environment.as_env());
    });
  } catch (std::runtime_error& e) {
    throw_compile_error(form, std::string("seval error: ") + e.what());
  }
  return get_none();
}

Val* Compiler::compile_asm_file(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  int i = 0;
  std::string filename;
  bool load = false;
  bool color = false;
  bool write = false;
  bool no_code = false;

  std::vector<std::pair<std::string, float>> timing;
  Timer total_timer;

  for_each_in_list(rest, [&](const goos::Object& o) {
    if (i == 0) {
      filename = as_string(o);
    } else {
      auto setting = symbol_string(o);
      if (setting == ":load") {
        load = true;
      } else if (setting == ":color") {
        color = true;
      } else if (setting == ":write") {
        write = true;
      } else if (setting == ":no-code") {
        no_code = true;
      } else {
        throw_compile_error(form, "invalid option " + setting + " in asm-file form");
      }
    }
    i++;
  });

  Timer reader_timer;
  auto code = m_goos.reader.read_from_file({filename});
  timing.emplace_back("read", reader_timer.getMs());

  Timer compile_timer;
  std::string obj_file_name = filename;
  for (int idx = int(filename.size()) - 1; idx-- > 0;) {
    if (filename.at(idx) == '\\' || filename.at(idx) == '/') {
      obj_file_name = filename.substr(idx + 1);
      break;
    }
  }

  obj_file_name = obj_file_name.substr(0, obj_file_name.find_last_of('.'));
  auto obj_file = compile_object_file(obj_file_name, code, !no_code);
  timing.emplace_back("compile", compile_timer.getMs());

  if (color) {
    Timer color_timer;
    color_object_file(obj_file);
    timing.emplace_back("color", color_timer.getMs());

    Timer codegen_timer;
    auto data = codegen_object_file(obj_file);
    timing.emplace_back("codegen", codegen_timer.getMs());

    if (load) {
      if (m_listener.is_connected()) {
        m_listener.send_code(data);
      } else {
        printf("WARNING - couldn't load because listener isn't connected\n");
      }
    }

    if (write) {
      //      auto output_dir = as_string(get_constant_or_error(form, "*compiler-output-path*"));
      // todo, change extension based on v3/v4
      auto output_name = m_goos.reader.get_source_dir() + "/data/" + obj_file_name + ".o";
      file_util::write_binary_file(output_name, (void*)data.data(), data.size());
    }
  } else {
    if (load) {
      printf("WARNING - couldn't load because coloring is not enabled\n");
    }

    if (write) {
      printf("WARNING - couldn't write because coloring is not enabled\n");
    }
  }

  //  if(truthy(get_config("print-asm-file-time"))) {
  printf("F: %36s ", obj_file_name.c_str());
  for (auto& e : timing) {
    printf(" %12s %4.2f", e.first.c_str(), e.second);
  }
  printf("\n");
  //  }

  return get_none();
}

Val* Compiler::compile_listen_to_target(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  (void)env;
  std::string ip = "127.0.0.1";
  int port = 8112;  // todo, get from some constant somewhere
  bool got_port = false, got_ip = false;

  for_each_in_list(rest, [&](const goos::Object& o) {
    if (o.is_string()) {
      if (got_ip) {
        throw_compile_error(form, "got multiple strings!");
      }
      got_ip = true;
      ip = o.as_string()->data;
    } else if (o.is_int()) {
      if (got_port) {
        throw_compile_error(form, "got multiple ports!");
      }
      got_port = true;
      port = o.integer_obj.value;
    } else {
      throw_compile_error(form, "invalid argument to listen-to-target");
    }
  });

  m_listener.connect_to_target(30, ip, port);
  return get_none();
}

Val* Compiler::compile_reset_target(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  bool shutdown = false;
  for_each_in_list(rest, [&](const goos::Object& o) {
    if (o.is_symbol() && symbol_string(o) == ":shutdown") {
      shutdown = true;
    } else {
      throw_compile_error(form, "invalid argument to reset-target");
    }
  });
  m_listener.send_reset(shutdown);
  return get_none();
}

Val* Compiler::compile_poke(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {}, {});
  m_listener.send_poke();
  return get_none();
}

Val* Compiler::compile_gs(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {}, {});
  m_goos.execute_repl();
  return get_none();
}

Val* Compiler::compile_set_config(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {goos::ObjectType::SYMBOL, {}}, {});
  m_settings.set(symbol_string(args.unnamed.at(0)), args.unnamed.at(1));
  return get_none();
}

Val* Compiler::compile_in_package(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)form;
  (void)rest;
  (void)env;
  return get_none();
}

Val* Compiler::compile_build_dgo(const goos::Object& form, const goos::Object& rest, Env* env) {
  (void)env;
  auto args = get_va(form, rest);
  va_check(form, args, {goos::ObjectType::STRING}, {});
  auto dgo_desc = pair_cdr(m_goos.reader.read_from_file({args.unnamed.at(0).as_string()->data}));

  for_each_in_list(dgo_desc, [&](const goos::Object& dgo) {
    DgoDescription desc;
    auto first = pair_car(dgo);
    desc.dgo_name = as_string(first);
    auto dgo_rest = pair_cdr(dgo);

    for_each_in_list(dgo_rest, [&](const goos::Object& entry) {
      auto e_arg = get_va(dgo, entry);
      va_check(dgo, e_arg, {goos::ObjectType::STRING, goos::ObjectType::STRING}, {});
      DgoDescription::DgoEntry o;
      o.file_name = as_string(e_arg.unnamed.at(0));
      o.name_in_dgo = as_string(e_arg.unnamed.at(1));
      if (o.file_name.substr(o.file_name.length() - 3) != ".go") {  // kill v2's for now.
        desc.entries.push_back(o);
      }
    });

    build_dgo(desc);
  });

  return get_none();
}