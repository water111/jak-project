#include "CompilerSettings.h"

CompilerSettings::CompilerSettings() {
  m_settings["print-ir"].kind = SettingKind::BOOL;
  m_settings["print-ir"].boolp = &debug_print_ir;

  m_settings["print-regalloc"].kind = SettingKind::BOOL;
  m_settings["print-regalloc"].boolp = &debug_print_regalloc;

  m_settings["disable-math-const-prop"].kind = SettingKind::BOOL;
  m_settings["disable-math-const-prop"].boolp = &disable_math_const_prop;
}

void CompilerSettings::set(const std::string& name, const goos::Object& value) {
  auto kv = m_settings.find(name);
  if (kv == m_settings.end()) {
    throw std::runtime_error("Compiler setting \"" + name + "\" was not recognized");
  }

  kv->second.value = value;
  if (kv->second.boolp) {
    *kv->second.boolp = !(value.is_symbol() && value.as_symbol()->name == "#f");
  }
}