#include "Form.h"
#include "FormStack.h"
#include "GenericElementMatcher.h"
#include "common/goos/PrettyPrinter.h"

/*
 * TODO
 * - use var_to_form over expressions for vars
 * - check out if we can push/pop variables instead of registers?
 */

namespace decompiler {

namespace {

Form* var_to_form(const Variable& var, FormPool& pool) {
  return pool.alloc_single_element_form<SimpleAtomElement>(nullptr, SimpleAtom::make_var(var));
}

// void update_var_from_stack_helper(int my_idx,
//                                  const Env& env,
//                                  Variable input,
//                                  FormPool& pool,
//                                  FormStack& stack,
//                                  const RegSet& consumes,
//                                  std::vector<FormElement*>* result) {
//  if (consumes.find(input.reg()) != consumes.end()) {
//    // is consumed.
////    auto stack_val = stack.pop_reg(input);
//    auto stack_val = stack.pop({input.reg()}, env);
//    if (!stack_val.empty()) {
//      for (auto x : stack_val.front()->elts()) {
//        result->push_back(x);
//      }
//      return;
//    }
//  }
//  auto elt =
//      pool.alloc_element<SimpleExpressionElement>(SimpleAtom::make_var(input).as_expr(), my_idx);
//  result->push_back(elt);
//}
//
// void update_var_from_stack_helper(int my_idx,
//                                  Variable input,
//                                  const Env& env,
//                                  FormPool& pool,
//                                  FormStack& stack,
//                                  std::vector<FormElement*>* result) {
//  auto& ri = env.reg_use().op.at(my_idx);
//  if (ri.consumes.find(input.reg()) != ri.consumes.end()) {
//    // is consumed.
////    auto stack_val = stack.pop_reg(input);
//    auto stack_val = stack.pop({input.reg()}, env);
//    if (!stack_val.empty()) {
//      for (auto x : stack_val.front()->elts()) {
//        result->push_back(x);
//      }
//      return;
//    }
//  }
//  auto elt =
//      pool.alloc_element<SimpleExpressionElement>(SimpleAtom::make_var(input).as_expr(), my_idx);
//  result->push_back(elt);
//}
//
// Form* update_var_from_stack_to_form(int my_idx,
//                                    Variable input,
//                                    const Env& env,
//                                    FormPool& pool,
//                                    FormStack& stack) {
//  std::vector<FormElement*> elts;
//  update_var_from_stack_helper(my_idx, input, env, pool, stack, &elts);
//  return pool.alloc_sequence_form(nullptr, elts);
//}

// Form* update_var_from_stack_to_form(int my_idx,
//                                    const Env& env,
//                                    Variable input,
//                                    const RegSet& consumes,
//                                    FormPool& pool,
//                                    FormStack& stack) {
//  std::vector<FormElement*> elts;
//  update_var_from_stack_helper(my_idx, env, input, pool, stack, consumes, &elts);
//  return pool.alloc_sequence_form(nullptr, elts);
//}

void pop_helper(const std::vector<Variable>& vars,
                const Env& env,
                FormPool& pool,
                FormStack& stack,
                const std::vector<std::vector<FormElement*>*>& output,
                std::optional<RegSet> consumes = std::nullopt) {
  //  fmt::print("VARS: ");
  //  for (auto& x : vars)
  //    fmt::print("{} ", x.to_string(&env));
  //  fmt::print("\n");

  std::vector<Register> submit_regs;
  std::vector<size_t> submit_reg_to_var;

  // build submission for stack
  for (size_t var_idx = 0; var_idx < vars.size(); var_idx++) {
    const auto& var = vars.at(var_idx);
    auto& ri = env.reg_use().op.at(var.idx());
    RegSet consumes_to_use = consumes.value_or(ri.consumes);
    if (consumes_to_use.find(var.reg()) != consumes_to_use.end()) {
      // could pop
      submit_reg_to_var.push_back(var_idx);
      submit_regs.push_back(var.reg());
    } else {
      // no way to pop
    }
  }

  //  fmt::print("SUBMIT: ");
  //  for (auto& x : submit_regs)
  //    fmt::print("{} ", x.to_string());
  //  fmt::print("\n");

  // submit!
  auto result = stack.pop(submit_regs, env);
  std::vector<Form*> forms;
  forms.resize(vars.size(), nullptr);
  if (!result.empty()) {
    // success!
    for (size_t i = 0; i < submit_regs.size(); i++) {
      //      fmt::print(" got submission result idx {}/{} reg {} value {}\n", i,
      //      submit_reg_to_var.at(i),
      //                 vars.at(submit_reg_to_var.at(i)).to_string(&env),
      //                 result.at(i)->to_string(env));
      forms.at(submit_reg_to_var.at(i)) = result.at(i);
    }
  }

  // fill in the missing pieces:
  for (size_t i = 0; i < forms.size(); i++) {
    if (forms.at(i)) {
      for (auto x : forms.at(i)->elts()) {
        output.at(i)->push_back(x);
      }
    } else {
      output.at(i)->push_back(pool.alloc_element<SimpleExpressionElement>(
          SimpleAtom::make_var(vars.at(i)).as_expr(), vars.at(i).idx()));
    }
  }
}

std::vector<Form*> pop_to_forms(const std::vector<Variable>& vars,
                                const Env& env,
                                FormPool& pool,
                                FormStack& stack,
                                std::optional<RegSet> consumes = std::nullopt) {
  std::vector<Form*> forms;
  std::vector<std::vector<FormElement*>> forms_out;
  std::vector<std::vector<FormElement*>*> form_ptrs;
  forms_out.resize(vars.size());
  form_ptrs.reserve(vars.size());
  forms.reserve(vars.size());
  for (auto& x : forms_out) {
    form_ptrs.push_back(&x);
  }

  pop_helper(vars, env, pool, stack, form_ptrs, consumes);

  for (auto& x : forms_out) {
    forms.push_back(pool.alloc_sequence_form(nullptr, x));
  }
  return forms;
}

bool is_float_type(const Env& env, int my_idx, Variable var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("float");
}

bool is_int_type(const Env& env, int my_idx, Variable var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("int");
}

bool is_uint_type(const Env& env, int my_idx, Variable var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("uint");
}
}  // namespace

void Form::update_children_from_stack(const Env& env, FormPool& pool, FormStack& stack) {
  std::vector<FormElement*> new_elts;
  for (auto& elt : m_elements) {
    elt->update_from_stack(env, pool, stack, &new_elts);
  }
  m_elements = new_elts;
}

void FormElement::update_from_stack(const Env& env,
                                    FormPool&,
                                    FormStack&,
                                    std::vector<FormElement*>*) {
  throw std::runtime_error(fmt::format("update_from_stack NYI for {}", to_string(env)));
}

void LoadSourceElement::update_from_stack(const Env& env,
                                          FormPool& pool,
                                          FormStack& stack,
                                          std::vector<FormElement*>* result) {
  m_addr->update_children_from_stack(env, pool, stack);
  result->push_back(this);
}

void SimpleExpressionElement::update_from_stack_identity(const Env& env,
                                                         FormPool& pool,
                                                         FormStack& stack,
                                                         std::vector<FormElement*>* result) {
  auto& arg = m_expr.get_arg(0);
  if (arg.is_var()) {
    pop_helper({arg.var()}, env, pool, stack, {result});
    //    update_var_from_stack_helper(m_my_idx, arg.var(), env, pool, stack, result);
  } else if (arg.is_static_addr()) {
    // for now, do nothing.
    result->push_back(this);
  } else if (arg.is_sym_ptr() || arg.is_sym_val() || arg.is_int() || arg.is_empty_list()) {
    result->push_back(this);
  } else {
    throw std::runtime_error(fmt::format(
        "SimpleExpressionElement::update_from_stack_identity NYI for {}", to_string(env)));
  }
}

void SimpleExpressionElement::update_from_stack_gpr_to_fpr(const Env& env,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result) {
  auto src = m_expr.get_arg(0);
  auto src_type = env.get_types_before_op(m_my_idx).get(src.var().reg());
  if (src_type.typespec() == TypeSpec("float")) {
    // set ourself to identity.
    m_expr = src.as_expr();
    // then go again.
    update_from_stack(env, pool, stack, result);
  } else {
    throw std::runtime_error(fmt::format("GPR -> FPR applied to a {}", src_type.print()));
  }
}

void SimpleExpressionElement::update_from_stack_fpr_to_gpr(const Env& env,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result) {
  auto src = m_expr.get_arg(0);
  auto src_type = env.get_types_before_op(m_my_idx).get(src.var().reg());
  if (src_type.typespec() == TypeSpec("float")) {
    // set ourself to identity.
    m_expr = src.as_expr();
    // then go again.
    update_from_stack(env, pool, stack, result);
  } else {
    throw std::runtime_error(fmt::format("FPR -> GPR applied to a {}", src_type.print()));
  }
}

void SimpleExpressionElement::update_from_stack_div_s(const Env& env,
                                                      FormPool& pool,
                                                      FormStack& stack,
                                                      std::vector<FormElement*>* result) {
  if (is_float_type(env, m_my_idx, m_expr.get_arg(0).var()) &&
      is_float_type(env, m_my_idx, m_expr.get_arg(1).var())) {
    // todo - check the order here

    //    auto arg0 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(0).var(), env, pool,
    //    stack); auto arg1 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(1).var(), env,
    //    pool, stack);
    auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::DIVISION), args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    throw std::runtime_error(fmt::format("Floating point division attempted on invalid types."));
  }
}

