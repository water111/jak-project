/*!
 * @file Env.h
 * The Env tree. The stores all of the nested scopes/contexts during compilation and also
 * manages the memory for stuff generated during compiling.
 */

#ifndef JAK_ENV_H
#define JAK_ENV_H

#include <string>
#include <memory>
#include <vector>
#include "common/type_system/TypeSpec.h"
#include "goalc/regalloc/allocate.h"
#include "goalc/goos/Object.h"
#include "StaticObject.h"
#include "Label.h"
#include "Val.h"

class FileEnv;
class BlockEnv;
class IR;

/*!
 * Parent class for Env's
 */
class Env {
 public:
  explicit Env(Env* parent) : m_parent(parent) {}
  virtual std::string print() = 0;
  virtual void emit(std::unique_ptr<IR> ir);
  virtual RegVal* make_ireg(TypeSpec ts, emitter::RegKind kind);
  virtual void constrain_reg(IRegConstraint constraint);  // todo, remove!
  virtual Val* lexical_lookup(goos::Object sym);
  virtual BlockEnv* find_block(const std::string& name);
  virtual std::unordered_map<std::string, Label>& get_label_map();
  RegVal* make_gpr(TypeSpec ts);
  RegVal* make_xmm(TypeSpec ts);
  virtual ~Env() = default;

  Env* parent() { return m_parent; }

 protected:
  Env* m_parent = nullptr;
};

/*!
 * The top-level Env. Holds FileEnvs for all files.
 */
class GlobalEnv : public Env {
 public:
  GlobalEnv();
  std::string print() override;
  void emit(std::unique_ptr<IR> ir) override;
  RegVal* make_ireg(TypeSpec ts, emitter::RegKind kind) override;
  void constrain_reg(IRegConstraint constraint) override;
  Val* lexical_lookup(goos::Object sym) override;
  BlockEnv* find_block(const std::string& name) override;
  ~GlobalEnv() = default;

  FileEnv* add_file(std::string name);

 private:
  std::vector<std::unique_ptr<FileEnv>> m_files;
};

/*!
 * An Env that doesn't allow emitting to go past it. Used to make sure source code that shouldn't
 * generate machine code actually does this.
 */
class NoEmitEnv : public Env {
 public:
  explicit NoEmitEnv(Env* parent) : Env(parent) {}
  std::string print() override;
  void emit(std::unique_ptr<IR> ir) override;
  ~NoEmitEnv() = default;
};

/*!
 * An Env for an entire file (or input to the REPL)
 */
class FileEnv : public Env {
 public:
  FileEnv(Env* parent, std::string name);
  std::string print() override;
  void add_function(std::unique_ptr<FunctionEnv> fe);
  void add_top_level_function(std::unique_ptr<FunctionEnv> fe);
  void add_static(std::unique_ptr<StaticObject> s);
  NoEmitEnv* add_no_emit_env();
  void debug_print_tl();
  const std::vector<std::unique_ptr<FunctionEnv>>& functions() { return m_functions; }
  const std::vector<std::unique_ptr<StaticObject>>& statics() { return m_statics; }
  const FunctionEnv& top_level_function() {
    assert(m_top_level_func);
    return *m_top_level_func;
  }

  bool is_empty();
  ~FileEnv() = default;

 protected:
  std::string m_name;
  std::vector<std::unique_ptr<FunctionEnv>> m_functions;
  std::vector<std::unique_ptr<StaticObject>> m_statics;
  std::unique_ptr<NoEmitEnv> m_no_emit_env = nullptr;

  // statics
  FunctionEnv* m_top_level_func = nullptr;
};

/*!
 * An Env which manages the scope for (declare ...) statements.
 */
class DeclareEnv : public Env {
 public:
  explicit DeclareEnv(Env* parent) : Env(parent) {}
  virtual std::string print() = 0;
  ~DeclareEnv() = default;

  struct Settings {
    bool is_set = false;             // has the user set these with a (declare)?
    bool inline_by_default = false;  // if a function, inline when possible?
    bool save_code = true;           // if a function, should we save the code?
    bool allow_inline = false;       // should we allow the user to use this an inline function
  } settings;
};

class IR_GotoLabel;

