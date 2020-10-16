#include <cstdio>
#include <string>
#include <vector>
#include "ObjectFile/ObjectFileDB.h"
#include "config.h"
#include "util/FileIO.h"
#include "third-party/spdlog/include/spdlog/spdlog.h"
#include "third-party/spdlog/include/spdlog/sinks/basic_file_sink.h"
#include "common/util/FileUtil.h"

int main(int argc, char** argv) {
  spdlog::info("Beginning disassembly. This may take a few minutes...");

  spdlog::set_level(spdlog::level::debug);
  auto lu = spdlog::basic_logger_mt("GOAL Decompiler", "logs/decompiler.log");
  spdlog::set_default_logger(lu);
  spdlog::flush_on(spdlog::level::info);

  init_crc();
  init_opcode_info();

  if (argc != 4) {
    printf("Usage: jak_disassembler <config_file> <in_folder> <out_folder>\n");
    return 1;
  }

  set_config(argv[1]);
  std::string in_folder = argv[2];
  std::string out_folder = argv[3];

  std::vector<std::string> dgos;
  for (const auto& dgo_name : get_config().dgo_names) {
    dgos.push_back(combine_path(in_folder, dgo_name));
  }

  ObjectFileDB db(dgos);
  file_util::write_text_file(combine_path(out_folder, "dgo.txt"), db.generate_dgo_listing());
  file_util::write_text_file(combine_path(out_folder, "obj.txt"), db.generate_obj_listing());

  db.process_link_data();
  db.find_code();
  db.process_labels();

  if (get_config().write_scripts) {
    db.find_and_write_scripts(out_folder);
  }

  if (get_config().write_hexdump) {
    db.write_object_file_words(out_folder, get_config().write_hexdump_on_v3_only);
  }

  db.analyze_functions();

  if (get_config().write_disassembly) {
    db.write_disassembly(out_folder, get_config().disassemble_objects_without_functions);
  }

  // todo print type summary
  // printf("%s\n", get_type_info().get_summary().c_str());

  file_util::write_text_file(combine_path(out_folder, "all-syms.gc"), db.dts.dump_symbol_types());
  spdlog::info("Disassembly has completed successfully.");
  return 0;
}