void SimpleExpressionElement::update_from_stack_add_i(const Env& env,
                                                      FormPool& pool,
                                                      FormStack& stack,
                                                      std::vector<FormElement*>* result) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());

  bool arg1_reg = m_expr.get_arg(1).is_var();
  bool arg1_i = true;
  bool arg1_u = true;
  if (arg1_reg) {
    arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());
    arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());
  }

  std::vector<Form*> args;

  if (arg1_reg) {
    args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);
  } else {
    args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack);
    args.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
  }

  if ((arg0_i && arg1_i) || (arg0_u && arg1_u)) {
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::ADDITION), args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    auto cast = pool.alloc_single_element_form<CastElement>(
        nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(1));
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::ADDITION), args.at(0), cast);
    result->push_back(new_form);
  }
}

void SimpleExpressionElement::update_from_stack_mult_si(const Env& env,
                                                        FormPool& pool,
                                                        FormStack& stack,
                                                        std::vector<FormElement*>* result) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());

  //  auto arg1 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(1).var(), env, pool,
  //  stack); auto arg0 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(0).var(), env,
  //  pool, stack);
  auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);

  if (!arg0_i) {
    args.at(0) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(0));
  }

  if (!arg1_i) {
    args.at(1) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(1));
  }

  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::MULTIPLICATION), args.at(0), args.at(1));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_force_si_2(const Env& env,
                                                           FixedOperatorKind kind,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());

  auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);

  if (!arg0_i) {
    args.at(0) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(0));
  }

  if (!arg1_i) {
    args.at(1) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(1));
  }

  auto new_form =
      pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0), args.at(1));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_force_ui_2(const Env& env,
                                                           FixedOperatorKind kind,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result) {
  auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
  bool arg1_u = true;
  bool arg1_reg = m_expr.get_arg(1).is_var();
  if (arg1_reg) {
    arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());
  } else {
    assert(m_expr.get_arg(1).is_int());
  }

  //  Form* arg1;
  //  if (arg1_reg) {
  //    arg1 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(1).var(), env, pool, stack);
  //  } else {
  //    arg1 = pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1));
  //  }
  //
  //  Form* arg0 = update_var_from_stack_to_form(m_my_idx, m_expr.get_arg(0).var(), env, pool,
  //  stack);

  std::vector<Form*> args;
  if (arg1_reg) {
    args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);
  } else {
    args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack);
    args.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
  }

  if (!arg0_u) {
    args.at(0) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("uint"), args.at(0));
  }

  if (!arg1_u) {
    args.at(1) = pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("uint"), args.at(1));
  }

  auto new_form =
      pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0), args.at(1));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_copy_first_int_2(
    const Env& env,
    FixedOperatorKind kind,
    FormPool& pool,
    FormStack& stack,
    std::vector<FormElement*>* result) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());
  auto arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());

  auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack);

  if ((arg0_i && arg1_i) || (arg0_u && arg1_u)) {
    auto new_form = pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind),
                                                       args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    auto cast = pool.alloc_single_element_form<CastElement>(
        nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(1));
    auto new_form =
        pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0), cast);
    result->push_back(new_form);
  }
}

