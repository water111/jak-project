#include "Form.h"
#include "FormStack.h"
#include "GenericElementMatcher.h"
#include "common/goos/PrettyPrinter.h"
#include "decompiler/util/DecompilerTypeSystem.h"
#include "decompiler/ObjectFile/LinkedObjectFile.h"
#include "decompiler/util/data_decompile.h"
#include "decompiler/IR2/bitfields.h"
#include "common/util/BitUtils.h"

/*
 * TODO
 * - use var_to_form over expressions for vars
 * - check out if we can push/pop variables instead of registers?
 */

/*!
 * Basic idea: push partial expressions to the stack and pop them off as they are used.
 * Leftovers are left on the stack and flushed out as set!
 *  But the challenge is knowing it's safe to pop something off of the stack.
 *  If the value is used after the read again, then it's not.
 *
 * The tricky situation is to accidentally generate this
 *  [simplified, we would never group like this, but similar things are possible]
 * (+ s5 (begin (set! s5 z) (+ x y)))
 * when the value of s5 used is after the (set! s5 z).
 *
 * To avoid that case, we make sure that anything after s5 in the expression cannot modify s5. If it
 * does, we just don't do the expression building and leave it as smaller expressions. To accomplish
 * this, we submit a batch of registers to pop, and the stack takes care of making sure this
 * property will hold.
 *
 * But what about
 * (+ (* s5 s4) (begin (set! s5 z) (+ x y)))
 *  Luckily this isn't a problem. The actual (* s5 s4) will only be inserted if the multiply
 *  instruction actually occurs before the second term in the outer addition.
 *  The issue only occurs when a pop fails and we just insert a variable name.
 *  In other words, this variable "insert" is the only that lets us bypass ordering.
 *    (which makes me wonder if I should have solved this by being more careful with that...
 *     but I have no immediate ideas unless we allow backtracking in popping which is bad)
 *
 * Now sometimes it's too hard to figure out exactly all of the variables we might pop and do
 * it all in one batch. We pop one thing, but we know that there are registers it shouldn't modify.
 * Instead of the barrier register approach, we do something easier: explicitly forbid
 * "outside of expression register variable side effects".
 * This means that you modify a register that's visible outside of the expression. Confusingly,
 * memory access doesn't count as a side effect and register read/writes that are part of a
 * chained together expression do not count.
 * This is kind of a hacky workaround that I'd really like to remove eventually.
 * I think it can basically be solved by being strategic in when we update from stack and we can
 * avoid the highly general "update some unknown vector of unknown things from the stack"
 */

namespace decompiler {

namespace {
Form* strip_pcypld_64(Form* in) {
  auto m = match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::PCPYLD),
                             {Matcher::integer(0), Matcher::any(0)}),
                 in);
  if (m.matched) {
    return m.maps.forms.at(0);
  } else {
    return in;
  }
}
}  // namespace

Form* try_cast_simplify(Form* in, const TypeSpec& new_type, FormPool& pool, const Env& env) {
  auto in_as_cast = dynamic_cast<CastElement*>(in->try_as_single_element());
  if (in_as_cast && in_as_cast->type() == new_type) {
    return in;  // no need to cast again, it already has it!
  }

  auto type_info = env.dts->ts.lookup_type(new_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info) {
    // todo remove this.
    if (bitfield_info->get_load_size() == 8) {
      in = strip_pcypld_64(in);
    }
    return cast_to_bitfield(bitfield_info, new_type, pool, env, in);
  }

  auto enum_info = dynamic_cast<EnumType*>(type_info);
  if (enum_info) {
    if (enum_info->is_bitfield()) {
      return cast_to_bitfield_enum(enum_info, new_type, pool, env, in);
    } else {
      return cast_to_int_enum(enum_info, new_type, pool, env, in);
    }
  }

  return nullptr;
}

bool Form::has_side_effects() {
  bool has_side_effect = false;
  apply([&](FormElement* elt) {
    if (dynamic_cast<SetVarElement*>(elt)) {
      has_side_effect = true;
    }
  });
  return has_side_effect;
}

bool FormElement::has_side_effects() {
  bool has_side_effect = false;
  apply([&](FormElement* elt) {
    if (dynamic_cast<SetVarElement*>(elt)) {
      has_side_effect = true;
    }
  });
  return has_side_effect;
}

namespace {

bool is_power_of_two(int in, int* out) {
  int x = 1;
  for (int i = 0; i < 32; i++) {
    if (x == in) {
      *out = i;
      return true;
    }
    x = x * 2;
  }
  return false;
}

/*!
 * Imagine:
 *   x = foo
 *   { // some macro/inlined thing
 *     read from x
 *     return x;
 *   }
 *
 * and you want to transform it to
 * x = some_macro(foo, blah, ...)
 *
 * this will get you foo (and pop it from the stack), assuming the stack is sitting right after the
 * point where the inline thing evaluated foo.
 *
 * For later book-keeping of reg use, if it gets you something new, it will set found_orig_out,
 * and also give you the regaccess for the x of the x = foo.
 *
 * If you use this, you are responsible for adding code that sets x again.
 */
Form* repop_passthrough_arg(Form* in,
                            FormStack& stack,
                            const Env& env,
                            RegisterAccess* orig_out,
                            bool* found_orig_out) {
  *found_orig_out = false;

  auto as_atom = form_as_atom(in);
  if (as_atom && as_atom->is_var()) {
    return stack.pop_reg(as_atom->var().reg(), {}, env, true, -1, orig_out, found_orig_out);
  }
  return in;
}

/*!
 * Create a form which represents a variable.
 */
Form* var_to_form(const RegisterAccess& var, FormPool& pool) {
  return pool.alloc_single_element_form<SimpleAtomElement>(nullptr, SimpleAtom::make_var(var));
}

/*!
 * Pop values of off the expression stack
 * @param vars     : list of variables to pop. In order of source code evaluation.
 * @param env      : the decompilation environment
 * @param pool     : form allocation pool
 * @param stack    : stack to pop from
 * @param output   : list of locations to push results.
 * @param consumes : if you have a different list of variables that are consumed by this operation.
 */
void pop_helper(const std::vector<RegisterAccess>& vars,
                const Env& env,
                FormPool& pool,
                FormStack& stack,
                const std::vector<std::vector<FormElement*>*>& output,
                bool allow_side_effects,
                const std::optional<RegSet>& consumes = std::nullopt,
                const std::vector<int>& times_used = {}) {
  // to submit to stack to attempt popping
  std::vector<Register> submit_regs;
  // submit_reg[i] is for var submit_reg_to_var[i]
  std::vector<size_t> submit_reg_to_var;

  // build submission for stack
  std::unordered_map<Register, int, Register::hash> reg_counts;
  for (auto& v : vars) {
    reg_counts[v.reg()]++;
  }

  for (size_t var_idx = 0; var_idx < vars.size(); var_idx++) {
    const auto& var = vars.at(var_idx);
    auto& ri = env.reg_use().op.at(var.idx());
    RegSet consumes_to_use = consumes.value_or(ri.consumes);
    if (consumes_to_use.find(var.reg()) != consumes_to_use.end()) {
      if (reg_counts.at(var.reg()) == 1) {
        // we consume the register, so it's safe to try popping.

        int times = 1;
        if (!times_used.empty()) {
          times = times_used.at(var_idx);
        }

        auto& use_def = env.get_use_def_info(var);
        if (use_def.use_count() == times && use_def.def_count() == 1) {
          submit_reg_to_var.push_back(var_idx);
          submit_regs.push_back(var.reg());
        } else {
          // auto var_id = env.get_program_var_id(var);
          //          fmt::print(
          //              "Unsafe to pop {}: used {} times, def {} times, expected use {} ({} {} rd:
          //              {}) ({} "
          //              "{})\n",
          //              var.to_string(env), use_def.use_count(), use_def.def_count(), times,
          //              var.reg().to_string(), var.idx(), var.mode() == AccessMode::READ,
          //              var_id.reg.to_string(), var_id.id);

          //          if (var.to_string(env) == "a3-0") {
          //            for (auto& use : use_def.uses) {
          //              if (!use.disabled) {
          //                fmt::print("  at instruction {}\n", use.op_id);
          //              }
          //            }
          //          }
        }
      }
    }
  }

  // submit and get a result! If the stack has nothing to pop, the result here may be nullptr.
  std::vector<Form*> pop_result;
  // loop in reverse (later vals first)
  for (size_t i = submit_regs.size(); i-- > 0;) {
    // figure out what var we are:
    auto var_idx = submit_reg_to_var.at(i);

    // anything _less_ than this should be unmodified by the pop
    // it's fine to modify yourself in your pop.
    RegSet pop_barrier_regs;
    for (size_t j = 0; j < var_idx; j++) {
      pop_barrier_regs.insert(vars.at(j).reg());
    }

    // do the pop, with the barrier to prevent out-of-sequence popping.
    pop_result.push_back(
        stack.pop_reg(submit_regs.at(i), pop_barrier_regs, env, allow_side_effects));
  }
  // now flip back to the source order for making the final result
  std::reverse(pop_result.begin(), pop_result.end());

  // final result forms. Will be nullptr if: we didn't try popping OR popping from stack failed.
  std::vector<Form*> forms;
  forms.resize(vars.size(), nullptr);
  if (!pop_result.empty()) {
    // success!
    for (size_t i = 0; i < submit_regs.size(); i++) {
      // fill out vars from our submission
      forms.at(submit_reg_to_var.at(i)) = pop_result.at(i);
    }
  }

  // write the output
  for (size_t i = 0; i < forms.size(); i++) {
    if (forms.at(i)) {
      // we got a form. inline these in the result
      for (auto x : forms.at(i)->elts()) {
        output.at(i)->push_back(x);
      }
    } else {
      // we got nothing, just insert the variable name.
      output.at(i)->push_back(pool.alloc_element<SimpleExpressionElement>(
          SimpleAtom::make_var(vars.at(i)).as_expr(), vars.at(i).idx()));
    }
  }
}

/*!
 * This should be used to generate all casts.
 */
Form* cast_form(Form* in, const TypeSpec& new_type, FormPool& pool, const Env& env) {
  auto result = try_cast_simplify(in, new_type, pool, env);
  if (result) {
    return result;
  }

  return pool.alloc_single_element_form<CastElement>(nullptr, new_type, in);
}

/*!
 * Pop each variable in the input list into a form. The variables should be given in the order
 * they are evaluated in the source. It is safe to put the result of these in the same expression.
 * This uses the barrier register approach, but it is only effective if you put all registers
 * appearing at the same level.
 */
std::vector<Form*> pop_to_forms(const std::vector<RegisterAccess>& vars,
                                const Env& env,
                                FormPool& pool,
                                FormStack& stack,
                                bool allow_side_effects,
                                const std::optional<RegSet>& consumes = std::nullopt,
                                const std::vector<int>& times_to_use = {}) {
  std::vector<Form*> forms;
  std::vector<std::vector<FormElement*>> forms_out;
  std::vector<std::vector<FormElement*>*> form_ptrs;
  forms_out.resize(vars.size());
  form_ptrs.reserve(vars.size());
  forms.reserve(vars.size());
  for (auto& x : forms_out) {
    form_ptrs.push_back(&x);
  }

  pop_helper(vars, env, pool, stack, form_ptrs, allow_side_effects, consumes, times_to_use);

  for (auto& x : forms_out) {
    forms.push_back(pool.alloc_sequence_form(nullptr, x));
  }

  // add casts, if needed.
  assert(vars.size() == forms.size());
  for (size_t i = 0; i < vars.size(); i++) {
    auto atom = form_as_atom(forms[i]);
    bool is_var = atom && atom->is_var();
    auto cast = env.get_user_cast_for_access(vars[i]);
    // only cast if we didn't get a var (compacting expressions).
    // there is a separate system for casting variables that will do a better job.
    if (cast && !is_var) {
      forms[i] = cast_form(forms[i], *cast, pool, env);
      // pool.alloc_single_element_form<CastElement>(nullptr, *cast, forms[i]);
    }
  }

  return forms;
}

// TODO - if we start using child classes of float/int/uint for things like degrees/meters
// we may need to adjust these.

/*!
 * type == float (exactly)?
 */
bool is_float_type(const Env& env, int my_idx, RegisterAccess var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("float");
}

/*!
 * type == int (exactly)?
 */
bool is_int_type(const Env& env, int my_idx, RegisterAccess var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("int");
}

bool is_pointer_type(const Env& env, int my_idx, RegisterAccess var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type.base_type() == "pointer";
}

/*!
 * type == uint (exactly)?
 */
bool is_uint_type(const Env& env, int my_idx, RegisterAccess var) {
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec();
  return type == TypeSpec("uint");
}

bool is_ptr_or_child(const Env& env, int my_idx, RegisterAccess var, bool) {
  // Now that decompiler types are synced up properly, we don't want this.
  //    auto type = as_var ? env.get_variable_type(var, true).base_type()
  //                       : env.get_types_before_op(my_idx).get(var.reg()).typespec().base_type();
  auto type = env.get_types_before_op(my_idx).get(var.reg()).typespec().base_type();
  return type == "pointer";
}

}  // namespace

/*!
 * Update a form to use values from the stack. Won't push to the stack.
 * This should be used to update a Form that immediately follows something being pushed.
 * Will only change the first element of the form - anything after that will jump sequencing
 */
void Form::update_children_from_stack(const Env& env,
                                      FormPool& pool,
                                      FormStack& stack,
                                      bool allow_side_effects) {
  assert(!m_elements.empty());

  std::vector<FormElement*> new_elts;

  for (size_t i = 0; i < m_elements.size(); i++) {
    if (i == 0) {
      // only bother doing the first one.
      if (!m_elements[i]->is_popped()) {
        m_elements[i]->update_from_stack(env, pool, stack, &new_elts, allow_side_effects);
      } else {
        new_elts.push_back(m_elements[i]);
      }

    } else {
      new_elts.push_back(m_elements[i]);
    }
  }

  for (auto& x : new_elts) {
    x->parent_form = this;
  }

  m_elements = new_elts;
}

/*!
 * Default update_from_stack for an element if no specific one is provided.
 */
void FormElement::update_from_stack(const Env& env,
                                    FormPool&,
                                    FormStack&,
                                    std::vector<FormElement*>*,
                                    bool) {
  throw std::runtime_error(fmt::format("update_from_stack NYI for {}", to_string(env)));
}

namespace {
Form* make_cast_if_needed(Form* in,
                          const TypeSpec& in_type,
                          const TypeSpec& out_type,
                          FormPool& pool,
                          const Env& env) {
  if (in_type == out_type) {
    return in;
  }
  return cast_form(in, out_type, pool, env);
}

std::vector<Form*> make_casts_if_needed(const std::vector<Form*>& in,
                                        const std::vector<TypeSpec>& in_types,
                                        const TypeSpec& out_type,
                                        FormPool& pool,
                                        const Env& env) {
  std::vector<Form*> out;
  assert(in.size() == in_types.size());
  for (size_t i = 0; i < in_types.size(); i++) {
    out.push_back(make_cast_if_needed(in.at(i), in_types.at(i), out_type, pool, env));
  }
  return out;
}
}  // namespace

/*!
 * Update a LoadSourceElement from the stack.
 */
void LoadSourceElement::update_from_stack(const Env& env,
                                          FormPool& pool,
                                          FormStack& stack,
                                          std::vector<FormElement*>* result,
                                          bool allow_side_effects) {
  mark_popped();
  m_addr->update_children_from_stack(env, pool, stack, allow_side_effects);
  result->push_back(this);
}

void SimpleExpressionElement::update_from_stack_identity(const Env& env,
                                                         FormPool& pool,
                                                         FormStack& stack,
                                                         std::vector<FormElement*>* result,
                                                         bool allow_side_effects) {
  auto& arg = m_expr.get_arg(0);
  if (arg.is_var()) {
    auto forms = pop_to_forms({arg.var()}, env, pool, stack, allow_side_effects);
    for (auto x : forms.at(0)->elts()) {
      result->push_back(x);
    }
  } else if (arg.is_static_addr()) {
    auto lab = env.file->labels.at(arg.label());
    if (env.file->is_string(lab.target_segment, lab.offset)) {
      auto str = env.file->get_goal_string(lab.target_segment, lab.offset / 4 - 1, false);
      result->push_back(pool.alloc_element<StringConstantElement>(str));
    } else {
      // look for a label hint:
      auto kv = env.label_types().find(lab.name);
      if (kv != env.label_types().end()) {
        auto type_name = kv->second.type_name;
        // the actual decompilation is deferred until later, once static lambdas are done.
        if (type_name == "_auto_") {
          result->push_back(pool.alloc_element<DecompiledDataElement>(lab));
        } else if (type_name == "_lambda_") {
          result->push_back(this);
        } else {
          result->push_back(pool.alloc_element<DecompiledDataElement>(lab, kv->second));
        }
      } else {
        result->push_back(this);
      }
    }

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
                                                           std::vector<FormElement*>* result,
                                                           bool allow_side_effects) {
  auto src = m_expr.get_arg(0);
  auto src_type = env.get_types_before_op(src.var().idx()).get(src.var().reg());
  std::vector<FormElement*> src_fes;
  if (src.is_var()) {
    auto forms = pop_to_forms({src.var()}, env, pool, stack, allow_side_effects);
    for (auto x : forms.at(0)->elts()) {
      src_fes.push_back(x);
    }
  } else {
    src_fes = {this};
  }

  // set ourself to identity.
  m_expr = src.as_expr();

  if (src_type.typespec() == TypeSpec("float")) {
    // got a float as an input, we can convert it to an FPR with no effect.
    for (auto x : src_fes) {
      result->push_back(x);
    }
  } else {
    // converting something else to an FPR, put an expression around it.
    result->push_back(pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::GPR_TO_FPR),
        pool.alloc_sequence_form(nullptr, src_fes)));
  }
}

void SimpleExpressionElement::update_from_stack_fpr_to_gpr(const Env& env,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result,
                                                           bool allow_side_effects) {
  auto src = m_expr.get_arg(0);
  auto src_type = env.get_types_before_op(m_my_idx).get(src.var().reg());
  if (src_type.typespec() == TypeSpec("float") || src_type.typespec() == TypeSpec("int")) {
    // set ourself to identity.
    m_expr = src.as_expr();
    // then go again.
    assert(m_popped);
    m_popped = false;
    update_from_stack(env, pool, stack, result, allow_side_effects);
  } else {
    throw std::runtime_error(
        fmt::format("FPR -> GPR applied to a {} in {}", src_type.print(), to_string(env)));
  }
}

