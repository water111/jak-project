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
  void push_value_to_reg(Variable var, Form* value, bool sequence_point);
  void push_non_seq_reg_to_reg(const Variable& dst, const Variable& src, Form* src_as_form);
  void push_form_element(FormElement* elt, bool sequence_point);

//  Form* pop_reg(const Variable& var);
//  Form* pop_reg(Register reg);

  std::vector<Form*> pop(const std::vector<Register>& regs, const Env& env);
  bool is_single_expression();
  std::vector<FormElement*> rewrite(FormPool& pool);
  std::vector<FormElement*> rewrite_to_get_var(FormPool& pool, const Variable& var, const Env& env);
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
    std::string print(const Env& env) const;
  };
  std::vector<StackEntry> m_stack;
};
}  // namespace decompiler