void SimpleExpressionElement::update_from_stack_lognot(const Env& env,
                                                       FormPool& pool,
                                                       FormStack& stack,
                                                       std::vector<FormElement*>* result) {
  auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack);
  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::LOGNOT), args.at(0));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack(const Env& env,
                                                FormPool& pool,
                                                FormStack& stack,
                                                std::vector<FormElement*>* result) {
  switch (m_expr.kind()) {
    case SimpleExpression::Kind::IDENTITY:
      update_from_stack_identity(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::GPR_TO_FPR:
      update_from_stack_gpr_to_fpr(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::FPR_TO_GPR:
      update_from_stack_fpr_to_gpr(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::DIV_S:
      update_from_stack_div_s(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::ADD:
      update_from_stack_add_i(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::SUB:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::SUBTRACTION, pool, stack, result);
      break;
    case SimpleExpression::Kind::MUL_SIGNED:
      update_from_stack_mult_si(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::DIV_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::DIVISION, pool, stack, result);
      break;
    case SimpleExpression::Kind::MOD_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MOD, pool, stack, result);
      break;
    case SimpleExpression::Kind::MIN_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MIN, pool, stack, result);
      break;
    case SimpleExpression::Kind::MAX_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MAX, pool, stack, result);
      break;
    case SimpleExpression::Kind::AND:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGAND, pool, stack, result);
      break;
    case SimpleExpression::Kind::OR:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGIOR, pool, stack, result);
      break;
    case SimpleExpression::Kind::NOR:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGNOR, pool, stack, result);
      break;
    case SimpleExpression::Kind::XOR:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGXOR, pool, stack, result);
      break;
    case SimpleExpression::Kind::LOGNOT:
      update_from_stack_lognot(env, pool, stack, result);
      break;
    case SimpleExpression::Kind::LEFT_SHIFT:
      update_from_stack_force_ui_2(env, FixedOperatorKind::SLL, pool, stack, result);
      break;
    case SimpleExpression::Kind::MUL_UNSIGNED:
      update_from_stack_force_ui_2(env, FixedOperatorKind::MULTIPLICATION, pool, stack, result);
      break;
    default:
      throw std::runtime_error(
          fmt::format("SimpleExpressionElement::update_from_stack NYI for {}", to_string(env)));
  }
}