void SimpleExpressionElement::update_from_stack_div_s(const Env& env,
                                                      FormPool& pool,
                                                      FormStack& stack,
                                                      std::vector<FormElement*>* result,
                                                      bool allow_side_effects) {
  if (is_float_type(env, m_my_idx, m_expr.get_arg(0).var()) &&
      is_float_type(env, m_my_idx, m_expr.get_arg(1).var())) {
    // todo - check the order here

    auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                             allow_side_effects);
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::DIVISION), args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    throw std::runtime_error(fmt::format("Floating point division attempted on invalid types."));
  }
}

void SimpleExpressionElement::update_from_stack_float_2(const Env& env,
                                                        FixedOperatorKind kind,
                                                        FormPool& pool,
                                                        FormStack& stack,
                                                        std::vector<FormElement*>* result,
                                                        bool allow_side_effects) {
  if (is_float_type(env, m_my_idx, m_expr.get_arg(0).var()) &&
      is_float_type(env, m_my_idx, m_expr.get_arg(1).var())) {
    // todo - check the order here

    auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                             allow_side_effects);
    auto new_form = pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind),
                                                       args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    auto type0 = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(0).var().reg());
    auto type1 = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(1).var().reg());
    throw std::runtime_error(
        fmt::format("Floating point math attempted on invalid types: {} and {} in op {}.",
                    type0.print(), type1.print(), to_string(env)));
  }
}

void SimpleExpressionElement::update_from_stack_float_1(const Env& env,
                                                        FixedOperatorKind kind,
                                                        FormPool& pool,
                                                        FormStack& stack,
                                                        std::vector<FormElement*>* result,
                                                        bool allow_side_effects) {
  if (is_float_type(env, m_my_idx, m_expr.get_arg(0).var())) {
    auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
    auto new_form =
        pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0));
    result->push_back(new_form);
  } else {
    throw std::runtime_error(fmt::format("Floating point division attempted on invalid types."));
  }
}

void SimpleExpressionElement::update_from_stack_si_1(const Env& env,
                                                     FixedOperatorKind kind,
                                                     FormPool& pool,
                                                     FormStack& stack,
                                                     std::vector<FormElement*>* result,
                                                     bool allow_side_effects) {
  auto in_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(0).var().reg()).typespec();
  auto arg = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
  result->push_back(pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(kind),
      make_cast_if_needed(arg, in_type, TypeSpec("int"), pool, env)));
}

void SimpleExpressionElement::update_from_stack_add_i(const Env& env,
                                                      FormPool& pool,
                                                      FormStack& stack,
                                                      std::vector<FormElement*>* result,
                                                      bool allow_side_effects) {
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
    args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                        allow_side_effects);
  } else {
    args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
    args.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
  }

  bool arg0_ptr = is_ptr_or_child(env, m_my_idx, m_expr.get_arg(0).var(), true);

  // Look for getting an address inside of an object.
  // (+ <integer 108 + int> process). array style access with a stride of 1.
  // in the case, both are vars.
  if (arg1_reg) {
    // lookup types.
    auto arg1_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(1).var().reg());
    auto arg0_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(0).var().reg());

    // try to find symbol to string stuff
    auto arg0_int = get_goal_integer_constant(args.at(0), env);
    if (arg0_int && (*arg0_int == SYM_INFO_OFFSET + 4) &&
        arg1_type.typespec() == TypeSpec("symbol")) {
      result->push_back(pool.alloc_element<GetSymbolStringPointer>(args.at(1)));
      return;
    }

    auto addition_matcher =
        GenericOpMatcher::or_match({GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                                    GenericOpMatcher::fixed(FixedOperatorKind::ADDITION_PTR)});
    if (arg0_type.kind == TP_Type::Kind::INTEGER_CONSTANT_PLUS_VAR) {
      // try to see if this is valid, from the type system.
      FieldReverseLookupInput input;
      input.offset = arg0_type.get_integer_constant();
      input.stride = 1;
      input.base_type = arg1_type.typespec();
      auto out = env.dts->ts.reverse_field_lookup(input);
      if (out.success && out.has_variable_token()) {
        // it is. now we have to modify things
        // first, look for the index

        auto arg0_matcher =
            Matcher::op(addition_matcher, {Matcher::any(0), Matcher::integer(input.offset)});
        auto match_result = match(arg0_matcher, args.at(0));
        if (match_result.matched) {
          bool used_index = false;
          std::vector<DerefToken> tokens;
          for (auto& tok : out.tokens) {
            if (tok.kind == FieldReverseLookupOutput::Token::Kind::VAR_IDX) {
              assert(!used_index);
              used_index = true;
              tokens.push_back(DerefToken::make_int_expr(match_result.maps.forms.at(0)));
            } else {
              tokens.push_back(to_token(tok));
            }
          }
          assert(used_index);
          result->push_back(pool.alloc_element<DerefElement>(args.at(1), out.addr_of, tokens));
          return;
        } else {
          throw std::runtime_error("Failed to match for stride 1 address access with add.");
        }
      }
    } else if (arg0_type.kind == TP_Type::Kind::INTEGER_CONSTANT_PLUS_VAR_MULT) {
      // try to see if this is valid, from the type system.
      FieldReverseLookupInput input;
      input.offset = arg0_type.get_add_int_constant();
      input.stride = arg0_type.get_mult_int_constant();
      input.base_type = arg1_type.typespec();
      auto out = env.dts->ts.reverse_field_lookup(input);
      if (out.success && out.has_variable_token()) {
        // it is. now we have to modify things
        // first, look for the index
        int p2;
        if (is_power_of_two(input.stride, &p2)) {
          // (+ (shl (-> a0-0 reg-count) 3) 28)
          auto arg0_matcher =
              Matcher::op(addition_matcher,
                          {Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                                       {Matcher::any(0), Matcher::integer(input.stride)}),
                           Matcher::integer(input.offset)});
          auto match_result = match(arg0_matcher, args.at(0));
          if (match_result.matched) {
            bool used_index = false;
            std::vector<DerefToken> tokens;
            for (auto& tok : out.tokens) {
              if (tok.kind == FieldReverseLookupOutput::Token::Kind::VAR_IDX) {
                assert(!used_index);
                used_index = true;
                tokens.push_back(DerefToken::make_int_expr(match_result.maps.forms.at(0)));
              } else {
                tokens.push_back(to_token(tok));
              }
            }
            assert(used_index);
            result->push_back(pool.alloc_element<DerefElement>(args.at(1), out.addr_of, tokens));
            return;
          } else {
            throw std::runtime_error(
                fmt::format("Failed to match for stride (power 2 {}) with add: {}", input.stride,
                            args.at(0)->to_string(env)));
          }
        } else {
          auto arg0_matcher =
              Matcher::op(addition_matcher,
                          {Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                                       {Matcher::integer(input.stride), Matcher::any(0)}),
                           Matcher::integer(input.offset)});
          auto match_result = match(arg0_matcher, args.at(0));
          if (match_result.matched) {
            bool used_index = false;
            std::vector<DerefToken> tokens;
            for (auto& tok : out.tokens) {
              if (tok.kind == FieldReverseLookupOutput::Token::Kind::VAR_IDX) {
                assert(!used_index);
                used_index = true;
                tokens.push_back(DerefToken::make_int_expr(match_result.maps.forms.at(0)));
              } else {
                tokens.push_back(to_token(tok));
              }
            }
            assert(used_index);
            result->push_back(pool.alloc_element<DerefElement>(args.at(1), out.addr_of, tokens));
            return;
          } else {
            throw std::runtime_error(
                fmt::format("Failed to match for stride (non power 2 {}) with add: {}",
                            input.stride, args.at(0)->to_string(env)));
          }
        }
      }
    } else if (arg1_type.kind == TP_Type::Kind::PRODUCT_WITH_CONSTANT &&
               arg0_type.kind == TP_Type::Kind::TYPESPEC &&
               arg0_type.typespec().base_type() == "inline-array") {
      FieldReverseLookupInput rd_in;
      rd_in.deref = std::nullopt;
      rd_in.stride = arg1_type.get_multiplier();
      rd_in.offset = 0;
      rd_in.base_type = arg0_type.typespec();
      auto rd = env.dts->ts.reverse_field_multi_lookup(rd_in);
      int idx_of_success = -1;
      if (rd.success) {
        for (int i = 0; i < (int)rd.results.size(); i++) {
          if (rd.results.at(i).has_variable_token()) {
            idx_of_success = i;
            break;
          }
        }
      }

      if (idx_of_success >= 0) {
        auto& rd_ok = rd.results.at(idx_of_success);
        auto arg1_matcher = Matcher::match_or(
            {Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                         {Matcher::any(0), Matcher::integer(rd_in.stride)}),
             Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                         {Matcher::integer(rd_in.stride), Matcher::any(0)})});
        auto match_result = match(arg1_matcher, args.at(1));
        if (match_result.matched) {
          bool used_index = false;
          std::vector<DerefToken> tokens;
          for (auto& tok : rd_ok.tokens) {
            if (tok.kind == FieldReverseLookupOutput::Token::Kind::VAR_IDX) {
              assert(!used_index);
              used_index = true;
              tokens.push_back(DerefToken::make_int_expr(match_result.maps.forms.at(0)));
            } else {
              tokens.push_back(to_token(tok));
            }
          }
          assert(used_index);
          result->push_back(pool.alloc_element<DerefElement>(args.at(0), rd_ok.addr_of, tokens));
          return;
        } else {
          throw std::runtime_error("Failed to match product_with_constant inline array access.");
        }
      }
    } else if (arg0_type.kind == TP_Type::Kind::PRODUCT_WITH_CONSTANT &&
               arg1_type.kind == TP_Type::Kind::TYPESPEC &&
               arg1_type.typespec().base_type() == "inline-array") {
      FieldReverseLookupInput rd_in;
      rd_in.deref = std::nullopt;
      rd_in.stride = arg0_type.get_multiplier();
      rd_in.offset = 0;
      rd_in.base_type = arg1_type.typespec();
      auto rd = env.dts->ts.reverse_field_multi_lookup(rd_in);
      int idx_of_success = -1;
      if (rd.success) {
        for (int i = 0; i < (int)rd.results.size(); i++) {
          if (rd.results.at(i).has_variable_token()) {
            idx_of_success = i;
            break;
          }
        }
      }
      // fmt::print("here {} {} {}\n", rd_in.base_type.print(), rd.success,
      // rd.has_variable_token());

      if (idx_of_success >= 0) {
        auto& rd_ok = rd.results.at(idx_of_success);
        auto arg0_matcher = Matcher::match_or(
            {Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                         {Matcher::any(0), Matcher::integer(rd_in.stride)}),
             Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                         {Matcher::integer(rd_in.stride), Matcher::any(0)})});
        auto match_result = match(arg0_matcher, args.at(0));
        if (match_result.matched) {
          bool used_index = false;
          std::vector<DerefToken> tokens;
          for (auto& tok : rd_ok.tokens) {
            if (tok.kind == FieldReverseLookupOutput::Token::Kind::VAR_IDX) {
              assert(!used_index);
              used_index = true;
              tokens.push_back(DerefToken::make_int_expr(match_result.maps.forms.at(0)));
            } else {
              tokens.push_back(to_token(tok));
            }
          }
          assert(used_index);
          result->push_back(pool.alloc_element<DerefElement>(args.at(1), rd_ok.addr_of, tokens));
          return;
        } else {
          throw std::runtime_error("Failed to match product_with_constant inline array access.");
        }
      }
    }
  }

  if ((arg0_i && arg1_i) || (arg0_u && arg1_u)) {
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::ADDITION), args.at(0), args.at(1));
    result->push_back(new_form);
  } else if (arg0_ptr) {
    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::ADDITION_PTR), args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    auto casted0 = args.at(0);

    auto arg0_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(0).var().reg());
    if (!arg0_i && !arg0_u && arg0_type.typespec() != TypeSpec("binteger")) {
      casted0 = pool.alloc_single_element_form<CastElement>(
          nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(0));
    }

    auto casted1 = pool.alloc_single_element_form<CastElement>(
        nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(1));

    auto new_form = pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::ADDITION), casted0, casted1);
    result->push_back(new_form);
  }
}

void SimpleExpressionElement::update_from_stack_mult_si(const Env& env,
                                                        FormPool& pool,
                                                        FormStack& stack,
                                                        std::vector<FormElement*>* result,
                                                        bool allow_side_effects) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());

  auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                           allow_side_effects);

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
                                                           std::vector<FormElement*>* result,
                                                           bool allow_side_effects,
                                                           bool reverse) {
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  bool arg1_i = true;
  bool arg1_reg = m_expr.get_arg(1).is_var();
  if (arg1_reg) {
    arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());
  } else {
    assert(m_expr.get_arg(1).is_int());
  }

  std::vector<Form*> args;
  if (arg1_reg) {
    if (reverse) {
      args = pop_to_forms({m_expr.get_arg(1).var(), m_expr.get_arg(0).var()}, env, pool, stack,
                          allow_side_effects);
      auto temp = args.at(1);
      args.at(1) = args.at(0);
      args.at(0) = temp;
    } else {
      args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                          allow_side_effects);
    }

  } else {
    args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
    args.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
  }

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
                                                           std::vector<FormElement*>* result,
                                                           bool allow_side_effects) {
  auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
  bool arg1_u = true;
  bool arg1_reg = m_expr.get_arg(1).is_var();
  if (arg1_reg) {
    arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());
  } else {
    assert(m_expr.get_arg(1).is_int());
  }

  std::vector<Form*> args;
  if (arg1_reg) {
    args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                        allow_side_effects);
  } else {
    args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
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

void SimpleExpressionElement::update_from_stack_pcypld(const Env& env,
                                                       FormPool& pool,
                                                       FormStack& stack,
                                                       std::vector<FormElement*>* result,
                                                       bool allow_side_effects) {
  std::vector<Form*> args;
  std::vector<RegisterAccess> ras;
  for (int arg_idx = 0; arg_idx < m_expr.args(); arg_idx++) {
    if (m_expr.get_arg(arg_idx).is_var()) {
      ras.push_back(m_expr.get_arg(arg_idx).var());
    }
  }
  auto popped_args = pop_to_forms(ras, env, pool, stack, allow_side_effects);

  int ras_idx = 0;
  for (int arg_idx = 0; arg_idx < m_expr.args(); arg_idx++) {
    if (m_expr.get_arg(arg_idx).is_var()) {
      args.push_back(popped_args.at(ras_idx));
      ras_idx++;
    } else {
      args.push_back(
          pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(arg_idx)));
    }
  }

  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::PCPYLD), args.at(0), args.at(1));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_vector_plus_minus(bool is_add,
                                                                  const Env& env,
                                                                  FormPool& pool,
                                                                  FormStack& stack,
                                                                  std::vector<FormElement*>* result,
                                                                  bool allow_side_effects) {
  std::vector<Form*> popped_args =
      pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var(), m_expr.get_arg(2).var()}, env,
                   pool, stack, allow_side_effects);

  for (int i = 0; i < 3; i++) {
    auto arg_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(i).var().reg());
    if (arg_type.typespec() != TypeSpec("vector")) {
      popped_args.at(i) = cast_form(popped_args.at(i), TypeSpec("vector"), pool, env);
    }
  }

  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(is_add ? FixedOperatorKind::VECTOR_PLUS
                                         : FixedOperatorKind::VECTOR_MINUS),
      std::vector<Form*>{popped_args.at(0), popped_args.at(1), popped_args.at(2)});
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_vector_float_product(
    const Env& env,
    FormPool& pool,
    FormStack& stack,
    std::vector<FormElement*>* result,
    bool allow_side_effects) {
  std::vector<Form*> popped_args =
      pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var(), m_expr.get_arg(2).var()}, env,
                   pool, stack, allow_side_effects);

  for (int i = 0; i < 3; i++) {
    auto arg_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(i).var().reg());
    TypeSpec desired_type(i == 2 ? "float" : "vector");
    if (arg_type.typespec() != desired_type) {
      popped_args.at(i) = cast_form(popped_args.at(i), desired_type, pool, env);
    }
  }

  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::VECTOR_FLOAT_PRODUCT),
      std::vector<Form*>{popped_args.at(0), popped_args.at(1), popped_args.at(2)});
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_vector_3_dot(const Env& env,
                                                             FormPool& pool,
                                                             FormStack& stack,
                                                             std::vector<FormElement*>* result,
                                                             bool allow_side_effects) {
  std::vector<Form*> popped_args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()},
                                                env, pool, stack, allow_side_effects);

  for (int i = 0; i < 2; i++) {
    auto arg_type = env.get_types_before_op(m_my_idx).get(m_expr.get_arg(i).var().reg());
    if (arg_type.typespec() != TypeSpec("vector")) {
      popped_args.at(i) = cast_form(popped_args.at(i), TypeSpec("vector"), pool, env);
    }
  }

  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::VECTOR_3_DOT),
      std::vector<Form*>{popped_args.at(0), popped_args.at(1)});
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_copy_first_int_2(const Env& env,
                                                                 FixedOperatorKind kind,
                                                                 FormPool& pool,
                                                                 FormStack& stack,
                                                                 std::vector<FormElement*>* result,
                                                                 bool allow_side_effects) {
  auto arg0_type = env.get_variable_type(m_expr.get_arg(0).var(), true);
  auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
  auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
  if (!m_expr.get_arg(1).is_var()) {
    auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);

    if (!arg0_i && !arg0_u) {
      auto bti = dynamic_cast<EnumType*>(env.dts->ts.lookup_type(arg0_type));
      if (bti) {
        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(kind), args.at(0),
            cast_form(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)),
                      arg0_type, pool, env));
        result->push_back(new_form);
      } else {
        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(kind),
            pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(0)),
            pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
        result->push_back(new_form);
      }
    } else {
      auto new_form = pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(kind), args.at(0),
          pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
      result->push_back(new_form);
    }

    return;
  }
  auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());
  auto arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());

  auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                           allow_side_effects);

  if ((arg0_i && arg1_i) || (arg0_u && arg1_u)) {
    auto new_form = pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind),
                                                       args.at(0), args.at(1));
    result->push_back(new_form);
  } else {
    auto cast = pool.alloc_single_element_form<CastElement>(
        nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(1));
    if (kind == FixedOperatorKind::SUBTRACTION &&
        is_pointer_type(env, m_my_idx, m_expr.get_arg(0).var())) {
      kind = FixedOperatorKind::SUBTRACTION_PTR;
    }
    auto new_form =
        pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0), cast);
    result->push_back(new_form);
  }
}

namespace {
Form* strip_int_or_uint_cast(Form* in) {
  auto as_cast = in->try_as_element<CastElement>();
  if (as_cast && (as_cast->type() == TypeSpec("int") || as_cast->type() == TypeSpec("uint"))) {
    return as_cast->source();
  }
  return in;
}
}  // namespace

