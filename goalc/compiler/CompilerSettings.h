#ifndef JAK_COMPILERSETTINGS_H
#define JAK_COMPILERSETTINGS_H

#include <unordered_map>
#include <string>
#include "goalc/goos/Object.h"

class CompilerSettings {
 public:
  CompilerSettings();
  bool debug_print_ir = false;
  bool debug_print_regalloc = false;
  bool disable_math_const_prop = false;
  bool emit_move_after_return = true;

  void set(const std::string& name, const goos::Object& value);

 private:
  enum class SettingKind { BOOL, INVALID };

  struct SettingsEntry {
    SettingKind kind = SettingKind::INVALID;
    goos::Object value;
    bool* boolp = nullptr;
  };

  std::unordered_map<std::string, SettingsEntry> m_settings;
};

#endif  // JAK_COMPILERSETTINGS_H