///////////////////
// SetVarElement
///////////////////

void SetVarElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  m_src->update_children_from_stack(env, pool, stack);
  if (m_src->is_single_element()) {
    auto src_as_se = dynamic_cast<SimpleExpressionElement*>(m_src->back());
    if (src_as_se) {
      if (src_as_se->expr().kind() == SimpleExpression::Kind::IDENTITY &&
          src_as_se->expr().get_arg(0).is_var()) {
        stack.push_non_seq_reg_to_reg(m_dst, src_as_se->expr().get_arg(0).var(), m_src);
        return;
      }
    }
  }

  stack.push_value_to_reg(m_dst, m_src, true);
}

void SetVarElement::update_from_stack(const Env& env,
                                      FormPool& pool,
                                      FormStack& stack,
                                      std::vector<FormElement*>* result) {
  m_src->update_children_from_stack(env, pool, stack);
  result->push_back(this);
}

void SetFormFormElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  // todo - is the order here right?
  m_src->update_children_from_stack(env, pool, stack);
  m_dst->update_children_from_stack(env, pool, stack);
  stack.push_form_element(this, true);
}

///////////////////
// AshElement
///////////////////

void AshElement::update_from_stack(const Env& env,
                                   FormPool& pool,
                                   FormStack& stack,
                                   std::vector<FormElement*>* result) {
  auto forms = pop_to_forms({value, shift_amount}, env, pool, stack, consumed);
  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::ARITH_SHIFT), forms.at(0), forms.at(1));
  result->push_back(new_form);
}