void SimpleExpressionElement::update_from_stack_logor_or_logand(const Env& env,
                                                                FixedOperatorKind kind,
                                                                FormPool& pool,
                                                                FormStack& stack,
                                                                std::vector<FormElement*>* result,
                                                                bool allow_side_effects) {
  auto arg0_type = env.get_variable_type(m_expr.get_arg(0).var(), true);

  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && m_expr.get_arg(1).is_int()) {
    // andi, ori with bitfield.
    auto base = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
    auto read_elt = dynamic_cast<BitfieldAccessElement*>(base->try_as_single_element());
    if (!read_elt) {
      read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    }

    BitfieldManip::Kind manip_kind;
    if (kind == FixedOperatorKind::LOGAND) {
      manip_kind = BitfieldManip::Kind::LOGAND_WITH_CONSTANT_INT;
    } else if (kind == FixedOperatorKind::LOGIOR) {
      manip_kind = BitfieldManip::Kind::LOGIOR_WITH_CONSTANT_INT;
    } else {
      assert(false);
    }

    BitfieldManip step(manip_kind, m_expr.get_arg(1).get_int());
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    if (other) {
      result->push_back(other);
    } else {
      result->push_back(read_elt);
    }
    return;

  } else if (!m_expr.get_arg(1).is_var()) {
    // andi, something else (don't think this can happen?)
    update_from_stack_copy_first_int_2(env, kind, pool, stack, result, allow_side_effects);
  } else {
    // and, two forms
    auto arg1_type = env.get_variable_type(m_expr.get_arg(1).var(), true);
    auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
    auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
    auto arg1_i = is_int_type(env, m_my_idx, m_expr.get_arg(1).var());
    auto arg1_u = is_uint_type(env, m_my_idx, m_expr.get_arg(1).var());
    auto arg0_n = arg0_i || arg0_u;
    auto arg1_n = arg1_i || arg1_u;

    auto args = pop_to_forms({m_expr.get_arg(0).var(), m_expr.get_arg(1).var()}, env, pool, stack,
                             allow_side_effects);

    if (bitfield_info) {
      // either the immediate didn't fit in the 16-bit imm or it's with a variable
      bool made_new_read_elt = false;
      auto read_elt = dynamic_cast<BitfieldAccessElement*>(args.at(0)->try_as_single_element());
      if (!read_elt) {
        read_elt = pool.alloc_element<BitfieldAccessElement>(args.at(0), arg0_type);
        made_new_read_elt = true;
      }

      auto stripped_arg1 = strip_int_or_uint_cast(args.at(1));
      auto arg1_atom = form_as_atom(strip_int_or_uint_cast(args.at(1)));
      if (arg1_atom && arg1_atom->is_int()) {
        BitfieldManip::Kind manip_kind;
        if (kind == FixedOperatorKind::LOGAND) {
          manip_kind = BitfieldManip::Kind::LOGAND_WITH_CONSTANT_INT;
        } else if (kind == FixedOperatorKind::LOGIOR) {
          manip_kind = BitfieldManip::Kind::LOGIOR_WITH_CONSTANT_INT;
        } else {
          assert(false);
        }
        BitfieldManip step(manip_kind, arg1_atom->get_int());
        auto other = read_elt->push_step(step, env.dts->ts, pool, env);
        assert(!other);  // shouldn't be complete.
        result->push_back(read_elt);
        return;
      } else if (!made_new_read_elt) {
        BitfieldManip::Kind manip_kind;
        if (kind == FixedOperatorKind::LOGAND) {
          manip_kind = BitfieldManip::Kind::LOGAND_WITH_FORM;
        } else if (kind == FixedOperatorKind::LOGIOR) {
          manip_kind = BitfieldManip::Kind::LOGIOR_WITH_FORM;
        } else {
          assert(false);
        }
        auto step = BitfieldManip::from_form(manip_kind, stripped_arg1);
        auto other = read_elt->push_step(step, env.dts->ts, pool, env);
        if (other) {
          result->push_back(other);
        } else {
          result->push_back(read_elt);
        }
        return;
      }
    }

    if ((arg0_i && arg1_i) || (arg0_u && arg1_u) ||
        (arg0_n && arg1_type.base_type() == "pointer") ||
        (arg1_n && arg0_type.base_type() == "pointer")) {
      // types already good
      // we also allow (logand intvar pointer) and (logand pointer intvar)
      auto new_form = pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind),
                                                         args.at(0), args.at(1));
      result->push_back(new_form);
      // types bad, insert cast.
    } else {
      // this is an ugly hack to make (logand (lognot (enum-bitfield xxxx)) work.
      // I have only one example for this, so I think this unlikely to work in all cases.
      if (m_expr.get_arg(1).is_var()) {
        auto eti = env.dts->ts.try_enum_lookup(arg1_type.base_type());
        if (eti) {
          auto integer = get_goal_integer_constant(args.at(0), env);
          if (integer && ((s64)*integer) < 0) {
            // clearing a bitfield.
            auto elts = decompile_bitfield_enum_from_int(arg1_type, env.dts->ts, ~*integer);
            auto oper =
                GenericOperator::make_function(pool.alloc_single_element_form<ConstantTokenElement>(
                    nullptr, arg1_type.base_type()));
            std::vector<Form*> form_elts;
            for (auto& x : elts) {
              form_elts.push_back(pool.alloc_single_element_form<ConstantTokenElement>(nullptr, x));
            }
            auto inverted =
                pool.alloc_single_element_form<GenericElement>(nullptr, oper, form_elts);
            auto normal = pool.alloc_single_element_form<GenericElement>(
                nullptr, GenericOperator::make_fixed(FixedOperatorKind::LOGNOT), inverted);
            auto new_form = pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind),
                                                               normal, args.at(1));
            result->push_back(new_form);
            //                assert(false);
            return;
          }
        }
      }

      auto cast = pool.alloc_single_element_form<CastElement>(
          nullptr, TypeSpec(arg0_i ? "int" : "uint"), args.at(1));
      auto new_form =
          pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), args.at(0), cast);
      result->push_back(new_form);
    }
  }
}

void SimpleExpressionElement::update_from_stack_left_shift(const Env& env,
                                                           FormPool& pool,
                                                           FormStack& stack,
                                                           std::vector<FormElement*>* result,
                                                           bool allow_side_effects) {
  auto arg0_type = env.get_variable_type(m_expr.get_arg(0).var(), true);

  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && m_expr.get_arg(1).is_int()) {
    auto base = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    BitfieldManip step(BitfieldManip::Kind::LEFT_SHIFT, m_expr.get_arg(1).get_int());
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    assert(!other);  // shouldn't be complete.
    result->push_back(read_elt);
  } else {
    // try to turn this into a multiplication, if possible
    if (m_expr.get_arg(1).is_int()) {
      auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
      int sa = m_expr.get_arg(1).get_int();

      auto as_ba = args.at(0)->try_as_element<BitfieldAccessElement>();
      if (as_ba) {
        BitfieldManip step(BitfieldManip::Kind::LEFT_SHIFT, m_expr.get_arg(1).get_int());
        auto other = as_ba->push_step(step, env.dts->ts, pool, env);
        assert(!other);  // shouldn't be complete.
        result->push_back(as_ba);
        return;
      }

      // somewhat arbitrary threshold to switch from multiplications to shift.
      if (sa < 10) {
        s64 multiplier = (s64(1) << sa);

        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::MULTIPLICATION), args.at(0),
            pool.alloc_single_element_form<SimpleAtomElement>(
                nullptr, SimpleAtom::make_int_constant(multiplier)));
        result->push_back(new_form);
        return;
      }

      auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
      auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
      if (!arg0_i && !arg0_u) {
        auto bti = dynamic_cast<EnumType*>(env.dts->ts.lookup_type(arg0_type));
        if (bti) {
          auto new_form = pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::SHL), args.at(0),
              cast_form(
                  pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)),
                  arg0_type, pool, env));
          result->push_back(new_form);
        } else {
          auto new_form = pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::SHL),
              pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(0)),
              pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
          result->push_back(new_form);
        }
      } else {
        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::SHL), args.at(0),
            pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
        result->push_back(new_form);
      }

      return;
    }

    update_from_stack_copy_first_int_2(env, FixedOperatorKind::SHL, pool, stack, result,
                                       allow_side_effects);
  }
}

void SimpleExpressionElement::update_from_stack_right_shift_logic(const Env& env,
                                                                  FormPool& pool,
                                                                  FormStack& stack,
                                                                  std::vector<FormElement*>* result,
                                                                  bool allow_side_effects) {
  auto arg0_type = env.get_variable_type(m_expr.get_arg(0).var(), true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && m_expr.get_arg(1).is_int()) {
    auto base = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_LOGICAL, m_expr.get_arg(1).get_int());
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    assert(other);  // should be a high field.
    result->push_back(other);
  } else {
    auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
    auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
    if (m_expr.get_arg(1).is_int()) {
      auto arg =
          pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
      auto as_bitfield_access = dynamic_cast<BitfieldAccessElement*>(arg->try_as_single_element());

      if (as_bitfield_access) {
        BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_LOGICAL, m_expr.get_arg(1).get_int());
        auto next = as_bitfield_access->push_step(step, env.dts->ts, pool, env);
        if (next) {
          result->push_back(next);
        } else {
          result->push_back(as_bitfield_access);
        }
      } else {
        /*
        int sa = m_expr.get_arg(1).get_int();
        if (sa < 10) {
          if (!arg0_u) {
            arg = cast_form(arg, TypeSpec("uint"), pool, env);
          }
          s64 multiplier = (s64(1) << sa);

          auto new_form = pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::DIVISION), arg,
              pool.alloc_single_element_form<SimpleAtomElement>(
                  nullptr, SimpleAtom::make_int_constant(multiplier)));
          result->push_back(new_form);
          return;
        }
         */

        if (!arg0_i && !arg0_u) {
          auto new_form = pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::SHR),
              pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), arg),
              pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
          result->push_back(new_form);
        } else {
          auto new_form = pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::SHR), arg,
              pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
          result->push_back(new_form);
        }
      }
    } else {
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::SHR, pool, stack, result,
                                         allow_side_effects);
    }
  }
}

void SimpleExpressionElement::update_from_stack_right_shift_arith(const Env& env,
                                                                  FormPool& pool,
                                                                  FormStack& stack,
                                                                  std::vector<FormElement*>* result,
                                                                  bool allow_side_effects) {
  auto arg0_type = env.get_variable_type(m_expr.get_arg(0).var(), true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && m_expr.get_arg(1).is_int()) {
    auto base = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_ARITH, m_expr.get_arg(1).get_int());
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    assert(other);  // should be a high field.
    result->push_back(other);
  } else {
    if (m_expr.get_arg(1).is_int()) {
      if (m_expr.get_arg(1).get_int() < 10) {
        auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
        auto arg =
            pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects).at(0);
        int sa = m_expr.get_arg(1).get_int();

        if (!arg0_i) {
          arg = cast_form(arg, TypeSpec("int"), pool, env);
        }
        s64 multiplier = (s64(1) << sa);

        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::DIVISION), arg,
            pool.alloc_single_element_form<SimpleAtomElement>(
                nullptr, SimpleAtom::make_int_constant(multiplier)));
        result->push_back(new_form);
        return;
      }

      auto arg0_i = is_int_type(env, m_my_idx, m_expr.get_arg(0).var());
      auto arg0_u = is_uint_type(env, m_my_idx, m_expr.get_arg(0).var());
      auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);

      auto as_ba = args.at(0)->try_as_element<BitfieldAccessElement>();
      if (as_ba) {
        BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_ARITH, m_expr.get_arg(1).get_int());
        auto other = as_ba->push_step(step, env.dts->ts, pool, env);
        assert(other);  // should be a high field.
        result->push_back(other);
        return;
      }

      if (!arg0_i && !arg0_u) {
        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::SAR),
            pool.alloc_single_element_form<CastElement>(nullptr, TypeSpec("int"), args.at(0)),
            pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
        result->push_back(new_form);
      } else {
        auto new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::SAR), args.at(0),
            pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_expr.get_arg(1)));
        result->push_back(new_form);
      }

      return;
    }

    update_from_stack_copy_first_int_2(env, FixedOperatorKind::SAR, pool, stack, result,
                                       allow_side_effects);
  }
}

void SimpleExpressionElement::update_from_stack_lognot(const Env& env,
                                                       FormPool& pool,
                                                       FormStack& stack,
                                                       std::vector<FormElement*>* result,
                                                       bool allow_side_effects) {
  auto args = pop_to_forms({m_expr.get_arg(0).var()}, env, pool, stack, allow_side_effects);
  auto new_form = pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::LOGNOT), args.at(0));
  result->push_back(new_form);
}

void SimpleExpressionElement::update_from_stack_int_to_float(const Env& env,
                                                             FormPool& pool,
                                                             FormStack& stack,
                                                             std::vector<FormElement*>* result,
                                                             bool allow_side_effects) {
  auto var = m_expr.get_arg(0).var();
  auto arg = pop_to_forms({var}, env, pool, stack, allow_side_effects).at(0);
  // if we convert from a GPR to FPR, then immediately to int to float, we can strip away the
  // the gpr->fpr operation beacuse it doesn't matter.
  auto fpr_convert_matcher =
      Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::GPR_TO_FPR), {Matcher::any(0)});
  auto type = env.get_types_before_op(var.idx()).get(var.reg()).typespec();
  if (type == TypeSpec("int") || type == TypeSpec("uint")) {
    auto mr = match(fpr_convert_matcher, arg);
    if (mr.matched) {
      arg = mr.maps.forms.at(0);
    }
    result->push_back(pool.alloc_element<CastElement>(TypeSpec("float"), arg, true));
  } else {
    throw std::runtime_error(fmt::format("Used int to float on a {} from {}: {}", type.print(),
                                         var.to_form(env).print(), arg->to_string(env)));
  }
}

void SimpleExpressionElement::update_from_stack_float_to_int(const Env& env,
                                                             FormPool& pool,
                                                             FormStack& stack,
                                                             std::vector<FormElement*>* result,
                                                             bool allow_side_effects) {
  auto var = m_expr.get_arg(0).var();
  auto arg = pop_to_forms({var}, env, pool, stack, allow_side_effects).at(0);
  auto type = env.get_types_before_op(var.idx()).get(var.reg()).typespec();
  if (type == TypeSpec("float")) {
    result->push_back(pool.alloc_element<CastElement>(TypeSpec("int"), arg, true));
  } else {
    throw std::runtime_error("Used float to int on a " + type.print());
  }
}

namespace {
GenericElement* allocate_fixed_op(FormPool& pool, FixedOperatorKind kind, Form* op1) {
  return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(kind), op1);
}
}  // namespace

void SimpleExpressionElement::update_from_stack_subu_l32_s7(const Env& env,
                                                            FormPool& pool,
                                                            FormStack& stack,
                                                            std::vector<FormElement*>* result,
                                                            bool allow_side_effects) {
  auto var = m_expr.get_arg(0).var();
  auto arg = pop_to_forms({var}, env, pool, stack, allow_side_effects).at(0);
  auto type = env.get_types_before_op(var.idx()).get(var.reg()).typespec();
  if (type != TypeSpec("handle")) {
    env.func->warnings.general_warning(
        ".subu (32-bit) used on a {} at idx {}. This probably should be a handle.", type.print(),
        var.idx());
  }
  result->push_back(allocate_fixed_op(pool, FixedOperatorKind::L32_NOT_FALSE_CBOOL, arg));
}

