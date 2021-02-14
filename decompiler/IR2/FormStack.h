#pragma once

#include <optional>
#include "decompiler/Disasm/Register.h"
#include "decompiler/IR2/AtomicOp.h"

namespace decompiler {
class Form;
/*!
 * A FormStack is used to track partial expressions when rebuilding the tree structure of
 * GOAL code. Linear sequences of operations are added onto the expression stack.
 */
class FormStack {
 public:
  FormStack() = default;
  void push_value_to_reg(Variable var,
                         Form* value,
                         bool sequence_point,
                         const SetVarInfo& info = {});
  void push_non_seq_reg_to_reg(const Variable& dst,
                               const Variable& src,
                               Form* src_as_form,
                               const SetVarInfo& info = {});
  void push_form_element(FormElement* elt, bool sequence_point);
  Form* pop_reg(const Variable& var,
                const RegSet& barrier,
                const Env& env,
                bool allow_side_effects,
                int begin_idx = -1);
  Form* pop_reg(Register reg,
                const RegSet& barrier,
                const Env& env,
                bool allow_side_effects,
                int begin_idx = -1);
  Form* unsafe_peek(Register reg);
  bool is_single_expression();
  std::vector<FormElement*> rewrite(FormPool& pool);
  std::string print(const Env& env);

 private:
  struct StackEntry {
    bool active = true;                   // should this appear in the output?
    std::optional<Variable> destination;  // what register we are setting (or nullopt if no dest.)
    std::optional<Variable> non_seq_source;  // source variable, if we are setting var to var.
    Form* source = nullptr;                  // the value we are setting the register to.

    FormElement* elt = nullptr;
    bool sequence_point = false;
    TP_Type type;

    SetVarInfo set_info;

    std::string print(const Env& env) const;
  };
  std::vector<StackEntry> m_stack;
};

void rewrite_to_get_var(std::vector<FormElement*>& default_result,
                        FormPool& pool,
                        const Variable& var);
std::vector<FormElement*> rewrite_to_get_var(FormStack& stack, FormPool& pool, const Variable& var);
}  // namespace decompiler