///////////////////
// AbsElement
///////////////////

void AbsElement::update_from_stack(const Env& env,
                                   FormPool& pool,
                                   FormStack& stack,
                                   std::vector<FormElement*>* result) {
  auto forms = pop_to_forms({source}, env, pool, stack, consumed);
  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::ABS), forms.at(0));
  result->push_back(new_form);
}

///////////////////
// FunctionCallElement
///////////////////

void FunctionCallElement::update_from_stack(const Env& env,
                                            FormPool& pool,
                                            FormStack& stack,
                                            std::vector<FormElement*>* result) {
  std::vector<Form*> args;
  auto nargs = m_op->arg_vars().size();
  args.resize(nargs, nullptr);

  //  for (size_t i = nargs; i-- > 0;) {
  //    auto var = m_op->arg_vars().at(i);
  //    args.at(i) = update_var_from_stack_to_form(m_op->op_id(), var, env, pool, stack);
  //  }
  //  Form* func = update_var_from_stack_to_form(m_op->op_id(), m_op->function_var(), env, pool,
  //  stack); auto new_form =
  //  pool.alloc_element<GenericElement>(GenericOperator::make_function(func), args);

  std::vector<Variable> all_pop_vars = {m_op->function_var()};
  for (size_t i = 0; i < nargs; i++) {
    all_pop_vars.push_back(m_op->arg_vars().at(i));
  }
  auto unstacked = pop_to_forms(all_pop_vars, env, pool, stack);
  std::vector<Form*> arg_forms;
  arg_forms.insert(arg_forms.begin(), unstacked.begin() + 1, unstacked.end());
  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_function(unstacked.at(0)), arg_forms);

  // detect method calls:
  // ex: ((-> pair methods-by-name new) (quote global) pair gp-0 a3-0)
  constexpr int type_for_method = 0;
  constexpr int method_name = 1;

  auto deref_matcher = Matcher::deref(
      Matcher::any_symbol(type_for_method), false,
      {DerefTokenMatcher::string("methods-by-name"), DerefTokenMatcher::any_string(method_name)});

  auto matcher = Matcher::op_with_rest(GenericOpMatcher::func(deref_matcher), {});
  auto temp_form = pool.alloc_single_form(nullptr, new_form);
  auto match_result = match(matcher, temp_form);
  if (match_result.matched) {
    auto type_1 = match_result.maps.strings.at(type_for_method);
    auto name = match_result.maps.strings.at(method_name);

    if (name == "new" && type_1 == "object") {
      std::vector<Form*> new_args = dynamic_cast<GenericElement*>(new_form)->elts();
      auto new_op = pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::OBJECT_NEW), new_args);
      result->push_back(new_op);
      return;
    } else if (name == "new") {
      constexpr int allocation = 2;
      constexpr int type_for_arg = 3;
      auto alloc_matcher = Matcher::any_quoted_symbol(allocation);
      auto type_arg_matcher = Matcher::any_symbol(type_for_arg);
      matcher = Matcher::op_with_rest(GenericOpMatcher::func(deref_matcher),
                                      {alloc_matcher, type_arg_matcher});
      match_result = match(matcher, temp_form);
      if (match_result.matched) {
        auto alloc = match_result.maps.strings.at(allocation);
        if (alloc != "global") {
          throw std::runtime_error("Unrecognized heap symbol for new: " + alloc);
        }
        auto type_2 = match_result.maps.strings.at(type_for_arg);
        if (type_1 != type_2) {
          throw std::runtime_error(
              fmt::format("Inconsistent types in method call: {} and {}", type_1, type_2));
        }

        std::vector<Form*> new_args = dynamic_cast<GenericElement*>(new_form)->elts();

        auto new_op = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::NEW), new_args);
        result->push_back(new_op);
        return;
      } else {
        throw std::runtime_error("Failed to match new method");
      }
    } else {
      throw std::runtime_error("Method call detected, not yet implemented");
    }
  }

  result->push_back(new_form);
}

void FunctionCallElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  std::vector<FormElement*> rewritten;
  update_from_stack(env, pool, stack, &rewritten);
  for (auto x : rewritten) {
    stack.push_form_element(x, true);
  }
}

///////////////////
// DerefElement
///////////////////
void DerefElement::update_from_stack(const Env& env,
                                     FormPool& pool,
                                     FormStack& stack,
                                     std::vector<FormElement*>* result) {
  // todo - update var tokens from stack?
  m_base->update_children_from_stack(env, pool, stack);
  auto as_deref = dynamic_cast<DerefElement*>(m_base->try_as_single_element());
  if (as_deref) {
    if (!m_is_addr_of && !as_deref->is_addr_of()) {
      m_tokens.insert(m_tokens.begin(), as_deref->tokens().begin(), as_deref->tokens().end());
      m_base = as_deref->m_base;
    }
  }
  result->push_back(this);
}

///////////////////
// UntilElement
///////////////////

void UntilElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  for (auto form : {condition, body}) {
    FormStack temp_stack;
    for (auto& entry : form->elts()) {
      entry->push_to_stack(env, pool, temp_stack);
    }
    auto new_entries = temp_stack.rewrite(pool);
    form->clear();
    for (auto e : new_entries) {
      form->push_back(e);
    }
  }

  stack.push_form_element(this, true);
}

void WhileElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  for (auto form : {condition, body}) {
    FormStack temp_stack;
    for (auto& entry : form->elts()) {
      entry->push_to_stack(env, pool, temp_stack);
    }
    auto new_entries = temp_stack.rewrite(pool);
    form->clear();
    for (auto e : new_entries) {
      form->push_back(e);
    }
  }
  stack.push_form_element(this, true);
}

///////////////////
// CondNoElseElement
///////////////////
void CondNoElseElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  for (auto& entry : entries) {
    for (auto form : {entry.condition, entry.body}) {
      FormStack temp_stack;
      for (auto& elt : form->elts()) {
        elt->push_to_stack(env, pool, temp_stack);
      }

      std::vector<FormElement*> new_entries;
      if (form == entry.body && used_as_value) {
        new_entries = temp_stack.rewrite_to_get_var(pool, final_destination, env);
      } else {
        new_entries = temp_stack.rewrite(pool);
      }

      form->clear();
      for (auto e : new_entries) {
        form->push_back(e);
      }
    }
  }

  if (used_as_value) {
    stack.push_value_to_reg(final_destination, pool.alloc_single_form(nullptr, this), true);
  } else {
    stack.push_form_element(this, true);
  }
}

void CondWithElseElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  // first, let's try to detect if all bodies write the same value
  std::optional<Variable> last_var;
  bool rewrite_as_set = true;

  // collect all forms which should write the output.
  std::vector<Form*> write_output_forms;
  for (const auto& entry : entries) {
    write_output_forms.push_back(entry.body);
  }
  write_output_forms.push_back(else_ir);

  // check all to see if they write the value.
  for (auto form : write_output_forms) {
    auto last_in_body = dynamic_cast<SetVarElement*>(form->elts().back());
    if (last_in_body) {
      if (last_var.has_value()) {
        if (last_var->reg() != last_in_body->dst().reg()) {
          rewrite_as_set = false;
          break;
        }
      }
      last_var = last_in_body->dst();
    }
  }

  if (rewrite_as_set) {
    assert(last_var.has_value());
  }

  for (auto& entry : entries) {
    for (auto form : {entry.condition, entry.body}) {
      FormStack temp_stack;
      for (auto& elt : form->elts()) {
        elt->push_to_stack(env, pool, temp_stack);
      }

      std::vector<FormElement*> new_entries;
      if (form == entry.body && rewrite_as_set) {
        new_entries = temp_stack.rewrite_to_get_var(pool, *last_var, env);
      } else {
        new_entries = temp_stack.rewrite(pool);
      }

      form->clear();
      for (auto e : new_entries) {
        form->push_back(e);
      }
    }
  }

  FormStack temp_stack;
  for (auto& elt : else_ir->elts()) {
    elt->push_to_stack(env, pool, temp_stack);
  }

  std::vector<FormElement*> new_entries;
  if (rewrite_as_set) {
    new_entries = temp_stack.rewrite_to_get_var(pool, *last_var, env);
  } else {
    new_entries = temp_stack.rewrite(pool);
  }

  else_ir->clear();
  for (auto e : new_entries) {
    else_ir->push_back(e);
  }

  //  auto top_condition = entries.front().condition;
  //  if (!top_condition->is_single_element()) {
  //    auto real_condition = top_condition->back();
  //    top_condition->pop_back();
  //    for (auto x : top_condition->elts()) {
  //      x->push_to_stack(env, pool, stack);
  //    }
  //    top_condition->elts() = {real_condition};
  //  }

  if (rewrite_as_set) {
    stack.push_value_to_reg(*last_var, pool.alloc_single_form(nullptr, this), true);
  } else {
    stack.push_form_element(this, true);
  }
}

///////////////////
// ShortCircuitElement
///////////////////

void ShortCircuitElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  if (!used_as_value.value_or(false)) {
    throw std::runtime_error(
        "ShortCircuitElement::push_to_stack not implemented for result not used case.");

    stack.push_form_element(this, true);
  } else {
    for (int i = 0; i < int(entries.size()); i++) {
      auto& entry = entries.at(i);
      FormStack temp_stack;
      for (auto& elt : entry.condition->elts()) {
        elt->push_to_stack(env, pool, temp_stack);
      }

      std::vector<FormElement*> new_entries;
      if (i == int(entries.size()) - 1) {
        new_entries = temp_stack.rewrite_to_get_var(pool, final_result, env);
      } else {
        new_entries = temp_stack.rewrite(pool);
      }

      entry.condition->clear();
      for (auto e : new_entries) {
        entry.condition->push_back(e);
      }
    }
    assert(used_as_value.has_value());
    stack.push_value_to_reg(final_result, pool.alloc_single_form(nullptr, this), true);
  }
}

///////////////////
// ConditionElement
///////////////////

void ConditionElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  std::vector<Form*> source_forms;
  std::vector<Variable> vars;
  fmt::print("A\n");

  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    //      source_forms.push_back(update_var_from_stack_to_form(m_src[i]->var().idx(), env,
    //                                                           m_src[i]->var(), m_consumed, pool,
    //                                                           stack));
    vars.push_back(m_src[i]->var());
  }
  std::reverse(vars.begin(), vars.end());
  source_forms = pop_to_forms(vars, env, pool, stack, m_consumed);
  std::reverse(source_forms.begin(), source_forms.end());

  stack.push_form_element(
      pool.alloc_element<GenericElement>(GenericOperator::make_compare(m_kind), source_forms),
      true);
}