void SimpleExpressionElement::update_from_stack(const Env& env,
                                                FormPool& pool,
                                                FormStack& stack,
                                                std::vector<FormElement*>* result,
                                                bool allow_side_effects) {
  mark_popped();
  switch (m_expr.kind()) {
    case SimpleExpression::Kind::IDENTITY:
      update_from_stack_identity(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::GPR_TO_FPR:
      update_from_stack_gpr_to_fpr(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::FPR_TO_GPR:
      update_from_stack_fpr_to_gpr(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::DIV_S:
      update_from_stack_div_s(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::SUB_S:
      update_from_stack_float_2(env, FixedOperatorKind::SUBTRACTION, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::MUL_S:
      update_from_stack_float_2(env, FixedOperatorKind::MULTIPLICATION, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::ADD_S:
      update_from_stack_float_2(env, FixedOperatorKind::ADDITION, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::MAX_S:
      update_from_stack_float_2(env, FixedOperatorKind::FMAX, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::MIN_S:
      update_from_stack_float_2(env, FixedOperatorKind::FMIN, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::SQRT_S:
      update_from_stack_float_1(env, FixedOperatorKind::SQRT, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::ABS_S:
      update_from_stack_float_1(env, FixedOperatorKind::FABS, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::NEG:
      update_from_stack_si_1(env, FixedOperatorKind::SUBTRACTION, pool, stack, result,
                             allow_side_effects);
      break;
    case SimpleExpression::Kind::NEG_S:
      update_from_stack_float_1(env, FixedOperatorKind::SUBTRACTION, pool, stack, result,
                                allow_side_effects);
      break;
    case SimpleExpression::Kind::ADD:
      update_from_stack_add_i(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::SUB:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::SUBTRACTION, pool, stack, result,
                                         allow_side_effects);
      break;
    case SimpleExpression::Kind::MUL_SIGNED:
      update_from_stack_mult_si(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::DIV_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::DIVISION, pool, stack, result,
                                   allow_side_effects, false);
      break;
    case SimpleExpression::Kind::DIV_UNSIGNED:
      update_from_stack_force_ui_2(env, FixedOperatorKind::DIVISION, pool, stack, result,
                                   allow_side_effects);
      break;
    case SimpleExpression::Kind::MOD_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MOD, pool, stack, result,
                                   allow_side_effects, false);
      break;
    case SimpleExpression::Kind::MOD_UNSIGNED:
      update_from_stack_force_ui_2(env, FixedOperatorKind::MOD, pool, stack, result,
                                   allow_side_effects);
      break;
    case SimpleExpression::Kind::MIN_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MIN, pool, stack, result,
                                   allow_side_effects, false);
      break;
    case SimpleExpression::Kind::MAX_SIGNED:
      update_from_stack_force_si_2(env, FixedOperatorKind::MAX, pool, stack, result,
                                   allow_side_effects, false);
      break;
    case SimpleExpression::Kind::AND:
      update_from_stack_logor_or_logand(env, FixedOperatorKind::LOGAND, pool, stack, result,
                                        allow_side_effects);
      break;
    case SimpleExpression::Kind::OR:
      update_from_stack_logor_or_logand(env, FixedOperatorKind::LOGIOR, pool, stack, result,
                                        allow_side_effects);
      break;
    case SimpleExpression::Kind::NOR:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGNOR, pool, stack, result,
                                         allow_side_effects);
      break;
    case SimpleExpression::Kind::XOR:
      update_from_stack_copy_first_int_2(env, FixedOperatorKind::LOGXOR, pool, stack, result,
                                         allow_side_effects);
      break;
    case SimpleExpression::Kind::LOGNOT:
      update_from_stack_lognot(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::LEFT_SHIFT:
      update_from_stack_left_shift(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::RIGHT_SHIFT_LOGIC:
      update_from_stack_right_shift_logic(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::RIGHT_SHIFT_ARITH:
      update_from_stack_right_shift_arith(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::MUL_UNSIGNED:
      update_from_stack_force_ui_2(env, FixedOperatorKind::MULTIPLICATION, pool, stack, result,
                                   allow_side_effects);
      break;
    case SimpleExpression::Kind::INT_TO_FLOAT:
      update_from_stack_int_to_float(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::FLOAT_TO_INT:
      update_from_stack_float_to_int(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::PCPYLD:
      update_from_stack_pcypld(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::VECTOR_PLUS:
      update_from_stack_vector_plus_minus(true, env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::VECTOR_MINUS:
      update_from_stack_vector_plus_minus(false, env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::VECTOR_FLOAT_PRODUCT:
      update_from_stack_vector_float_product(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::SUBU_L32_S7:
      update_from_stack_subu_l32_s7(env, pool, stack, result, allow_side_effects);
      break;
    case SimpleExpression::Kind::VECTOR_3_DOT:
      update_from_stack_vector_3_dot(env, pool, stack, result, allow_side_effects);
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
  mark_popped();
  for (auto x : m_src->elts()) {
    assert(x->parent_form == m_src);
  }
  assert(m_src->parent_element == this);

  // hack for method stuff
  if (is_dead_set()) {
    stack.push_value_to_reg_dead(m_dst, m_src, true, m_src_type, m_var_info);
    return;
  }

  // if we are a reg-reg move that consumes the original, push it without popping from stack.
  // it is the Stack's responsibility to untangle these later on.
  if (m_src->is_single_element()) {
    auto src_as_se = dynamic_cast<SimpleExpressionElement*>(m_src->back());
    if (src_as_se) {
      if (src_as_se->expr().kind() == SimpleExpression::Kind::IDENTITY &&
          src_as_se->expr().get_arg(0).is_var()) {
        // this can happen late in the case of coloring moves which are also gpr -> fpr's
        // so they don't get caught by SetVarOp::get_as_form's check.
        if (env.op_id_is_eliminated_coloring_move(src_as_se->expr().get_arg(0).var().idx())) {
          m_var_info.is_eliminated_coloring_move = true;
        }

        auto var = src_as_se->expr().get_arg(0).var();
        auto& info = env.reg_use().op.at(var.idx());
        if (info.consumes.find(var.reg()) != info.consumes.end()) {
          stack.push_non_seq_reg_to_reg(m_dst, src_as_se->expr().get_arg(0).var(), m_src,
                                        m_src_type, m_var_info);
          return;
        }
      }
    }
  }

  // we aren't a reg-reg move, so update our source
  m_src->update_children_from_stack(env, pool, stack, true);

  for (auto x : m_src->elts()) {
    assert(x->parent_form == m_src);
  }

  if (m_src->is_single_element()) {
    auto src_as_se = dynamic_cast<SimpleExpressionElement*>(m_src->back());
    if (src_as_se) {
      if (src_as_se->expr().kind() == SimpleExpression::Kind::IDENTITY &&
          m_dst.reg().get_kind() == Reg::FPR && src_as_se->expr().get_arg(0).is_int() &&
          src_as_se->expr().get_arg(0).get_int() == 0) {
        // not sure this is the best place for this.
        stack.push_value_to_reg(m_dst,
                                pool.alloc_single_element_form<ConstantFloatElement>(nullptr, 0.0),
                                true, m_src_type, m_var_info);
        return;
      }

      // this might get skipped earlier because gpr->fpr gets wrapped in an operation that's
      // stripped off by update_children_from_stack.
      if (src_as_se->expr().kind() == SimpleExpression::Kind::IDENTITY &&
          src_as_se->expr().get_arg(0).is_var()) {
        if (env.op_id_is_eliminated_coloring_move(src_as_se->expr().get_arg(0).var().idx())) {
          m_var_info.is_eliminated_coloring_move = true;
        }
      }
    }
  }

  stack.push_value_to_reg(m_dst, m_src, true, m_src_type, m_var_info);
  for (auto x : m_src->elts()) {
    assert(x->parent_form == m_src);
  }
}

void SetFormFormElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  assert(m_popped);
  assert(m_real_push_count == 0);
  m_real_push_count++;

  // check for bitfield setting:
  auto src_as_bf_set = dynamic_cast<ModifiedCopyBitfieldElement*>(m_src->try_as_single_element());
  if (src_as_bf_set && src_as_bf_set->mods().size() == 1) {
    auto dst_form = m_dst->to_form(env);
    auto src_form = src_as_bf_set->base()->to_form(env);
    if (dst_form == src_form) {
      // success!
      auto value = src_as_bf_set->mods().at(0).value;
      value->parent_element = this;

      // make the (-> thing bitfield)
      auto field_token = DerefToken::make_field_name(src_as_bf_set->mods().at(0).field_name);
      auto loc_elt = pool.alloc_element<DerefElement>(m_dst, false, field_token);
      loc_elt->inline_nested();
      auto loc = pool.alloc_single_form(nullptr, loc_elt);
      loc->parent_element = this;

      m_dst = loc;
      m_src = value;
    }
  }

  stack.push_form_element(this, true);
}

void StoreInSymbolElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  auto sym = pool.alloc_single_element_form<SimpleExpressionElement>(
      nullptr, SimpleAtom::make_sym_val(m_sym_name).as_expr(), m_my_idx);
  auto val = pool.alloc_single_element_form<SimpleExpressionElement>(nullptr, m_value, m_my_idx);
  val->update_children_from_stack(env, pool, stack, true);

  auto elt = pool.alloc_element<SetFormFormElement>(sym, val, m_cast_for_set, m_cast_for_define);
  elt->mark_popped();
  stack.push_form_element(elt, true);
}

void StoreInPairElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  auto op = m_is_car ? FixedOperatorKind::CAR : FixedOperatorKind::CDR;
  if (m_value.is_var()) {
    auto vars = std::vector<RegisterAccess>({m_value.var(), m_pair});
    auto popped = pop_to_forms(vars, env, pool, stack, true);
    auto addr = pool.alloc_single_element_form<GenericElement>(
        nullptr, GenericOperator::make_fixed(op), popped.at(1));
    addr->mark_popped();
    auto fr = pool.alloc_element<SetFormFormElement>(addr, popped.at(0));
    fr->mark_popped();
    stack.push_form_element(fr, true);
  } else {
    auto val = pool.alloc_single_element_form<SimpleExpressionElement>(nullptr, m_value, m_my_idx);
    val->mark_popped();
    auto addr = pool.alloc_single_element_form<GenericElement>(
        nullptr, GenericOperator::make_fixed(op),
        pop_to_forms({m_pair}, env, pool, stack, true).at(0));
    addr->mark_popped();
    auto fr = pool.alloc_element<SetFormFormElement>(addr, val);
    fr->mark_popped();
    stack.push_form_element(fr, true);
  }
}

namespace {

Form* make_optional_cast(const std::optional<TypeSpec>& cast_type,
                         Form* in,
                         FormPool& pool,
                         const Env& env) {
  if (cast_type) {
    return cast_form(in, *cast_type, pool, env);
  } else {
    return in;
  }
}

bool try_to_rewrite_vector_inline_ctor(const Env& env,
                                       FormPool& pool,
                                       FormStack& stack,
                                       const std::string& type_name) {
  // now, let's check for a matrix initialization.
  auto matrix_entries = stack.try_getting_active_stack_entries({true, false});
  if (matrix_entries) {
    // the (set! var (new 'stack-no-clear 'matrix))
    if (matrix_entries->at(0).destination->reg() == Register(Reg::GPR, Reg::R0)) {
      return false;
    }
    auto var_name = env.get_variable_name(*matrix_entries->at(0).destination);
    auto src = matrix_entries->at(0).source->try_as_element<StackStructureDefElement>();
    if (!src) {
      return false;
    }
    if (src->type() != TypeSpec(type_name)) {
      return false;
    }

    // zeroing the rows:
    std::vector<RegisterAccess> write_vars;

    auto elt = matrix_entries->at(1).elt;

    std::vector<DerefTokenMatcher> token_matchers = {DerefTokenMatcher::string("vec"),
                                                     DerefTokenMatcher::string("quad")};
    if (type_name == "vector") {
      token_matchers = {DerefTokenMatcher::string("quad")};
    }

    auto matcher = Matcher::set(Matcher::deref(Matcher::any_reg(0), false, token_matchers),
                                Matcher::cast("uint128", Matcher::integer(0)));

    Form hack;
    hack.elts().push_back(elt);
    auto mr = match(matcher, &hack);

    if (mr.matched) {
      if (var_name != env.get_variable_name(*mr.maps.regs.at(0))) {
        return false;
      }
      write_vars.push_back(*mr.maps.regs.at(0));
    } else {
      return false;
    }

    // success!
    for (auto& wv : write_vars) {
      env.get_use_def_info(wv);
      Env* menv = const_cast<Env*>(&env);
      menv->disable_use(wv);
    }
    stack.pop(2);

    stack.push_value_to_reg(
        *matrix_entries->at(0).destination,
        pool.alloc_single_element_form<GenericElement>(
            nullptr,
            GenericOperator::make_function(pool.alloc_single_element_form<ConstantTokenElement>(
                nullptr, fmt::format("new-stack-{}0", type_name)))),
        true, TypeSpec(type_name));
    return true;
  }
  return false;
}

bool try_to_rewrite_matrix_inline_ctor(const Env& env, FormPool& pool, FormStack& stack) {
  // now, let's check for a matrix initialization.
  auto matrix_entries = stack.try_getting_active_stack_entries({true, false, false, false, false});
  if (matrix_entries) {
    // the (set! var (new 'stack-no-clear 'matrix))
    if (matrix_entries->at(0).destination->reg() == Register(Reg::GPR, Reg::R0)) {
      return false;
    }
    auto var_name = env.get_variable_name(*matrix_entries->at(0).destination);
    auto src = matrix_entries->at(0).source->try_as_element<StackStructureDefElement>();
    if (!src) {
      return false;
    }
    if (src->type() != TypeSpec("matrix")) {
      return false;
    }

    // zeroing the rows:
    std::vector<RegisterAccess> write_vars;
    for (int i = 0; i < 4; i++) {
      auto elt = matrix_entries->at(i + 1).elt;

      auto matcher = Matcher::set(
          Matcher::deref(Matcher::any_reg(0), false,
                         {DerefTokenMatcher::string("vector"), DerefTokenMatcher::integer(i),
                          DerefTokenMatcher::string("quad")}),
          Matcher::cast("uint128", Matcher::integer(0)));

      Form hack;
      hack.elts().push_back(elt);
      auto mr = match(matcher, &hack);

      if (mr.matched) {
        if (var_name != env.get_variable_name(*mr.maps.regs.at(0))) {
          return false;
        }
        write_vars.push_back(*mr.maps.regs.at(0));
      } else {
        return false;
      }
    }

    // success!
    for (auto& wv : write_vars) {
      env.get_use_def_info(wv);
      Env* menv = const_cast<Env*>(&env);
      menv->disable_use(wv);
    }
    stack.pop(5);

    stack.push_value_to_reg(*matrix_entries->at(0).destination,
                            pool.alloc_single_element_form<GenericElement>(
                                nullptr, GenericOperator::make_function(
                                             pool.alloc_single_element_form<ConstantTokenElement>(
                                                 nullptr, "new-stack-matrix0"))),
                            true, TypeSpec("matrix"));
    return true;
  }
  return false;
}

}  // namespace

void StorePlainDeref::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();

  if (m_expr.is_var()) {
    // this matches the order in Compiler::compile_set
    auto vars = std::vector<RegisterAccess>({m_expr.var(), m_base_var});
    // for 16-byte stores, the order is backward. Why????
    if (size() == 16) {
      std::swap(vars.at(0), vars.at(1));
    }
    auto popped = pop_to_forms(vars, env, pool, stack, true);
    if (size() == 16) {
      std::swap(popped.at(0), popped.at(1));
    }
    m_dst->set_base(make_optional_cast(m_dst_cast_type, popped.at(1), pool, env));
    m_dst->mark_popped();
    m_dst->inline_nested();
    auto fr = pool.alloc_element<SetFormFormElement>(
        pool.alloc_single_form(nullptr, m_dst),
        make_optional_cast(m_src_cast_type, popped.at(0), pool, env));
    // so the bitfield set check can run
    fr->mark_popped();
    fr->push_to_stack(env, pool, stack);
  } else {
    auto vars = std::vector<RegisterAccess>({m_base_var});
    auto popped = pop_to_forms(vars, env, pool, stack, true);
    m_dst->set_base(make_optional_cast(m_dst_cast_type, popped.at(0), pool, env));
    m_dst->mark_popped();
    m_dst->inline_nested();
    auto val = pool.alloc_single_element_form<SimpleExpressionElement>(nullptr, m_expr, m_my_idx);
    val->mark_popped();
    auto fr =
        pool.alloc_element<SetFormFormElement>(pool.alloc_single_form(nullptr, m_dst),
                                               make_optional_cast(m_src_cast_type, val, pool, env));
    fr->mark_popped();
    stack.push_form_element(fr, true);
  }

  if (!try_to_rewrite_matrix_inline_ctor(env, pool, stack)) {
    if (!try_to_rewrite_vector_inline_ctor(env, pool, stack, "vector")) {
      try_to_rewrite_vector_inline_ctor(env, pool, stack, "quaternion");
    }
  }
}

void StoreArrayAccess::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  Form* expr_form = nullptr;
  Form* array_form = nullptr;
  if (m_expr.is_var()) {
    auto vars = std::vector<RegisterAccess>({m_expr.var(), m_base_var});
    auto popped = pop_to_forms(vars, env, pool, stack, true);
    m_dst->mark_popped();
    expr_form = popped.at(0);
    array_form = popped.at(1);
  } else {
    auto vars = std::vector<RegisterAccess>({m_base_var});
    auto popped = pop_to_forms(vars, env, pool, stack, true);
    m_dst->mark_popped();
    expr_form = pool.alloc_single_element_form<SimpleExpressionElement>(nullptr, m_expr, m_my_idx);
    array_form = popped.at(0);
  }

  std::vector<FormElement*> forms_out;
  m_dst->update_with_val(array_form, env, pool, &forms_out, true);
  auto form_out = pool.alloc_sequence_form(nullptr, forms_out);

  auto fr = pool.alloc_element<SetFormFormElement>(
      form_out, make_optional_cast(m_src_cast_type, expr_form, pool, env));
  fr->mark_popped();
  fr->push_to_stack(env, pool, stack);
}

///////////////////
// AshElement
///////////////////

void AshElement::update_from_stack(const Env& env,
                                   FormPool& pool,
                                   FormStack& stack,
                                   std::vector<FormElement*>* result,
                                   bool allow_side_effects) {
  mark_popped();
  auto forms = pop_to_forms({value, shift_amount}, env, pool, stack, allow_side_effects, consumed);
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
                                   std::vector<FormElement*>* result,
                                   bool allow_side_effects) {
  mark_popped();
  auto forms = pop_to_forms({source}, env, pool, stack, allow_side_effects, consumed);
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
                                            std::vector<FormElement*>* result,
                                            bool allow_side_effects) {
  mark_popped();
  std::vector<Form*> args;
  auto nargs = m_op->arg_vars().size();
  args.resize(nargs, nullptr);

  std::vector<RegisterAccess> all_pop_vars = {m_op->function_var()};
  for (size_t i = 0; i < nargs; i++) {
    all_pop_vars.push_back(m_op->arg_vars().at(i));
  }

  TypeSpec function_type;
  auto& tp_type = env.get_types_before_op(all_pop_vars.at(0).idx()).get(all_pop_vars.at(0).reg());
  if (env.has_type_analysis()) {
    function_type = tp_type.typespec();
  }

  bool swap_function =
      tp_type.kind == TP_Type::Kind::NON_VIRTUAL_METHOD && all_pop_vars.size() >= 2;
  if (tp_type.kind == TP_Type::Kind::NON_VIRTUAL_METHOD) {
    // this is a hack to make some weird macro for calling res-lump methods work
    if (env.dts->ts.tc(TypeSpec("res-lump"), tp_type.method_from_type())) {
      swap_function = false;
    }
  }

  if (swap_function) {
    std::swap(all_pop_vars.at(0), all_pop_vars.at(1));
  }

  auto unstacked = pop_to_forms(all_pop_vars, env, pool, stack, allow_side_effects);
  if (swap_function) {
    std::swap(unstacked.at(0), unstacked.at(1));
    std::swap(all_pop_vars.at(0), all_pop_vars.at(1));
  }

  std::vector<Form*> arg_forms;
  bool has_good_types = env.has_type_analysis() && function_type.arg_count() == nargs + 1;
  TypeSpec first_arg_type;

  for (size_t arg_id = 0; arg_id < nargs; arg_id++) {
    auto val = unstacked.at(arg_id + 1);  // first is the function itself.
    auto& var = all_pop_vars.at(arg_id + 1);
    if (has_good_types) {
      auto actual_arg_type = env.get_types_before_op(var.idx()).get(var.reg()).typespec();
      auto val_atom = form_as_atom(val);
      if (val_atom && val_atom->is_var()) {
        actual_arg_type = env.get_variable_type(val_atom->var(), true);
      }

      if (arg_id == 0) {
        first_arg_type = actual_arg_type;
      }

      auto desired_arg_type = function_type.get_arg(arg_id);
      if (!env.dts->ts.tc(desired_arg_type, actual_arg_type)) {
        arg_forms.push_back(cast_form(val, desired_arg_type, pool, env));
      } else {
        arg_forms.push_back(val);
      }
    } else {
      arg_forms.push_back(val);
    }
  }

  FormElement* new_form = nullptr;

  {
    // deal with virtual method calls.
    auto matcher = Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::METHOD_OF_OBJECT),
                               {Matcher::any_reg(0), Matcher::any(1)});
    auto mr = match(matcher, unstacked.at(0));
    if (mr.matched && nargs >= 1) {
      auto vtable_reg = mr.maps.regs.at(0);
      assert(vtable_reg);
      auto vtable_var_name = env.get_variable_name(*vtable_reg);
      auto arg0_mr = match(Matcher::any_reg(0), unstacked.at(1));
      if (arg0_mr.matched && env.get_variable_name(*arg0_mr.maps.regs.at(0)) == vtable_var_name) {
        if (tp_type.kind != TP_Type::Kind::VIRTUAL_METHOD) {
          throw std::runtime_error(
              "Method internal mismatch. METHOD_OF_OBJECT operator didn't get a VIRTUAL_METHOD "
              "type.");
        }

        if (!env.dts->ts.should_use_virtual_methods(tp_type.method_from_type(),
                                                    tp_type.method_id())) {
          throw std::runtime_error(
              fmt::format("Method call on {} id {} used a virtual call unexpectedly.",
                          tp_type.method_from_type().print(), tp_type.method_id()));
        }
        // fmt::print("STACK\n{}\n\n", stack.print(env));
        auto pop =
            pop_to_forms({*arg0_mr.maps.regs.at(0)}, env, pool, stack, allow_side_effects, {}, {2})
                .at(0);
        // fmt::print("GOT: {}\n", pop->to_string(env));
        arg_forms.at(0) = pop;

        new_form = pool.alloc_element<GenericElement>(
            GenericOperator::make_function(mr.maps.forms.at(1)), arg_forms);
        result->push_back(new_form);
        return;
      }
    }
  }

  new_form = pool.alloc_element<GenericElement>(GenericOperator::make_function(unstacked.at(0)),
                                                arg_forms);

  {
    // detect method calls:
    // ex: ((method-of-type pair new) (quote global) pair gp-0 a3-0)
    constexpr int type_for_method = 0;
    constexpr int method_name = 1;

    auto deref_matcher = Matcher::op(
        GenericOpMatcher::fixed(FixedOperatorKind::METHOD_OF_TYPE),
        {Matcher::any_symbol(type_for_method), Matcher::any_constant_token(method_name)});

    auto matcher = Matcher::op_with_rest(GenericOpMatcher::func(deref_matcher), {});
    auto temp_form = pool.alloc_single_form(nullptr, new_form);
    auto match_result = match(matcher, temp_form);
    if (match_result.matched) {
      auto type_1 = match_result.maps.strings.at(type_for_method);
      auto name = match_result.maps.strings.at(method_name);

      if (name == "new" && type_1 == "object") {
        // calling the new method of object. This is a special case that turns into an (object-new
        // macro. The arguments are allocation type-to-make and size of type
        // symbol, type, int.
        std::vector<Form*> new_args = dynamic_cast<GenericElement*>(new_form)->elts();

        // if needed, cast to to correct type.
        std::vector<TypeSpec> expected_arg_types = {TypeSpec("symbol"), TypeSpec("type"),
                                                    TypeSpec("int")};
        assert(new_args.size() >= 3);
        for (size_t i = 0; i < 3; i++) {
          auto& var = all_pop_vars.at(i + 1);  // 0 is the function itself.
          auto arg_type = env.get_types_before_op(var.idx()).get(var.reg()).typespec();
          if (!env.dts->ts.tc(expected_arg_types.at(i), arg_type)) {
            new_args.at(i) = pool.alloc_single_element_form<CastElement>(
                nullptr, expected_arg_types.at(i), new_args.at(i));
          }
        }

        auto new_op = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::OBJECT_NEW), new_args);
        result->push_back(new_op);
        return;
      }
      if (name == "new" && type_1 == "type") {
        std::vector<Form*> new_args = dynamic_cast<GenericElement*>(new_form)->elts();
        auto new_op = pool.alloc_element<GenericElement>(
            GenericOperator::make_fixed(FixedOperatorKind::TYPE_NEW), new_args);
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
          if (alloc != "global" && alloc != "debug" && alloc != "process") {
            throw std::runtime_error("Unrecognized heap symbol for new: " + alloc);
          }
          auto type_2 = match_result.maps.strings.at(type_for_arg);
          if (type_1 != type_2) {
            throw std::runtime_error(
                fmt::format("Inconsistent types in method call: {} and {}", type_1, type_2));
          }

          if (type_2 == "array") {
            type_2 = "boxed-array";
          }

          auto quoted_type = pool.alloc_single_element_form<SimpleAtomElement>(
              nullptr, SimpleAtom::make_sym_ptr(type_2));

          if (alloc == "global" && type_1 == "pair") {
            // cons!
            // (new 'global 'pair a b) -> (cons a b)
            std::vector<Form*> cons_args = {dynamic_cast<GenericElement*>(new_form)->elts().at(2),
                                            dynamic_cast<GenericElement*>(new_form)->elts().at(3)};
            auto cons_op = pool.alloc_element<GenericElement>(
                GenericOperator::make_fixed(FixedOperatorKind::CONS), cons_args);
            result->push_back(cons_op);
            return;
          } else {
            // just normal construction on the heap
            std::vector<Form*> new_args = dynamic_cast<GenericElement*>(new_form)->elts();
            new_args.at(1) = quoted_type;

            auto new_op = pool.alloc_element<GenericElement>(
                GenericOperator::make_fixed(FixedOperatorKind::NEW), new_args);
            result->push_back(new_op);
            return;
          }
        }
        // possible else case here to catch fixed-type new's in a nicer way
      }
    }
  }

  {
    // detect method calls:
    // ex: ((method-of-type x blah) arg...)
    constexpr int method_name = 0;
    constexpr int type_source = 1;

    auto deref_matcher =
        Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::METHOD_OF_TYPE),
                    {Matcher::any(type_source), Matcher::any_constant_token(method_name)});

    auto matcher = Matcher::op_with_rest(GenericOpMatcher::func(deref_matcher), {});
    auto temp_form = pool.alloc_single_form(nullptr, new_form);
    auto match_result = match(matcher, temp_form);
    if (match_result.matched) {
      auto name = match_result.maps.strings.at(method_name);
      if (name != "new") {
        // only do these checks on non-new methods.  New methods are treated as functions because
        // they are never virtual and are never called like a method.
        if (tp_type.kind != TP_Type::Kind::NON_VIRTUAL_METHOD) {
          throw std::runtime_error(fmt::format(
              "Method internal mismatch. METHOD_OF_TYPE operator didn't get a NON_VIRTUAL_METHOD "
              "type. Got {} instead.",
              tp_type.print()));
        }
      }

      auto type_source_form = match_result.maps.forms.at(type_source);

      // if the type is the exact type of the argument, we want to build it into a method call
      if (type_source_form->to_string(env) == first_arg_type.base_type() && name != "new") {
        if (env.dts->ts.should_use_virtual_methods(tp_type.method_from_type(),
                                                   tp_type.method_id())) {
          throw std::runtime_error(fmt::format(
              "Expected type {} method id {} to use virtual methods, but it didn't.  Set option "
              ":final in the deftype to disable virtual method calls",
              tp_type.method_from_type().print(), tp_type.method_id()));
        }
        auto method_op = pool.alloc_single_element_form<ConstantTokenElement>(nullptr, name);
        auto gop = GenericOperator::make_function(method_op);

        result->push_back(pool.alloc_element<GenericElement>(gop, arg_forms));
        return;
      }

      if (name == "new" && arg_forms.size() >= 2) {
        bool got_stack_new = true;
        // method
        // (the-as symbol (new 'stack-no-clear 'draw-context))
        //  draw-context
        auto first_cast = arg_forms.at(0)->try_as_element<CastElement>();
        if (!first_cast || first_cast->type() != TypeSpec("symbol")) {
          got_stack_new = false;
        }

        if (got_stack_new) {
          auto new_op = first_cast->source()->try_as_element<StackStructureDefElement>();
          if (!new_op || new_op->type().base_type() != type_source_form->to_string(env)) {
            got_stack_new = false;
          }
        }

        if (got_stack_new) {
          if (arg_forms.at(1)->to_string(env) != type_source_form->to_string(env)) {
            got_stack_new = false;
          }
        }

        if (got_stack_new) {
          std::vector<Form*> stack_new_args;
          stack_new_args.push_back(
              pool.alloc_single_element_form<ConstantTokenElement>(nullptr, "'stack"));
          stack_new_args.push_back(pool.alloc_single_element_form<ConstantTokenElement>(
              nullptr, fmt::format("'{}", type_source_form->to_string(env))));
          for (size_t i = 2; i < arg_forms.size(); i++) {
            stack_new_args.push_back(arg_forms.at(i));
          }
          result->push_back(pool.alloc_element<GenericElement>(
              GenericOperator::make_fixed(FixedOperatorKind::NEW), stack_new_args));
          return;
        }
      }

      auto method_op =
          pool.alloc_single_element_form<GetMethodElement>(nullptr, type_source_form, name, false);
      auto gop = GenericOperator::make_function(method_op);

      result->push_back(pool.alloc_element<GenericElement>(gop, arg_forms));
      return;
    }
  }

  result->push_back(new_form);
}

void FunctionCallElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  std::vector<FormElement*> rewritten;
  update_from_stack(env, pool, stack, &rewritten, true);
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
                                     std::vector<FormElement*>* result,
                                     bool allow_side_effects) {
  mark_popped();
  // todo - update var tokens from stack?
  m_base->update_children_from_stack(env, pool, stack, allow_side_effects);

  // look for sym->str-ptr
  auto sym_str = m_base->try_as_element<GetSymbolStringPointer>();
  if (sym_str && m_tokens.size() == 1 && m_tokens.at(0).is_int(0)) {
    result->push_back(pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::SYMBOL_TO_STRING), sym_str->src()));
    return;
  }

  // merge nested ->'s
  inline_nested();

  result->push_back(this);
}

void DerefElement::inline_nested() {
  auto as_deref = dynamic_cast<DerefElement*>(m_base->try_as_single_element());
  if (as_deref) {
    if (!m_is_addr_of && !as_deref->is_addr_of()) {
      m_tokens.insert(m_tokens.begin(), as_deref->tokens().begin(), as_deref->tokens().end());
      m_base = as_deref->m_base;
    }
  }
}

///////////////////
// UntilElement
///////////////////

void UntilElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  for (auto form : {condition, body}) {
    FormStack temp_stack(false);
    for (auto& entry : form->elts()) {
      entry->push_to_stack(env, pool, temp_stack);
    }
    auto new_entries = temp_stack.rewrite(pool, env);
    form->clear();
    for (auto e : new_entries) {
      form->push_back(e);
    }
  }

  stack.push_form_element(this, true);
}

void WhileElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  bool first = true;
  for (auto form : {body, condition}) {
    FormStack temp_stack(first && stack.is_root());
    first = false;
    for (auto& entry : form->elts()) {
      entry->push_to_stack(env, pool, temp_stack);
    }
    auto new_entries = temp_stack.rewrite(pool, env);
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
  mark_popped();
  if (already_rewritten) {
    stack.push_form_element(this, true);
    return;
  }

  // the first condition is special
  auto first_condition = entries.front().condition;
  // lets evaluate in on the parent stack...
  for (auto x : first_condition->elts()) {
    x->push_to_stack(env, pool, stack);
  }

  RegisterAccess write_as_value = final_destination;
  bool first = true;
  for (auto& entry : entries) {
    for (auto form : {entry.condition, entry.body}) {
      if (form == first_condition) {
        form->clear();
        form->push_back(stack.pop_back(pool));
      } else {
        FormStack temp_stack(first && stack.is_root());
        first = false;
        for (auto& elt : form->elts()) {
          elt->push_to_stack(env, pool, temp_stack);
        }

        std::vector<FormElement*> new_entries;

        if (form == entry.body && used_as_value) {
          // try to advance us to the real write so we don't use the final_destination,
          // which may contain the wrong variable, but right register.
          std::optional<RegisterAccess> written_var;
          new_entries = rewrite_to_get_var(temp_stack, pool, final_destination, env, &written_var);
          if (written_var) {
            write_as_value = *written_var;
          }
        } else {
          new_entries = temp_stack.rewrite(pool, env);
        }

        form->clear();
        for (auto e : new_entries) {
          form->push_back(e);
        }
      }
    }
  }

  if (used_as_value) {
    // TODO - is this wrong?
    stack.push_value_to_reg(write_as_value, pool.alloc_single_form(nullptr, this), true,
                            env.get_variable_type(final_destination, false));
  } else {
    stack.push_form_element(this, true);
  }
  already_rewritten = true;
}

void CondWithElseElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  if (already_rewritten) {
    stack.push_form_element(this, true);
    return;
  }
  // first, let's try to detect if all bodies write the same value
  std::optional<RegisterAccess> last_var;
  bool rewrite_as_set = true;

  // the first condition is special
  auto first_condition = entries.front().condition;
  // lets evaluate in on the parent stack...
  for (auto x : first_condition->elts()) {
    x->push_to_stack(env, pool, stack);
  }

  // process conditions and bodies
  for (auto& entry : entries) {
    for (auto form : {entry.condition, entry.body}) {
      if (form == first_condition) {
        form->clear();
        form->push_back(stack.pop_back(pool));
      } else {
        FormStack temp_stack(false);
        if (form == entry.body) {
          auto as_setvar = dynamic_cast<SetVarElement*>(form->elts().back());
          if (as_setvar && as_setvar->is_dead_set() && as_setvar->src_type() != TypeSpec("float")) {
            rewrite_as_set = false;
          }
        }
        for (auto& elt : form->elts()) {
          elt->push_to_stack(env, pool, temp_stack);
        }

        std::vector<FormElement*> new_entries;
        new_entries = temp_stack.rewrite(pool, env);

        form->clear();
        for (auto e : new_entries) {
          form->push_back(e);
        }
      }
    }
  }

  // process else.
  FormStack temp_stack(false);
  for (auto& elt : else_ir->elts()) {
    elt->push_to_stack(env, pool, temp_stack);
  }

  std::vector<FormElement*> new_entries;
  new_entries = temp_stack.rewrite(pool, env);

  else_ir->clear();
  for (auto e : new_entries) {
    else_ir->push_back(e);
  }

  // collect all forms which should write the output.
  std::vector<Form*> write_output_forms;
  for (const auto& entry : entries) {
    write_output_forms.push_back(entry.body);
  }
  write_output_forms.push_back(else_ir);

  // check all to see if they write the value.
  std::vector<SetVarElement*> dest_sets;
  std::vector<TypeSpec> source_types;  // only explicit accesses that aren't move-eliminated

  int empty_count = 0;
  for (auto form : write_output_forms) {
    auto last_in_body = dynamic_cast<SetVarElement*>(form->elts().back());
    if (last_in_body) {
      dest_sets.push_back(last_in_body);
      if (last_var.has_value()) {
        if (last_var->reg() != last_in_body->dst().reg()) {
          rewrite_as_set = false;
          break;
        }
        source_types.push_back(last_in_body->src_type());
      }
      last_var = last_in_body->dst();
    }
    empty_count++;
  }

  if (empty_count > 0 && env.aggressively_reject_cond_to_value_rewrite) {
    rewrite_as_set = false;
  }

  if (!last_var.has_value()) {
    rewrite_as_set = false;
  }

  // determine if set destination is used
  bool set_unused = false;
  if (rewrite_as_set) {
    auto& info = env.reg_use().op.at(last_var->idx());
    if (info.written_and_unused.find(last_var->reg()) != info.written_and_unused.end()) {
      set_unused = true;
    }
  }

  // rewrite extra sets as needed.
  if (rewrite_as_set && !set_unused) {
    for (auto& entry : entries) {
      rewrite_to_get_var(entry.body->elts(), pool, *last_var, env);
      entry.body->claim_all_children();
    }
    rewrite_to_get_var(else_ir->elts(), pool, *last_var, env);
    else_ir->claim_all_children();
  }

  // update register info
  if (rewrite_as_set && !set_unused) {
    // might not be the same if a set is eliminated by a coloring move.
    // assert(dest_sets.size() == write_output_forms.size());
    if (!dest_sets.empty()) {
      for (size_t i = 0; i < dest_sets.size() - 1; i++) {
        auto var = dest_sets.at(i)->dst();
        auto* env2 = const_cast<Env*>(&env);
        env2->disable_def(var, env2->func->warnings);
      }
    }
  }

  if (rewrite_as_set) {
    if (set_unused) {
      stack.push_form_element(this, true);
    } else {
      // We may need to insert a cast here.

      // Note:
      // I think this might skip a cast if you have something like
      // (set! x (if y z (expr))) and z requires a cast, but the move from z to x is
      // eliminated by GOAL's register allocator.

      //      fmt::print("checking:\n");
      //      for (auto& t : source_types) {
      //        fmt::print("  {}\n", t.print());
      //      }

      auto expected_type = env.get_variable_type(*last_var, true);
      // fmt::print("The expected type is {}\n", expected_type.print());
      auto result_type =
          source_types.empty() ? expected_type : env.dts->ts.lowest_common_ancestor(source_types);
      // fmt::print("but we actually got {}\n", result_type.print());

      Form* result_value = pool.alloc_single_form(nullptr, this);
      if (!env.dts->ts.tc(expected_type, result_type)) {
        result_value =
            pool.alloc_single_element_form<CastElement>(nullptr, expected_type, result_value);
      }

      stack.push_value_to_reg(*last_var, result_value, true,
                              env.get_variable_type(*last_var, false));
    }
  } else {
    stack.push_form_element(this, true);
  }
  already_rewritten = true;
}

///////////////////
// ShortCircuitElement
///////////////////

FormElement* sc_to_handle_get_proc(ShortCircuitElement* elt,
                                   const Env& env,
                                   FormPool& pool,
                                   FormStack& stack) {
  if (elt->kind != ShortCircuitElement::AND) {
    return nullptr;
  }

  if (elt->entries.size() != 2) {
    return nullptr;
  }

  // fmt::print("candidate: {}\n", elt->to_string(env));

  constexpr int reg_input_1 = 0;
  constexpr int reg_input_2 = 1;
  constexpr int reg_input_3 = 2;
  constexpr int reg_temp_1 = 10;
  constexpr int reg_temp_2 = 11;
  constexpr int reg_temp_3 = 12;

  // check first.
  auto first_matcher =
      Matcher::op(GenericOpMatcher::condition(IR2_Condition::Kind::NONZERO),
                  {Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::L32_NOT_FALSE_CBOOL),
                               {Matcher::any_reg(reg_input_1)})});

  auto first_result = match(first_matcher, elt->entries.at(0).condition);
  if (!first_result.matched) {
    return nullptr;
  }

  // auto first_use_of_in = *first_result.maps.regs.at(reg_input_1);
  // fmt::print("reg1: {}\n", first_use_of_in.to_string(env));

  auto setup_matcher = Matcher::set_var(
      Matcher::deref(Matcher::any_reg(reg_input_2), false,
                     {DerefTokenMatcher::string("process"), DerefTokenMatcher::integer(0)}),
      reg_temp_1);

  auto if_matcher = Matcher::if_no_else(
      Matcher::op(
          GenericOpMatcher::fixed(FixedOperatorKind::EQ),
          {Matcher::deref(Matcher::any_reg(reg_input_3), false, {DerefTokenMatcher::string("pid")}),
           Matcher::deref(Matcher::any_reg(reg_temp_2), false,
                          {DerefTokenMatcher::string("pid")})}),
      Matcher::any_reg(reg_temp_3));

  auto second_matcher = Matcher::begin({setup_matcher, if_matcher});

  auto second_result = match(second_matcher, elt->entries.at(1).condition);
  if (!second_result.matched) {
    return nullptr;
  }

  auto in1 = *first_result.maps.regs.at(reg_input_1);
  auto in2 = *second_result.maps.regs.at(reg_input_2);
  auto in3 = *second_result.maps.regs.at(reg_input_3);

  auto in_name = in1.to_string(env);
  if (in_name != in2.to_string(env)) {
    return nullptr;
  }

  if (in_name != in3.to_string(env)) {
    return nullptr;
  }

  auto temp_name = second_result.maps.regs.at(reg_temp_1)->to_string(env);
  if (temp_name != second_result.maps.regs.at(reg_temp_2)->to_string(env)) {
    return nullptr;
  }

  if (temp_name != second_result.maps.regs.at(reg_temp_3)->to_string(env)) {
    return nullptr;
  }

  const auto& temp_use_def = env.get_use_def_info(*second_result.maps.regs.at(reg_temp_1));
  if (temp_use_def.use_count() != 2 || temp_use_def.def_count() != 1) {
    return nullptr;
  }

  // modify use def:
  auto* menv = const_cast<Env*>(&env);
  menv->disable_use(in2);
  menv->disable_use(in3);

  auto repopped = stack.pop_reg(in1, {}, env, true);
  // fmt::print("repopped: {}\n", repopped->to_string(env));

  if (!repopped) {
    repopped = var_to_form(in1, pool);
  }

  return pool.alloc_element<GenericElement>(
      GenericOperator::make_function(
          pool.alloc_single_element_form<ConstantTokenElement>(nullptr, "handle->process")),
      repopped);

  return nullptr;
}

void ShortCircuitElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  if (!used_as_value.value_or(false)) {
    throw std::runtime_error(
        "ShortCircuitElement::push_to_stack not implemented for result not used case.");

    stack.push_form_element(this, true);
  } else {
    if (already_rewritten) {
      stack.push_form_element(this, true);
      return;
    }

    // the first condition is special
    auto first_condition = entries.front().condition;
    // lets evaluate in on the parent stack...
    for (auto x : first_condition->elts()) {
      x->push_to_stack(env, pool, stack);
    }

    for (int i = 0; i < int(entries.size()); i++) {
      auto& entry = entries.at(i);
      if (entry.condition == first_condition) {
        entry.condition->clear();
        entry.condition->push_back(stack.pop_back(pool));
      } else {
        FormStack temp_stack(false);
        for (auto& elt : entry.condition->elts()) {
          elt->push_to_stack(env, pool, temp_stack);
        }

        std::vector<FormElement*> new_entries;
        if (i == int(entries.size()) - 1) {
          new_entries = rewrite_to_get_var(temp_stack, pool, final_result, env);
        } else {
          new_entries = temp_stack.rewrite(pool, env);
        }

        entry.condition->clear();
        for (auto e : new_entries) {
          entry.condition->push_back(e);
        }
      }
    }

    FormElement* to_push = this;
    auto as_handle_get = sc_to_handle_get_proc(this, env, pool, stack);
    if (as_handle_get) {
      to_push = as_handle_get;
    }

    assert(used_as_value.has_value());
    stack.push_value_to_reg(final_result, pool.alloc_single_form(nullptr, to_push), true,
                            env.get_variable_type(final_result, false));
    already_rewritten = true;
  }
}