struct UnresolvedGoto {
  IR_GotoLabel* ir;
  std::string label;
};

class FunctionEnv : public DeclareEnv {
 public:
  FunctionEnv(Env* parent, std::string name);
  std::string print() override;
  std::unordered_map<std::string, Label>& get_label_map() override;
  void set_segment(int seg) { segment = seg; }
  void emit(std::unique_ptr<IR> ir) override;
  void finish();
  RegVal* make_ireg(TypeSpec ts, emitter::RegKind kind) override;
  const std::vector<std::unique_ptr<IR>>& code() const { return m_code; }
  int max_vars() const { return m_iregs.size(); }
  const std::vector<IRegConstraint>& constraints() { return m_constraints; }
  void constrain(const IRegConstraint& c) { m_constraints.push_back(c); }
  void set_allocations(const AllocationResult& result) { m_regalloc_result = result; }
  Val* lexical_lookup(goos::Object sym) override;

  const AllocationResult& alloc_result() { return m_regalloc_result; }

  bool needs_aligned_stack() const { return m_aligned_stack_required; }
  void require_aligned_stack() { m_aligned_stack_required = true; }

  int idx_in_file = -1;

  template <typename T, class... Args>
  T* alloc_val(Args&&... args) {
    std::unique_ptr<T> new_obj = std::make_unique<T>(std::forward<Args>(args)...);
    m_vals.push_back(std::move(new_obj));
    return (T*)m_vals.back().get();
  }

  template <typename T, class... Args>
  T* alloc_env(Args&&... args) {
    std::unique_ptr<T> new_obj = std::make_unique<T>(std::forward<Args>(args)...);
    m_envs.push_back(std::move(new_obj));
    return (T*)m_envs.back().get();
  }

  int segment = -1;
  std::string method_of_type_name = "#f";
  bool is_asm_func = false;
  std::vector<UnresolvedGoto> unresolved_gotos;
  std::unordered_map<std::string, Val*> params;

 protected:
  void resolve_gotos();
  std::string m_name;
  std::vector<std::unique_ptr<IR>> m_code;
  std::vector<std::unique_ptr<RegVal>> m_iregs;
  std::vector<std::unique_ptr<Val>> m_vals;
  std::vector<std::unique_ptr<Env>> m_envs;
  std::vector<IRegConstraint> m_constraints;
  // todo, unresolved gotos
  AllocationResult m_regalloc_result;

  bool m_aligned_stack_required = false;

  std::unordered_map<std::string, Label> m_labels;
};

class BlockEnv : public Env {
 public:
  BlockEnv(Env* parent, std::string name);
  std::string print() override;
  BlockEnv* find_block(const std::string& name) override;

  std::string name;
  Label end_label = nullptr;
  RegVal* return_value = nullptr;
  std::vector<TypeSpec> return_types;
};

class LexicalEnv : public DeclareEnv {
 public:
  explicit LexicalEnv(Env* parent) : DeclareEnv(parent) {}
  Val* lexical_lookup(goos::Object sym) override;
  std::string print() override;
  std::unordered_map<std::string, Val*> vars;
};

class LabelEnv : public Env {
 public:
  explicit LabelEnv(Env* parent) : Env(parent) {}
  std::string print() override { return "labelenv"; }
  std::unordered_map<std::string, Label>& get_label_map() override;

 protected:
  std::unordered_map<std::string, Label> m_labels;
};

class WithInlineEnv : public Env {
 public:
  WithInlineEnv(Env* parent, bool _inline_preference)
      : Env(parent), inline_preference(_inline_preference) {}
  bool inline_preference = false;
};

class SymbolMacroEnv : public Env {
 public:
  explicit SymbolMacroEnv(Env* parent) : Env(parent) {}
  std::unordered_map<std::shared_ptr<goos::SymbolObject>, goos::Object> macros;
};

template <typename T>
T* get_parent_env_of_type(Env* in) {
  for (;;) {
    auto attempt = dynamic_cast<T*>(in);
    if (attempt)
      return attempt;
    if (dynamic_cast<GlobalEnv*>(in)) {
      return nullptr;
    }
    in = in->parent();
  }
}
// function
// block
// lexical
// label
// symbolmacro
// get parent env of type.

#endif  // JAK_ENV_H