void ConditionElement::update_from_stack(const Env& env,
                                         FormPool& pool,
                                         FormStack& stack,
                                         std::vector<FormElement*>* result) {
  fmt::print("B\n");
  //  throw std::runtime_error("ce2 NYI");
  //  std::vector<Form*> source_forms;
  //
  //  //  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
  //  for (int i = get_condition_num_args(m_kind); i-- > 0;) {
  //    source_forms.push_back(update_var_from_stack_to_form(m_src[i]->var().idx(), env,
  //                                                         m_src[i]->var(), m_consumed, pool,
  //                                                         stack));
  //  }
  //
  std::vector<Form*> source_forms;
  std::vector<Variable> vars;

  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    //      source_forms.push_back(update_var_from_stack_to_form(m_src[i]->var().idx(), env,
    //                                                           m_src[i]->var(), m_consumed, pool,
    //                                                           stack));
    vars.push_back(m_src[i]->var());
  }
  source_forms = pop_to_forms(vars, env, pool, stack, m_consumed);

  result->push_back(
      pool.alloc_element<GenericElement>(GenericOperator::make_compare(m_kind), source_forms));
}

void ReturnElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  FormStack temp_stack;
  for (auto& elt : return_code->elts()) {
    elt->push_to_stack(env, pool, temp_stack);
  }

  std::vector<FormElement*> new_entries;
  new_entries = temp_stack.rewrite_to_get_var(pool, env.end_var(), env);

  return_code->clear();
  for (auto e : new_entries) {
    return_code->push_back(e);
  }
  stack.push_form_element(this, true);
}

void AtomicOpElement::push_to_stack(const Env& env, FormPool&, FormStack& stack) {
  auto as_end = dynamic_cast<const FunctionEndOp*>(m_op);
  if (as_end) {
    // we don't want to push this to the stack (for now at least)
    return;
  }

  auto as_special = dynamic_cast<const SpecialOp*>(m_op);
  if (as_special) {
    if (as_special->kind() == SpecialOp::Kind::NOP) {
      stack.push_form_element(this, true);
      return;
    }
  }
  throw std::runtime_error("Can't push atomic op to stack: " + m_op->to_string(env));
}

void GenericElement::update_from_stack(const Env& env,
                                       FormPool& pool,
                                       FormStack& stack,
                                       std::vector<FormElement*>* result) {
  if (m_head.m_kind == GenericOperator::Kind::FUNCTION_EXPR) {
    m_head.m_function->update_children_from_stack(env, pool, stack);
  }

  for (auto& x : m_elts) {
    x->update_children_from_stack(env, pool, stack);
  }
  result->push_back(this);
}

////////////////////////
// DynamicMethodAccess
////////////////////////

void DynamicMethodAccess::update_from_stack(const Env& env,
                                            FormPool& pool,
                                            FormStack& stack,
                                            std::vector<FormElement*>* result) {
  //  auto new_val = stack.pop_reg(m_source);
  throw std::runtime_error("dma NYI");
  //  auto new_val = update_var_from_stack_to_form(m_source.idx(), m_source, env, pool, stack);
  //  auto reg0_matcher =
  //      Matcher::match_or({Matcher::any_reg(0), Matcher::cast("uint", Matcher::any_reg(0))});
  //  auto reg1_matcher =
  //      Matcher::match_or({Matcher::any_reg(1), Matcher::cast("int", Matcher::any_reg(1))});
  //
  //  // (+ (sll (the-as uint a1-0) 2) (the-as int a0-0))
  //  auto sll_matcher = Matcher::fixed_op(FixedOperatorKind::SLL, {reg0_matcher,
  //  Matcher::integer(2)}); auto matcher = Matcher::fixed_op(FixedOperatorKind::ADDITION,
  //  {sll_matcher, reg1_matcher}); auto match_result = match(matcher, new_val); if
  //  (!match_result.matched) {
  //    throw std::runtime_error("Couldn't match DynamicMethodAccess values: " +
  //                             new_val->to_string(env));
  //  }
  //
  //  auto idx = match_result.maps.regs.at(0);
  //  auto base = match_result.maps.regs.at(1);
  //  assert(idx.has_value() && base.has_value());
  //
  //  auto deref = pool.alloc_element<DerefElement>(
  //      var_to_form(base.value(), pool), false,
  //      std::vector<DerefToken>{DerefToken::make_field_name("methods"),
  //                              DerefToken::make_int_expr(var_to_form(idx.value(), pool))});
  //  result->push_back(deref);
}

}  // namespace decompiler