void ShortCircuitElement::update_from_stack(const Env& env,
                                            FormPool& pool,
                                            FormStack& stack,
                                            std::vector<FormElement*>* result,
                                            bool) {
  mark_popped();
  (void)stack;
  if (already_rewritten) {
    result->push_back(this);
    return;
  }
  for (int i = 0; i < int(entries.size()); i++) {
    auto& entry = entries.at(i);
    FormStack temp_stack(false);
    for (auto& elt : entry.condition->elts()) {
      elt->push_to_stack(env, pool, temp_stack);
    }

    std::vector<FormElement*> new_entries;
    if (i == int(entries.size()) - 1) {
      new_entries = rewrite_to_get_var(temp_stack, pool, final_result, env);
    } else {
      new_entries = temp_stack.rewrite(pool, env);
    }

    entry.condition->clear();
    for (auto e : new_entries) {
      entry.condition->push_back(e);
    }
  }
  result->push_back(this);
  already_rewritten = true;
}

namespace {
Matcher make_int_uint_cast_matcher(const Matcher& thing) {
  return Matcher::match_or({Matcher::cast("uint", thing), Matcher::cast("int", thing), thing});
}
}  // namespace

///////////////////
// ConditionElement
///////////////////

namespace {
/*!
 * Try to make a pretty looking constant out of value for comparing to something of type.
 * If we can't do anything nice, return nullptr.
 */
Form* try_make_constant_for_compare(Form* value,
                                    const TypeSpec& type,
                                    FormPool& pool,
                                    const Env& env) {
  if (get_goal_integer_constant(value, env) && env.dts->ts.try_enum_lookup(type)) {
    return cast_form(value, type, pool, env);
  }
  return nullptr;
}

Form* try_make_constant_from_int_for_compare(s64 value,
                                             const TypeSpec& type,
                                             FormPool& pool,
                                             const Env& env) {
  auto enum_type_info = env.dts->ts.try_enum_lookup(type);
  if (enum_type_info) {
    if (enum_type_info->is_bitfield()) {
      if (value != 0) {
        // prefer (zero? x) for bitfield enums.
        return cast_to_bitfield_enum(enum_type_info, pool, env, value);
      }
    } else {
      return cast_to_int_enum(enum_type_info, pool, env, value);
    }
  }
  return nullptr;
}
}  // namespace

FormElement* ConditionElement::make_zero_check_generic(const Env& env,
                                                       FormPool& pool,
                                                       const std::vector<Form*>& source_forms,
                                                       const std::vector<TypeSpec>& source_types) {
  // (zero? (+ thing small-integer)) -> (= thing (- small-integer))
  assert(source_forms.size() == 1);
  auto mr = match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                              {Matcher::any(0), Matcher::any_integer(1)}),
                  source_forms.at(0));
  if (mr.matched) {
    s64 value = -mr.maps.ints.at(1);
    auto value_form = pool.alloc_single_element_form<SimpleAtomElement>(
        nullptr, SimpleAtom::make_int_constant(value));
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::EQ),
                                              std::vector<Form*>{mr.maps.forms.at(0), value_form});
  }

  auto enum_type_info = env.dts->ts.try_enum_lookup(source_types.at(0));
  if (enum_type_info && !enum_type_info->is_bitfield()) {
    // (zero? (+ (the-as uint arg0) (the-as uint -2))) check enum value
    mr = match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                           {make_int_uint_cast_matcher(Matcher::any(0)),
                            make_int_uint_cast_matcher(Matcher::any_integer(1))}),
               source_forms.at(0));
    if (mr.matched) {
      s64 value = mr.maps.ints.at(1);
      value = -value;
      auto enum_constant = cast_to_int_enum(enum_type_info, pool, env, value);
      return pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::EQ),
          std::vector<Form*>{mr.maps.forms.at(0), enum_constant});
    }
  }

  auto nice_constant = try_make_constant_from_int_for_compare(0, source_types.at(0), pool, env);
  if (nice_constant) {
    return pool.alloc_element<GenericElement>(
        GenericOperator::make_fixed(FixedOperatorKind::EQ),
        std::vector<Form*>{source_forms.at(0), nice_constant});
  }

  return pool.alloc_element<GenericElement>(GenericOperator::make_compare(m_kind), source_forms);
}

FormElement* ConditionElement::make_nonzero_check_generic(const Env& env,
                                                          FormPool& pool,
                                                          const std::vector<Form*>& source_forms,
                                                          const std::vector<TypeSpec>&) {
  // for (nonzero? (-> obj bitfield))
  FormElement* bitfield_compare = nullptr;
  assert(source_forms.size() == 1);
  auto as_bitfield_op =
      dynamic_cast<BitfieldAccessElement*>(source_forms.at(0)->try_as_single_element());
  if (as_bitfield_op) {
    bitfield_compare = as_bitfield_op->push_step(
        BitfieldManip(BitfieldManip::Kind::NONZERO_COMPARE, 0), env.dts->ts, pool, env);
  }

  if (bitfield_compare) {
    return bitfield_compare;
  }

  auto mr = match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                              {Matcher::any(0), Matcher::any_integer(1)}),
                  source_forms.at(0));
  if (mr.matched) {
    s64 value = -mr.maps.ints.at(1);
    auto value_form = pool.alloc_single_element_form<SimpleAtomElement>(
        nullptr, SimpleAtom::make_int_constant(value));
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::NEQ),
                                              std::vector<Form*>{mr.maps.forms.at(0), value_form});
  }

  return pool.alloc_element<GenericElement>(GenericOperator::make_compare(m_kind), source_forms);
}

FormElement* ConditionElement::make_equal_check_generic(const Env& env,
                                                        FormPool& pool,
                                                        const std::vector<Form*>& source_forms,
                                                        const std::vector<TypeSpec>& source_types) {
  assert(source_forms.size() == 2);
  // (= thing '())
  auto ref = source_forms.at(1);
  auto ref_atom = form_as_atom(ref);
  if (ref_atom && ref_atom->get_kind() == SimpleAtom::Kind::EMPTY_LIST) {
    // null?
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::NULLP),
                                              source_forms.at(0));
  } else {
    auto nice_constant =
        try_make_constant_for_compare(source_forms.at(1), source_types.at(0), pool, env);
    if (nice_constant) {
      auto forms_with_cast = source_forms;
      forms_with_cast.at(1) = nice_constant;
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::EQ),
                                                forms_with_cast);
    } else {
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::EQ),
                                                source_forms);
    }
  }
}

FormElement* ConditionElement::make_not_equal_check_generic(const Env&,
                                                            FormPool& pool,
                                                            const std::vector<Form*>& source_forms,
                                                            const std::vector<TypeSpec>&) {
  assert(source_forms.size() == 2);
  // (!= thing '())
  auto ref = source_forms.at(1);
  auto ref_atom = form_as_atom(ref);
  if (ref_atom && ref_atom->get_kind() == SimpleAtom::Kind::EMPTY_LIST) {
    // null?
    return pool.alloc_element<GenericElement>(
        GenericOperator::make_compare(IR2_Condition::Kind::FALSE),
        pool.alloc_single_element_form<GenericElement>(
            nullptr, GenericOperator::make_fixed(FixedOperatorKind::NULLP), source_forms.at(0)));
  } else {
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::NEQ),
                                              source_forms);
  }
}

FormElement* ConditionElement::make_less_than_zero_signed_check_generic(
    const Env& env,
    FormPool& pool,
    const std::vector<Form*>& source_forms,
    const std::vector<TypeSpec>& types) {
  assert(source_forms.size() == 1);
  // (< (shl (the-as int iter) 62) 0) -> (pair? iter)

  // match (shl [(the-as int [x]) | [x]] 62)
  auto shift_match =
      match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::SHL),
                        {
                            Matcher::match_or({Matcher::cast("int", Matcher::any(0)),
                                               Matcher::any(0)}),  // the val
                            Matcher::integer(62)  // get the bit in the highest position.
                        }),
            source_forms.at(0));

  if (shift_match.matched) {
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::PAIRP),
                                              shift_match.maps.forms.at(0));
  } else {
    auto casted = make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env);
    auto zero = pool.alloc_single_element_form<SimpleAtomElement>(nullptr,
                                                                  SimpleAtom::make_int_constant(0));
    casted.push_back(zero);
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::LT),
                                              casted);
  }
}

FormElement* ConditionElement::make_geq_zero_signed_check_generic(
    const Env& env,
    FormPool& pool,
    const std::vector<Form*>& source_forms,
    const std::vector<TypeSpec>& types) {
  assert(source_forms.size() == 1);
  // (>= (shl (the-as int iter) 62) 0) -> (not (pair? iter))

  // match (shl [(the-as int [x]) | [x]] 62)
  auto shift_match =
      match(Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::SHL),
                        {
                            Matcher::match_or({Matcher::cast("int", Matcher::any(0)),
                                               Matcher::any(0)}),  // the val
                            Matcher::integer(62)  // get the bit in the highest position.
                        }),
            source_forms.at(0));

  if (shift_match.matched) {
    return pool.alloc_element<GenericElement>(
        GenericOperator::make_compare(IR2_Condition::Kind::FALSE),
        pool.alloc_single_element_form<GenericElement>(
            nullptr, GenericOperator::make_fixed(FixedOperatorKind::PAIRP),
            shift_match.maps.forms.at(0)));
  } else {
    auto casted = make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env);
    auto zero = pool.alloc_single_element_form<SimpleAtomElement>(nullptr,
                                                                  SimpleAtom::make_int_constant(0));
    casted.push_back(zero);
    return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::GEQ),
                                              casted);
  }
}

FormElement* ConditionElement::make_generic(const Env& env,
                                            FormPool& pool,
                                            const std::vector<Form*>& source_forms,
                                            const std::vector<TypeSpec>& types) {
  switch (m_kind) {
    case IR2_Condition::Kind::ZERO:
      return make_zero_check_generic(env, pool, source_forms, types);
    case IR2_Condition::Kind::NONZERO:
      return make_nonzero_check_generic(env, pool, source_forms, types);
    case IR2_Condition::Kind::TRUTHY:
    case IR2_Condition::Kind::FALSE:
    case IR2_Condition::Kind::IS_PAIR:
    case IR2_Condition::Kind::IS_NOT_PAIR:
    case IR2_Condition::Kind::ALWAYS:
      // kind of a hack, we fall back to the old condition operator which is special cased
      // to print the truthy condition in a nice way. and we use it for other things that don't
      // require fancy renaming.
      return pool.alloc_element<GenericElement>(GenericOperator::make_compare(m_kind),
                                                source_forms);
    case IR2_Condition::Kind::EQUAL:
      return make_equal_check_generic(env, pool, source_forms, types);

    case IR2_Condition::Kind::NOT_EQUAL:
      return make_not_equal_check_generic(env, pool, source_forms, types);

    case IR2_Condition::Kind::LESS_THAN_SIGNED:
      return pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::LT),
          make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env));
    case IR2_Condition::Kind::LESS_THAN_UNSIGNED:
      return pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::LT),
          make_casts_if_needed(source_forms, types, TypeSpec("uint"), pool, env));

    case IR2_Condition::Kind::GEQ_UNSIGNED:
      return pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::GEQ),
          make_casts_if_needed(source_forms, types, TypeSpec("uint"), pool, env));

    case IR2_Condition::Kind::GEQ_SIGNED:
      return pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::GEQ),
          make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env));

    case IR2_Condition::Kind::LESS_THAN_ZERO_SIGNED: {
      return make_less_than_zero_signed_check_generic(env, pool, source_forms, types);
    }

    case IR2_Condition::Kind::LESS_THAN_ZERO_UNSIGNED: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("uint"), pool, env);
      auto zero = pool.alloc_single_element_form<SimpleAtomElement>(
          nullptr, SimpleAtom::make_int_constant(0));
      casted.push_back(zero);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::LT),
                                                casted);
    }

    case IR2_Condition::Kind::LEQ_ZERO_SIGNED: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env);
      auto zero = pool.alloc_single_element_form<SimpleAtomElement>(
          nullptr, SimpleAtom::make_int_constant(0));
      casted.push_back(zero);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::LEQ),
                                                casted);
    }

    case IR2_Condition::Kind::GEQ_ZERO_SIGNED: {
      return make_geq_zero_signed_check_generic(env, pool, source_forms, types);
    }

    case IR2_Condition::Kind::GREATER_THAN_ZERO_UNSIGNED: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("uint"), pool, env);
      auto zero = pool.alloc_single_element_form<SimpleAtomElement>(
          nullptr, SimpleAtom::make_int_constant(0));
      casted.push_back(zero);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::GT),
                                                casted);
    }

    case IR2_Condition::Kind::GREATER_THAN_ZERO_SIGNED: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("int"), pool, env);
      auto zero = pool.alloc_single_element_form<SimpleAtomElement>(
          nullptr, SimpleAtom::make_int_constant(0));
      casted.push_back(zero);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::GT),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_NOT_EQUAL: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::NEQ),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_EQUAL: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::EQ),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_LEQ: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::LEQ),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_LESS_THAN: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::LT),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_GEQ: {
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::GEQ),
                                                casted);
    }

    case IR2_Condition::Kind::FLOAT_GREATER_THAN: {
      // never emitted by normal branch conditions
      auto casted = make_casts_if_needed(source_forms, types, TypeSpec("float"), pool, env);
      return pool.alloc_element<GenericElement>(GenericOperator::make_fixed(FixedOperatorKind::GT),
                                                casted);
    }

    default:
      throw std::runtime_error("ConditionElement::make_generic NYI for kind " +
                               get_condition_kind_name(m_kind));
  }
}

void ConditionElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  std::vector<Form*> source_forms, popped_forms;
  std::vector<TypeSpec> source_types;
  std::vector<RegisterAccess> vars;

  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    if (m_src[i]->is_var()) {
      auto& var = m_src[i]->var();
      vars.push_back(var);
      source_types.push_back(env.get_types_before_op(var.idx()).get(var.reg()).typespec());
    } else if (m_src[i]->is_int()) {
      if (m_src[i]->get_int() == 0 && condition_uses_float(m_kind)) {
        // if we're doing a floating point comparison, and one of our arguments is a constant
        // which is an "integer zero", treat it as a floating point zero.
        source_types.push_back(TypeSpec("float"));
      } else {
        source_types.push_back(TypeSpec("int"));
      }
    } else if (m_src[i]->is_sym_val() && m_src[i]->get_str() == "#f") {
      source_types.push_back(TypeSpec("symbol"));
    } else {
      throw std::runtime_error(fmt::format(
          "Unsupported atom in ConditionElement::push_to_stack: {}", m_src[i]->to_string(env)));
    }
  }
  if (m_flipped) {
    std::reverse(vars.begin(), vars.end());
  }

  popped_forms = pop_to_forms(vars, env, pool, stack, true, m_consumed);
  if (m_flipped) {
    std::reverse(popped_forms.begin(), popped_forms.end());
  }

  int popped_counter = 0;
  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    if (m_src[i]->is_var()) {
      source_forms.push_back(popped_forms.at(popped_counter++));
    } else {
      source_forms.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, *m_src[i]));
    }
  }
  assert(popped_counter == int(popped_forms.size()));
  assert(source_forms.size() == source_types.size());

  stack.push_form_element(make_generic(env, pool, source_forms, source_types), true);
}

void ConditionElement::update_from_stack(const Env& env,
                                         FormPool& pool,
                                         FormStack& stack,
                                         std::vector<FormElement*>* result,
                                         bool allow_side_effects) {
  mark_popped();
  std::vector<Form*> source_forms, popped_forms;
  std::vector<TypeSpec> source_types;
  std::vector<RegisterAccess> vars;

  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    if (m_src[i]->is_var()) {
      auto& var = m_src[i]->var();
      vars.push_back(var);
      source_types.push_back(env.get_types_before_op(var.idx()).get(var.reg()).typespec());
    } else if (m_src[i]->is_int()) {
      if (m_src[i]->get_int() == 0 && condition_uses_float(m_kind)) {
        // if we're doing a floating point comparison, and one of our arguments is a constant
        // which is an "integer zero", treat it as a floating point zero.
        source_types.push_back(TypeSpec("float"));
      } else {
        source_types.push_back(TypeSpec("int"));
      }
    } else if (m_src[i]->is_sym_val() && m_src[i]->get_str() == "#f") {
      source_types.push_back(TypeSpec("symbol"));
    }

    else {
      throw std::runtime_error("Unsupported atom in ConditionElement::update_from_stack");
    }
  }
  if (m_flipped) {
    std::reverse(vars.begin(), vars.end());
  }

  popped_forms = pop_to_forms(vars, env, pool, stack, allow_side_effects, m_consumed);
  if (m_flipped) {
    std::reverse(popped_forms.begin(), popped_forms.end());
  }

  int popped_counter = 0;
  for (int i = 0; i < get_condition_num_args(m_kind); i++) {
    if (m_src[i]->is_var()) {
      source_forms.push_back(popped_forms.at(popped_counter++));
    } else {
      source_forms.push_back(pool.alloc_single_element_form<SimpleAtomElement>(nullptr, *m_src[i]));
    }
  }
  assert(popped_counter == int(popped_forms.size()));
  assert(source_forms.size() == source_types.size());

  result->push_back(make_generic(env, pool, source_forms, source_types));
}

void ReturnElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  FormStack temp_stack(false);
  for (auto& elt : return_code->elts()) {
    elt->push_to_stack(env, pool, temp_stack);
  }

  std::vector<FormElement*> new_entries;
  new_entries = rewrite_to_get_var(temp_stack, pool, env.end_var(), env);

  assert(!new_entries.empty());
  return_code->clear();

  for (int i = 0; i < ((int)new_entries.size()) - 1; i++) {
    stack.push_form_element(new_entries.at(i), true);
  }

  return_code->push_back(new_entries.back());
  stack.push_form_element(this, true);
}

namespace {

void push_asm_srl_to_stack(const AsmOp* op,
                           FormElement* /*form_elt*/,
                           const Env& env,
                           FormPool& pool,
                           FormStack& stack) {
  // we will try to convert this into a bitfield operation. If this fails, fall back to assembly.
  auto var = op->src(0);
  assert(var.has_value());  // srl should always have this.

  auto dst = op->dst();
  assert(dst.has_value());

  auto integer_atom = op->instruction().get_src(1);
  assert(integer_atom.is_imm());
  auto integer = integer_atom.get_imm();

  auto arg0_type = env.get_variable_type(*var, true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info) {
    auto base = pop_to_forms({*var}, env, pool, stack, true).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_LOGICAL_32BIT, integer);
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    assert(other);  // should be a high field.
    stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, other), true,
                            env.get_variable_type(*dst, true));
  } else {
    // stack.push_form_element(form_elt, true);
    auto src_var = pop_to_forms({*var}, env, pool, stack, true).at(0);
    auto as_ba = src_var->try_as_element<BitfieldAccessElement>();
    if (as_ba) {
      BitfieldManip step(BitfieldManip::Kind::RIGHT_SHIFT_LOGICAL_32BIT, integer);
      auto other = as_ba->push_step(step, env.dts->ts, pool, env);
      assert(other);  // should immediately get a field.
      stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, other), true,
                              env.get_variable_type(*dst, true));
    } else {
      throw std::runtime_error("Got invalid bitfield manip for srl");
    }
  }
}

void push_asm_sllv_to_stack(const AsmOp* op,
                            FormElement* form_elt,
                            const Env& env,
                            FormPool& pool,
                            FormStack& stack) {
  auto var = op->src(0);
  assert(var.has_value());

  auto dst = op->dst();
  assert(dst.has_value());

  auto sav = op->src(1);
  assert(sav.has_value());

  auto arg0_type = env.get_variable_type(*var, true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (sav->reg() == Register(Reg::GPR, Reg::R0)) {
    if (bitfield_info) {
      auto base = pop_to_forms({*var}, env, pool, stack, true).at(0);
      auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
      BitfieldManip step(BitfieldManip::Kind::SLLV_SEXT, 0);
      auto other = read_elt->push_step(step, env.dts->ts, pool, env);
      assert(other);  // should immediately get a field.
      stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, other), true,
                              env.get_variable_type(*dst, true));
    } else {
      auto src_var = pop_to_forms({*var}, env, pool, stack, true).at(0);
      auto as_ba = src_var->try_as_element<BitfieldAccessElement>();
      if (as_ba) {
        // part of existing chain.
        BitfieldManip step(BitfieldManip::Kind::SLLV_SEXT, 0);
        auto other = as_ba->push_step(step, env.dts->ts, pool, env);
        assert(other);  // should immediately get a field.
        stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, other), true,
                                env.get_variable_type(*dst, true));
      } else {
        // push it to a weird looking form for initial bitfield setting.
        // these are lazily converted at the destination.
        stack.push_value_to_reg(
            *dst,
            pool.alloc_single_element_form<GenericElement>(
                nullptr, GenericOperator::make_fixed(FixedOperatorKind::ASM_SLLV_R0), src_var),
            true, env.get_variable_type(*dst, true));
      }
    }
  } else {
    stack.push_form_element(form_elt, true);
  }
}

void push_asm_pcpyud_to_stack(const AsmOp* op,
                              FormElement* form_elt,
                              const Env& env,
                              FormPool& pool,
                              FormStack& stack) {
  // pcpyud v1, gp, r0 for example.

  auto var = op->src(0);
  assert(var.has_value());

  auto dst = op->dst();
  assert(dst.has_value());

  auto possible_r0 = op->src(1);
  assert(possible_r0.has_value());

  auto arg0_type = env.get_variable_type(*var, true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && possible_r0->reg() == Register(Reg::GPR, Reg::R0)) {
    auto base = pop_to_forms({*var}, env, pool, stack, true).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    read_elt->push_pcpyud(env.dts->ts, pool, env);
    stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, read_elt), true,
                            env.get_variable_type(*dst, true));
  } else {
    stack.push_form_element(form_elt, true);
  }
}

void push_asm_pextuw_to_stack(const AsmOp* op,
                              FormElement* form_elt,
                              const Env& env,
                              FormPool& pool,
                              FormStack& stack) {
  // (.pextuw t0-0 r0-0 obj)

  auto var = op->src(1);
  assert(var.has_value());

  auto dst = op->dst();
  assert(dst.has_value());

  auto possible_r0 = op->src(0);
  assert(possible_r0.has_value());

  auto arg0_type = env.get_variable_type(*var, true);
  auto type_info = env.dts->ts.lookup_type(arg0_type);
  auto bitfield_info = dynamic_cast<BitFieldType*>(type_info);
  if (bitfield_info && possible_r0->reg() == Register(Reg::GPR, Reg::R0)) {
    auto base = pop_to_forms({*var}, env, pool, stack, true).at(0);
    auto read_elt = pool.alloc_element<BitfieldAccessElement>(base, arg0_type);
    BitfieldManip step(BitfieldManip::Kind::PEXTUW, 0);
    auto other = read_elt->push_step(step, env.dts->ts, pool, env);
    assert(other);  // should immediately get a field.
    stack.push_value_to_reg(*dst, pool.alloc_single_form(nullptr, other), true,
                            env.get_variable_type(*dst, true));
  } else {
    stack.push_form_element(form_elt, true);
  }
}

/*
void push_asm_madds_to_stack(const AsmOp* op,
                             FormElement* form_elt,
                             const Env& env,
                             FormPool& pool,
                             FormStack& stack) {
  auto src0 = op->src(0);
  assert(src0.has_value());

  auto src1 = op->src(1);
  assert(src1.has_value());

  auto dst = op->dst();
  assert(dst.has_value());

  auto vars = pop_to_forms({*src0, *src1}, env, pool, stack, true);

  stack.push_value_to_reg(
      *dst,
      pool.alloc_single_element_form<GenericElement>(
          nullptr, GenericOperator::make_fixed(FixedOperatorKind::ASM_MADDS), vars),
      true, env.get_variable_type(*dst, true));
}
*/

void push_asm_to_stack(const AsmOp* op,
                       FormElement* form_elt,
                       const Env& env,
                       FormPool& pool,
                       FormStack& stack) {
  switch (op->instruction().kind) {
    case InstructionKind::SRL:
      push_asm_srl_to_stack(op, form_elt, env, pool, stack);
      break;
    case InstructionKind::SLLV:
      push_asm_sllv_to_stack(op, form_elt, env, pool, stack);
      break;
    case InstructionKind::PCPYUD:
      push_asm_pcpyud_to_stack(op, form_elt, env, pool, stack);
      break;
    case InstructionKind::PEXTUW:
      push_asm_pextuw_to_stack(op, form_elt, env, pool, stack);
      break;
      /*
    case InstructionKind::MADDS:
      push_asm_madds_to_stack(op, form_elt, env, pool, stack);
      break;
       */
    default:
      stack.push_form_element(form_elt, true);
      break;
  }
}

}  // namespace

void AtomicOpElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  auto as_end = dynamic_cast<const FunctionEndOp*>(m_op);
  if (as_end) {
    // we don't want to push this to the stack (for now at least)
    return;
  }

  auto as_special = dynamic_cast<const SpecialOp*>(m_op);
  if (as_special) {
    if (as_special->kind() == SpecialOp::Kind::NOP ||
        as_special->kind() == SpecialOp::Kind::BREAK ||
        as_special->kind() == SpecialOp::Kind::CRASH ||
        as_special->kind() == SpecialOp::Kind::SUSPEND) {
      stack.push_form_element(this, true);
      return;
    }
  }

  auto as_asm = dynamic_cast<const AsmOp*>(m_op);
  if (as_asm) {
    push_asm_to_stack(as_asm, this, env, pool, stack);
    return;
  }

  auto as_branch = dynamic_cast<AsmBranchOp*>(m_op);
  if (as_branch && !as_branch->is_likely()) {
    // this is a bit of a hack, but we go AsmBranchOp -> AsmBranchElement -> TranslatedAsmBranch
    auto delay = as_branch->branch_delay();
    assert(delay);
    // this might not be enough - we may need to back up to the cfg builder and do something there.
    auto del = pool.alloc_single_element_form<AtomicOpElement>(nullptr, delay);
    auto be = pool.alloc_element<AsmBranchElement>(as_branch, del, false);
    be->push_to_stack(env, pool, stack);
    return;
  }

  throw std::runtime_error("Cannot push atomic op to stack: " + m_op->to_string(env));
}

void AsmOpElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  push_asm_to_stack(m_op, this, env, pool, stack);
}

void GenericElement::update_from_stack(const Env& env,
                                       FormPool& pool,
                                       FormStack& stack,
                                       std::vector<FormElement*>* result,
                                       bool) {
  mark_popped();
  if (m_elts.size() == 1) {
    // a bit of a hack, but AtomicOpForm uses this for loading car/cdr
    // this is safe to do.
    m_elts.front()->update_children_from_stack(env, pool, stack, true);
  }
  result->push_back(this);
}

void AsmBranchElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  // create a condition element
  RegSet consumed;
  if (env.has_reg_use()) {
    consumed = env.reg_use().op.at(m_branch_op->op_id()).consumes;
  }
  std::optional<SimpleAtom> vars[2];
  for (int i = 0; i < get_condition_num_args(m_branch_op->condition().kind()); i++) {
    vars[i] = m_branch_op->condition().src(i);
  }
  auto ce = pool.alloc_element<ConditionElement>(m_branch_op->condition().kind(), vars[0], vars[1],
                                                 consumed, false);

  // and update it from the stack.
  std::vector<FormElement*> ce_updated;
  ce->update_from_stack(env, pool, stack, &ce_updated, true);

  auto branch_condition = pool.alloc_sequence_form(nullptr, ce_updated);

  auto op = pool.alloc_element<TranslatedAsmBranch>(
      branch_condition, m_branch_delay, m_branch_op->label_id(), m_branch_op->is_likely());
  // fmt::print("rewrote as {}\n", op->to_string(env));
  stack.push_form_element(op, true);
}

void BranchElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  // These will appear if we have an asm-branch that looked like a normal branch.
  // create a condition element
  RegSet consumed;
  if (env.has_reg_use()) {
    consumed = env.reg_use().op.at(m_op->op_id()).consumes;
  }
  std::optional<SimpleAtom> vars[2];
  for (int i = 0; i < get_condition_num_args(m_op->condition().kind()); i++) {
    vars[i] = m_op->condition().src(i);
  }
  auto ce = pool.alloc_element<ConditionElement>(m_op->condition().kind(), vars[0], vars[1],
                                                 consumed, false);

  // and update it from the stack.
  std::vector<FormElement*> ce_updated;
  ce->update_from_stack(env, pool, stack, &ce_updated, true);

  auto branch_condition = pool.alloc_sequence_form(nullptr, ce_updated);

  Form* branch_delay = nullptr;
  switch (m_op->branch_delay().kind()) {
    case IR2_BranchDelay::Kind::NOP: {
      branch_delay = nullptr;
    } break;
    case IR2_BranchDelay::Kind::SET_REG_REG: {
      auto src = m_op->branch_delay().var(1);
      auto dst = m_op->branch_delay().var(0);

      auto src_form =
          pool.alloc_single_element_form<SimpleAtomElement>(nullptr, SimpleAtom::make_var(src));

      branch_delay = pool.alloc_single_element_form<SetVarElement>(
          nullptr, dst, src_form, true, env.get_variable_type(src, true));
    } break;
    default:
      throw std::runtime_error("Unhandled branch delay in BranchElement::push_to_stack: " +
                               m_op->to_string(env));
  }

  auto op = pool.alloc_element<TranslatedAsmBranch>(branch_condition, branch_delay,
                                                    m_op->label_id(), m_op->likely());
  // fmt::print("rewrote (non-asm) as {}\n", op->to_string(env));
  stack.push_form_element(op, true);
}

void GenericElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  (void)env;
  (void)pool;
  mark_popped();
  stack.push_form_element(this, true);
}

////////////////////////
// DynamicMethodAccess
////////////////////////

void DynamicMethodAccess::update_from_stack(const Env& env,
                                            FormPool& pool,
                                            FormStack& stack,
                                            std::vector<FormElement*>* result,
                                            bool allow_side_effects) {
  mark_popped();
  // auto new_val = stack.pop_reg(m_source, {}, env, allow_side_effects);
  auto new_val = pop_to_forms({m_source}, env, pool, stack, allow_side_effects).at(0);
  auto reg0_matcher =
      Matcher::match_or({Matcher::any_reg(0), Matcher::cast("uint", Matcher::any_reg(0))});
  auto reg1_matcher =
      Matcher::match_or({Matcher::any_reg(1), Matcher::cast("int", Matcher::any_reg(1))});

  // (+ (* method-id 4) (the-as int child-type))
  auto mult_matcher =
      Matcher::fixed_op(FixedOperatorKind::MULTIPLICATION, {reg0_matcher, Matcher::integer(4)});
  auto matcher = Matcher::fixed_op(FixedOperatorKind::ADDITION, {mult_matcher, reg1_matcher});
  auto match_result = match(matcher, new_val);
  if (!match_result.matched) {
    throw std::runtime_error("Could not match DynamicMethodAccess values: " +
                             new_val->to_string(env));
  }

  auto idx = match_result.maps.regs.at(0);
  auto base = match_result.maps.regs.at(1);
  assert(idx.has_value() && base.has_value());

  auto deref = pool.alloc_element<DerefElement>(
      var_to_form(base.value(), pool), false,
      std::vector<DerefToken>{DerefToken::make_field_name("method-table"),
                              DerefToken::make_int_expr(var_to_form(idx.value(), pool))});
  result->push_back(deref);
}

////////////////////////
// ArrayFieldAccess
////////////////////////

void ArrayFieldAccess::update_with_val(Form* new_val,
                                       const Env& env,
                                       FormPool& pool,
                                       std::vector<FormElement*>* result,
                                       bool) {
  int power_of_two = 0;

  if (m_constant_offset == 0) {
    if (m_expected_stride == 1) {
      auto base_matcher =
          Matcher::match_or({Matcher::cast("int", Matcher::any(0)),
                             Matcher::cast("uint", Matcher::any(0)), Matcher::any(0)});
      auto offset_matcher =
          Matcher::match_or({Matcher::cast("int", Matcher::any(1)),
                             Matcher::cast("uint", Matcher::any(1)), Matcher::any(1)});

      // (&+ data-ptr <idx>)
      auto matcher = Matcher::match_or(
          {Matcher::fixed_op(FixedOperatorKind::ADDITION, {base_matcher, offset_matcher}),
           Matcher::fixed_op(FixedOperatorKind::ADDITION_PTR, {base_matcher, offset_matcher})});

      auto match_result = match(matcher, new_val);
      if (!match_result.matched) {
        throw std::runtime_error(
            fmt::format("Failed to match array stride 1 load {}", new_val->to_string(env)));
      }
      auto idx = match_result.maps.forms.at(1);
      auto base = match_result.maps.forms.at(0);
      assert(idx && base);

      if (m_flipped) {
        std::swap(idx, base);
      }

      std::vector<DerefToken> tokens = m_deref_tokens;
      for (auto& x : tokens) {
        if (x.kind() == DerefToken::Kind::EXPRESSION_PLACEHOLDER) {
          x = DerefToken::make_int_expr(idx);
        }
      }
      //      tokens.push_back(DerefToken::make_int_expr(idx));

      auto deref = pool.alloc_element<DerefElement>(base, false, tokens);
      result->push_back(deref);
    } else if (is_power_of_two(m_expected_stride, &power_of_two)) {
      // reg0 is base
      // reg1 is idx

      auto reg0_matcher =
          Matcher::match_or({Matcher::cast("int", Matcher::any(0)),
                             Matcher::cast("uint", Matcher::any(0)), Matcher::any(0)});
      auto reg1_matcher =
          Matcher::match_or({Matcher::cast("uint", Matcher::any(1)), Matcher::any(1)});
      auto mult_matcher = Matcher::fixed_op(FixedOperatorKind::MULTIPLICATION,
                                            {reg1_matcher, Matcher::integer(m_expected_stride)});
      mult_matcher = Matcher::match_or({Matcher::cast("uint", mult_matcher), mult_matcher});
      auto matcher = Matcher::match_or(
          {Matcher::fixed_op(FixedOperatorKind::ADDITION, {reg0_matcher, mult_matcher}),
           Matcher::fixed_op(FixedOperatorKind::ADDITION_PTR, {reg0_matcher, mult_matcher})});
      auto match_result = match(matcher, new_val);
      if (!match_result.matched) {
        matcher = Matcher::match_or(
            {Matcher::fixed_op(FixedOperatorKind::ADDITION, {mult_matcher, reg0_matcher}),
             Matcher::fixed_op(FixedOperatorKind::ADDITION_PTR, {mult_matcher, reg0_matcher})});
        match_result = match(matcher, new_val);
        if (!match_result.matched) {
          result->push_back(this);
          return;
          fmt::print("power {}\n", power_of_two);
          throw std::runtime_error(
              "Couldn't match ArrayFieldAccess (stride power of 2, 0 offset) values: " +
              new_val->to_string(env));
        }
      }

      auto idx = match_result.maps.forms.at(1);
      auto base = match_result.maps.forms.at(0);
      assert(idx && base);

      std::vector<DerefToken> tokens = m_deref_tokens;
      for (auto& x : tokens) {
        if (x.kind() == DerefToken::Kind::EXPRESSION_PLACEHOLDER) {
          x = DerefToken::make_int_expr(idx);
        }
      }
      //      tokens.push_back(DerefToken::make_int_expr(idx));

      auto deref = pool.alloc_element<DerefElement>(base, false, tokens);
      result->push_back(deref);
    } else {
      throw std::runtime_error("Not power of two case, not yet implemented (no offset)");
    }
  } else {
    if (m_expected_stride == 1) {
      // reg0 is idx
      auto reg0_matcher =
          Matcher::match_or({Matcher::cast("int", Matcher::any(0)),
                             Matcher::cast("uint", Matcher::any(0)), Matcher::any(0)});
      // reg1 is base
      auto reg1_matcher =
          Matcher::match_or({Matcher::cast("int", Matcher::any(1)),
                             Matcher::cast("uint", Matcher::any(1)), Matcher::any(1)});
      auto matcher = Matcher::fixed_op(FixedOperatorKind::ADDITION, {reg0_matcher, reg1_matcher});
      auto match_result = match(matcher, new_val);
      if (!match_result.matched) {
        throw std::runtime_error("Could not match ArrayFieldAccess (stride 1) values: " +
                                 new_val->to_string(env));
      }
      auto idx = match_result.maps.forms.at(0);
      auto base = match_result.maps.forms.at(1);
      assert(idx && base);

      std::vector<DerefToken> tokens = m_deref_tokens;
      for (auto& x : tokens) {
        if (x.kind() == DerefToken::Kind::EXPRESSION_PLACEHOLDER) {
          x = DerefToken::make_int_expr(idx);
        }
      }
      // tokens.push_back(DerefToken::make_int_expr(var_to_form(idx.value(), pool)));

      auto deref = pool.alloc_element<DerefElement>(base, false, tokens);
      result->push_back(deref);
    } else if (is_power_of_two(m_expected_stride, &power_of_two)) {
      // (+ (sll (the-as uint a1-0) 2) (the-as int a0-0))
      // (+ gp-0 (the-as uint (shl (the-as uint (shl (the-as uint s4-0) 2)) 2)))
      auto reg0_matcher =
          Matcher::match_or({Matcher::cast("uint", Matcher::any(0)), Matcher::any(0)});
      auto reg1_matcher =
          Matcher::match_or({Matcher::cast("uint", Matcher::any(1)),
                             Matcher::cast("int", Matcher::any(1)), Matcher::any(1)});
      auto mult_matcher = Matcher::fixed_op(FixedOperatorKind::MULTIPLICATION,
                                            {reg0_matcher, Matcher::integer(m_expected_stride)});
      mult_matcher = Matcher::match_or({Matcher::cast("uint", mult_matcher), mult_matcher});
      auto matcher = Matcher::fixed_op(FixedOperatorKind::ADDITION, {mult_matcher, reg1_matcher});
      auto match_result = match(matcher, new_val);
      Form* idx = nullptr;
      Form* base = nullptr;
      // TODO - figure out why it sometimes happens the other way.
      if (!match_result.matched) {
        matcher = Matcher::fixed_op(FixedOperatorKind::ADDITION, {reg1_matcher, mult_matcher});
        match_result = match(matcher, new_val);
        if (!match_result.matched) {
          throw std::runtime_error("Could not match ArrayFieldAccess (stride power of 2) values: " +
                                   new_val->to_string(env));
        }
      }
      idx = match_result.maps.forms.at(0);
      base = match_result.maps.forms.at(1);

      assert(idx && base);

      std::vector<DerefToken> tokens = m_deref_tokens;
      for (auto& x : tokens) {
        if (x.kind() == DerefToken::Kind::EXPRESSION_PLACEHOLDER) {
          x = DerefToken::make_int_expr(idx);
        }
      }
      // tokens.push_back(DerefToken::make_int_expr(var_to_form(idx.value(), pool)));

      auto deref = pool.alloc_element<DerefElement>(base, false, tokens);
      result->push_back(deref);
    } else {
      // (+ v0-0 (the-as uint (* 12 (+ a3-0 -1))))
      auto mult_matcher = Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::MULTIPLICATION),
                                      {Matcher::integer(m_expected_stride), Matcher::any(0)});
      mult_matcher = Matcher::match_or(
          {Matcher::cast("uint", mult_matcher), Matcher::cast("int", mult_matcher), mult_matcher});
      auto add_matcher = Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                                     {Matcher::any(1), mult_matcher});
      add_matcher = Matcher::match_or(
          {add_matcher, Matcher::op(GenericOpMatcher::fixed(FixedOperatorKind::ADDITION),
                                    {mult_matcher, Matcher::any(1)})});

      auto mr = match(add_matcher, new_val);
      if (!mr.matched) {
        throw std::runtime_error("Failed to match non-power of two case: " +
                                 new_val->to_string(env));
      }

      auto base = strip_int_or_uint_cast(mr.maps.forms.at(1));
      auto idx = mr.maps.forms.at(0);

      assert(idx && base);

      std::vector<DerefToken> tokens = m_deref_tokens;
      for (auto& x : tokens) {
        if (x.kind() == DerefToken::Kind::EXPRESSION_PLACEHOLDER) {
          x = DerefToken::make_int_expr(idx);
        }
      }

      auto deref = pool.alloc_element<DerefElement>(base, false, tokens);
      result->push_back(deref);
    }
  }
}

void ArrayFieldAccess::update_from_stack(const Env& env,
                                         FormPool& pool,
                                         FormStack& stack,
                                         std::vector<FormElement*>* result,
                                         bool allow_side_effects) {
  mark_popped();
  auto new_val = pop_to_forms({m_source}, env, pool, stack, allow_side_effects).at(0);
  update_with_val(new_val, env, pool, result, allow_side_effects);
}

////////////////////////
// CastElement
////////////////////////

void CastElement::update_from_stack(const Env& env,
                                    FormPool& pool,
                                    FormStack& stack,
                                    std::vector<FormElement*>* result,
                                    bool allow_side_effects) {
  mark_popped();
  m_source->update_children_from_stack(env, pool, stack, allow_side_effects);
  result->push_back(this);
}

////////////////////////
// TypeOfElement
////////////////////////

void TypeOfElement::update_from_stack(const Env& env,
                                      FormPool& pool,
                                      FormStack& stack,
                                      std::vector<FormElement*>* result,
                                      bool allow_side_effects) {
  mark_popped();
  value->update_children_from_stack(env, pool, stack, allow_side_effects);
  result->push_back(this);
}
////////////////////////
// EmptyElement
////////////////////////

void EmptyElement::push_to_stack(const Env&, FormPool&, FormStack& stack) {
  mark_popped();
  stack.push_form_element(this, true);
}

void StoreElement::push_to_stack(const Env&, FormPool&, FormStack& stack) {
  mark_popped();
  stack.push_form_element(this, true);
}

bool is_symbol_true(const Form* form) {
  auto as_simple = dynamic_cast<SimpleExpressionElement*>(form->try_as_single_element());
  if (as_simple && as_simple->expr().is_identity() && as_simple->expr().get_arg(0).is_sym_ptr() &&
      as_simple->expr().get_arg(0).get_str() == "#t") {
    return true;
  }
  return false;
}

void ConditionalMoveFalseElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  // pop the value and the original
  auto popped = pop_to_forms({old_value, source}, env, pool, stack, true);
  if (!is_symbol_true(popped.at(0))) {
    lg::warn("Failed to ConditionalMoveFalseElement::push_to_stack");
    stack.push_value_to_reg(source, popped.at(1), true, TypeSpec("symbol"));
    stack.push_form_element(this, true);
    return;
  }
  stack.push_value_to_reg(dest,
                          pool.alloc_single_element_form<GenericElement>(
                              nullptr,
                              GenericOperator::make_compare(on_zero ? IR2_Condition::Kind::NONZERO
                                                                    : IR2_Condition::Kind::ZERO),
                              std::vector<Form*>{popped.at(1)}),
                          true, TypeSpec("symbol"));
}

///////////////////////////
// StackSpillStoreElement
///////////////////////////
void StackSpillStoreElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();
  Form* src;
  if (m_value.is_var()) {
    src = pop_to_forms({m_value.var()}, env, pool, stack, true).at(0);
  } else {
    src = pool.alloc_single_element_form<SimpleAtomElement>(nullptr, m_value);
  }

  auto dst = pool.alloc_single_element_form<ConstantTokenElement>(
      nullptr, env.get_spill_slot_var_name(m_stack_offset));
  if (m_cast_type) {
    src = cast_form(src, *m_cast_type, pool, env);
  }
  stack.push_form_element(pool.alloc_element<SetFormFormElement>(dst, src), true);
}

namespace {

/*!
 * Is the given form an assembly form to load data from a vector to the given vf register?
 */
Form* is_load_store_vector_to_reg(const Register& reg,
                                  FormElement* form,
                                  bool is_load,
                                  int* idx_out) {
  auto as_vf_op = dynamic_cast<VectorFloatLoadStoreElement*>(form);
  if (!as_vf_op) {
    return nullptr;
  }

  if (as_vf_op->is_load() != is_load) {
    return nullptr;
  }

  if (as_vf_op->vf_reg() != reg) {
    return nullptr;
  }

  // check that we actually got a real vector type, not some other thing that happens to have a
  // quad.
  auto& addr_type = as_vf_op->addr_type();
  if (!addr_type || addr_type != TypeSpec("vector")) {
    return nullptr;
  }

  // make sure we load from the right spot, and extract the base.
  auto loc = as_vf_op->location();
  auto matcher = Matcher::deref(Matcher::any(0), true, {DerefTokenMatcher::string("quad")});
  auto mr = match(matcher, loc);
  if (!mr.matched) {
    return nullptr;
  }

  // output the index of the actual store op, for reguse purposes.
  if (idx_out) {
    *idx_out = as_vf_op->my_idx();
  }

  // got it!
  return mr.maps.forms.at(0);
}

/*!
 * Try to convert a form to a regaccess.
 */
std::optional<RegisterAccess> form_as_ra(Form* form) {
  auto as_atom = form_as_atom(form);
  if (as_atom && as_atom->is_var()) {
    return as_atom->var();
  }
  return {};
}

bool try_vector_reset_inline(const Env& env,
                             FormPool& pool,
                             FormStack& stack,
                             FormElement* store_element) {
  // the store
  int store_idx = -1;
  auto store = is_load_store_vector_to_reg(Register(Reg::VF, 0), store_element, false, &store_idx);
  if (!store) {
    return false;
  }

  // remove these from the stack.
  // stack.pop(1);

  // the store here _should_ have failed propagation and just given us a variable.
  // if this is causing issues, we can run this check before propagating, as well call this from
  // the function that attempts the pop.
  auto store_var = form_as_ra(store);
  if (!store_var) {
    env.func->warnings.general_warning("Almost found vector reset, but couldn't get store var.");
    // stack.push_form_element(new_thing->elts().at(0), true);
    return false;
  }

  // ignore the store as a use. This will allow the entire vector-! expression to be expression
  // propagated, if it is appropriate.
  if (store_var) {
    auto menv = const_cast<Env*>(&env);
    menv->disable_use(*store_var);
  }

  // now try to see if we can pop the first arg (destination vector).
  bool got_orig = false;
  RegisterAccess orig;
  store = repop_passthrough_arg(store, stack, env, &orig, &got_orig);

  // create the actual  form
  Form* new_thing = pool.alloc_single_element_form<GenericElement>(
      nullptr,
      GenericOperator::make_function(
          pool.alloc_single_element_form<ConstantTokenElement>(nullptr, "vector-reset!")),
      std::vector<Form*>{store});

  if (got_orig) {
    // we got a value for the destination.  because we used the special repop passthrough,
    // we're responsible for inserting a set to set the var that we "stole" from.
    // We do this through push_value_to_reg, so it can be propagated if needed, but only if
    // somebody will actually read the output.
    // to tell, we look at the live out of the store op and the end - the earlier one would of
    // course be live out always because the store will read it again.

    auto& op_info = env.reg_use().op.at(store_idx);
    if (op_info.live.find(orig.reg()) == op_info.live.end()) {
      // nobody reads it, don't bother.
      stack.push_form_element(new_thing->elts().at(0), true);
    } else {
      stack.push_value_to_reg(orig, new_thing, true, TypeSpec("vector"));
    }

  } else {
    stack.push_form_element(new_thing->elts().at(0), true);
  }

  return true;
}
}  // namespace

void VectorFloatLoadStoreElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();

  auto loc_as_deref = m_location->try_as_element<DerefElement>();
  if (loc_as_deref) {
    auto root = loc_as_deref->base();
    auto atom = form_as_atom(root);
    if (atom && atom->get_kind() == SimpleAtom::Kind::VARIABLE) {
      m_addr_type = env.get_variable_type(atom->var(), true);
    }
  }

  auto name = env.func->guessed_name.to_string();
  // don't find vector-! inside of vector-!.
  if (!m_is_load && name != "vector-!" && name != "vector+!" && name != "vector-reset!") {
    if (try_vector_reset_inline(env, pool, stack, this)) {
      return;
    }
  }

  if (loc_as_deref) {
    auto root = loc_as_deref->base();
    auto atom = form_as_atom(root);
    if (atom && atom->get_kind() == SimpleAtom::Kind::VARIABLE) {
      loc_as_deref->set_base(pop_to_forms({atom->var()}, env, pool, stack, true).at(0));
    }
  }
  stack.push_form_element(this, true);
}

void MethodOfTypeElement::update_from_stack(const Env& env,
                                            FormPool& pool,
                                            FormStack& stack,
                                            std::vector<FormElement*>* result,
                                            bool allow_side_effects) {
  mark_popped();
  auto type = pop_to_forms({m_type_reg}, env, pool, stack, allow_side_effects).at(0);

  auto type_as_deref = type->try_as_element<DerefElement>();
  if (type_as_deref) {
    if (type_as_deref->tokens().size() > 1 &&
        type_as_deref->tokens().back().is_field_name("type")) {
      type_as_deref->tokens().pop_back();
      result->push_back(pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::METHOD_OF_OBJECT),
          std::vector<Form*>{type, pool.alloc_single_element_form<ConstantTokenElement>(
                                       nullptr, m_method_info.name)}));
      return;
    } else if (type_as_deref->tokens().size() == 1 &&
               type_as_deref->tokens().back().is_field_name("type")) {
      result->push_back(pool.alloc_element<GenericElement>(
          GenericOperator::make_fixed(FixedOperatorKind::METHOD_OF_OBJECT),
          std::vector<Form*>{
              type_as_deref->base(),
              pool.alloc_single_element_form<ConstantTokenElement>(nullptr, m_method_info.name)}));
      return;
    }
  }

  result->push_back(pool.alloc_element<GenericElement>(
      GenericOperator::make_fixed(FixedOperatorKind::METHOD_OF_TYPE),
      std::vector<Form*>{type, pool.alloc_single_element_form<ConstantTokenElement>(
                                   nullptr, m_method_info.name)}));
}

void SimpleAtomElement::update_from_stack(const Env&,
                                          FormPool&,
                                          FormStack&,
                                          std::vector<FormElement*>* result,
                                          bool) {
  mark_popped();
  result->push_back(this);
}

void StringConstantElement::update_from_stack(const Env&,
                                              FormPool&,
                                              FormStack&,
                                              std::vector<FormElement*>* result,
                                              bool) {
  mark_popped();
  result->push_back(this);
}

void GetMethodElement::update_from_stack(const Env&,
                                         FormPool&,
                                         FormStack&,
                                         std::vector<FormElement*>* result,
                                         bool) {
  mark_popped();
  result->push_back(this);
}

void CondNoElseElement::update_from_stack(const Env&,
                                          FormPool&,
                                          FormStack&,
                                          std::vector<FormElement*>* result,
                                          bool) {
  mark_popped();
  result->push_back(this);
}

void ConstantTokenElement::update_from_stack(const Env&,
                                             FormPool&,
                                             FormStack&,
                                             std::vector<FormElement*>* result,
                                             bool) {
  mark_popped();
  result->push_back(this);
}

void ConstantFloatElement::update_from_stack(const Env&,
                                             FormPool&,
                                             FormStack&,
                                             std::vector<FormElement*>* result,
                                             bool) {
  mark_popped();
  result->push_back(this);
}

void StackStructureDefElement::update_from_stack(const Env&,
                                                 FormPool&,
                                                 FormStack&,
                                                 std::vector<FormElement*>* result,
                                                 bool) {
  mark_popped();
  result->push_back(this);
}

void StackSpillValueElement::update_from_stack(const Env&,
                                               FormPool&,
                                               FormStack&,
                                               std::vector<FormElement*>* result,
                                               bool) {
  mark_popped();
  result->push_back(this);
}

void GetSymbolStringPointer::update_from_stack(const Env&,
                                               FormPool&,
                                               FormStack&,
                                               std::vector<FormElement*>* result,
                                               bool) {
  mark_popped();
  result->push_back(this);
}

void LabelElement::push_to_stack(const Env&, FormPool&, FormStack& stack) {
  mark_popped();
  stack.push_form_element(this, true);
}

void BreakElement::push_to_stack(const Env& env, FormPool& pool, FormStack& stack) {
  mark_popped();

  FormStack temp_stack(false);
  for (auto& elt : return_code->elts()) {
    elt->push_to_stack(env, pool, temp_stack);
  }

  auto new_entries = temp_stack.rewrite(pool, env);
  return_code->clear();
  for (auto e : new_entries) {
    return_code->push_back(e);
  }
  stack.push_form_element(this, true);
}

}  // namespace decompiler
