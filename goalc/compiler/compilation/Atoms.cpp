/*!
 * @file Atoms.cpp
 * Top-level compilation forms for each of the GOOS object types.
 */

#include "goalc/compiler/Compiler.h"
#include "goalc/compiler/IR.h"

/*!
 * Main table for compiler forms
 */
const std::unordered_map<
    std::string,
    Val* (Compiler::*)(const goos::Object& form, const goos::Object& rest, Env* env)>
    g_goal_forms = {
        // INLINE ASM
        {".nop", &Compiler::compile_nop},
        {".ret", &Compiler::compile_asm_ret},
        {".push", &Compiler::compile_asm_push},
        {".pop", &Compiler::compile_asm_pop},
        {"rlet", &Compiler::compile_rlet},
        {".jr", &Compiler::compile_asm_jr},
        {".sub", &Compiler::compile_asm_sub},
        {".add", &Compiler::compile_asm_add},
        {".load-sym", &Compiler::compile_asm_load_sym},
        {".mov", &Compiler::compile_asm_mov},

        // INLINE ASM - VECTOR FLOAT OPERATIONS
        {".lvf", &Compiler::compile_asm_lvf},
        {".svf", &Compiler::compile_asm_svf},
        {".mov.vf", &Compiler::compile_asm_mov_vf},
        {".blend.vf", &Compiler::compile_asm_blend_vf},

        {".nop.vf", &Compiler::compile_asm_nop_vf},
        {".wait.vf", &Compiler::compile_asm_wait_vf},

        {".xor.vf", &Compiler::compile_asm_xor_vf},
        {".xor.p", &Compiler::compile_asm_xorp},

        {".max.vf", &Compiler::compile_asm_max_vf},
        {".max.x.vf", &Compiler::compile_asm_max_x_vf},
        {".max.y.vf", &Compiler::compile_asm_max_y_vf},
        {".max.z.vf", &Compiler::compile_asm_max_z_vf},
        {".max.w.vf", &Compiler::compile_asm_max_w_vf},

        {".min.vf", &Compiler::compile_asm_min_vf},
        {".min.x.vf", &Compiler::compile_asm_min_x_vf},
        {".min.y.vf", &Compiler::compile_asm_min_y_vf},
        {".min.z.vf", &Compiler::compile_asm_min_z_vf},
        {".min.w.vf", &Compiler::compile_asm_min_w_vf},

        {".add.vf", &Compiler::compile_asm_add_vf},
        {".add.x.vf", &Compiler::compile_asm_add_x_vf},
        {".add.y.vf", &Compiler::compile_asm_add_y_vf},
        {".add.z.vf", &Compiler::compile_asm_add_z_vf},
        {".add.w.vf", &Compiler::compile_asm_add_w_vf},

        {".sub.vf", &Compiler::compile_asm_sub_vf},
        {".sub.x.vf", &Compiler::compile_asm_sub_x_vf},
        {".sub.y.vf", &Compiler::compile_asm_sub_y_vf},
        {".sub.z.vf", &Compiler::compile_asm_sub_z_vf},
        {".sub.w.vf", &Compiler::compile_asm_sub_w_vf},

        {".mul.vf", &Compiler::compile_asm_mul_vf},
        {".mul.x.vf", &Compiler::compile_asm_mul_x_vf},
        {".mul.y.vf", &Compiler::compile_asm_mul_y_vf},
        {".mul.z.vf", &Compiler::compile_asm_mul_z_vf},
        {".mul.w.vf", &Compiler::compile_asm_mul_w_vf},

        {".add.mul.vf", &Compiler::compile_asm_mul_add_vf},
        {".add.mul.x.vf", &Compiler::compile_asm_mul_add_x_vf},
        {".add.mul.y.vf", &Compiler::compile_asm_mul_add_y_vf},
        {".add.mul.z.vf", &Compiler::compile_asm_mul_add_z_vf},
        {".add.mul.w.vf", &Compiler::compile_asm_mul_add_w_vf},

        {".sub.mul.vf", &Compiler::compile_asm_mul_sub_vf},
        {".sub.mul.x.vf", &Compiler::compile_asm_mul_sub_x_vf},
        {".sub.mul.y.vf", &Compiler::compile_asm_mul_sub_y_vf},
        {".sub.mul.z.vf", &Compiler::compile_asm_mul_sub_z_vf},
        {".sub.mul.w.vf", &Compiler::compile_asm_mul_sub_w_vf},

        {".abs.vf", &Compiler::compile_asm_abs_vf},
        // NOTE - to compute the Outer Product with the VU, two back to back instructions were used
        // involving the ACC
        // However, we can be better than that and just provide a single instruction
        // BUT - if things used side effects of the modified ACC or benefited from only doing 1/2
        // operations, we'll need to implement them separately.
        {".outer.product.vf", &Compiler::compile_asm_outer_product_vf},

        {".div.vf", &Compiler::compile_asm_div_vf},
        {".sqrt.vf", &Compiler::compile_asm_sqrt_vf},
        {".isqrt.vf", &Compiler::compile_asm_inv_sqrt_vf},
        {".itof.vf", &Compiler::compile_asm_itof_vf},
        {".ftoi.vf", &Compiler::compile_asm_ftoi_vf},

        {".pw.sll", &Compiler::compile_asm_pw_sll},
        {".pw.srl", &Compiler::compile_asm_pw_srl},
        {".pw.sra", &Compiler::compile_asm_pw_sra},

        {".por", &Compiler::compile_asm_por},
        {".pnor", &Compiler::compile_asm_pnor},
        {".pand", &Compiler::compile_asm_pand},

        {".pceqb", &Compiler::compile_asm_pceqb},
        {".pceqh", &Compiler::compile_asm_pceqh},
        {".pceqw", &Compiler::compile_asm_pceqw},

        {".pcgtb", &Compiler::compile_asm_pcgtb},
        {".pcgth", &Compiler::compile_asm_pcgth},
        {".pcgtw", &Compiler::compile_asm_pcgtw},

        {".pextlb", &Compiler::compile_asm_pextlb},
        {".pextlh", &Compiler::compile_asm_pextlh},
        {".pextlw", &Compiler::compile_asm_pextlw},

        {".pextub", &Compiler::compile_asm_pextub},
        {".pextuh", &Compiler::compile_asm_pextuh},
        {".pextuw", &Compiler::compile_asm_pextuw},

        {".pcpyld", &Compiler::compile_asm_pcpyld},
        {".pcpyud", &Compiler::compile_asm_pcpyud},
        {".pceqw", &Compiler::compile_asm_pceqw},
        {".ppach", &Compiler::compile_asm_ppach},
        {".psubw", &Compiler::compile_asm_psubw},

        // BLOCK FORMS
        {"top-level", &Compiler::compile_top_level},
        {"begin", &Compiler::compile_begin},
        {"block", &Compiler::compile_block},
        {"return-from", &Compiler::compile_return_from},
        {"label", &Compiler::compile_label},
        {"goto", &Compiler::compile_goto},
        {"nop!", &Compiler::compile_nop},

        // COMPILER CONTROL
        {"repl-help", &Compiler::compile_repl_help},
        {":clear", &Compiler::compile_repl_clear_screen},
        {"gs", &Compiler::compile_gs},
        {":exit", &Compiler::compile_exit},
        {"asm-file", &Compiler::compile_asm_file},
        {"asm-data-file", &Compiler::compile_asm_data_file},
        {"listen-to-target", &Compiler::compile_listen_to_target},
        {"reset-target", &Compiler::compile_reset_target},
        {":status", &Compiler::compile_poke},
        {"in-package", &Compiler::compile_in_package},
        {"reload", &Compiler::compile_reload},
        {"get-info", &Compiler::compile_get_info},
        {"autocomplete", &Compiler::compile_autocomplete},
        {"add-macro-to-autocomplete", &Compiler::compile_add_macro_to_autocomplete},

        // CONDITIONAL COMPILATION
        {"#cond", &Compiler::compile_gscond},
        {"defglobalconstant", &Compiler::compile_defglobalconstant},
        {"seval", &Compiler::compile_seval},

        // CONTROL FLOW
        {"cond", &Compiler::compile_cond},
        {"when-goto", &Compiler::compile_when_goto},
        {"and", &Compiler::compile_and_or},
        {"or", &Compiler::compile_and_or},

        // DEFINITION
        {"define", &Compiler::compile_define},
        {"define-extern", &Compiler::compile_define_extern},
        {"set!", &Compiler::compile_set},

        // DEBUGGING
        {"dbs", &Compiler::compile_dbs},
        {"dbg", &Compiler::compile_dbg},
        {":cont", &Compiler::compile_cont},
        {":break", &Compiler::compile_break},
        {":dump-all-mem", &Compiler::compile_dump_all},
        {":pm", &Compiler::compile_pm},
        {":di", &Compiler::compile_di},
        {":disasm", &Compiler::compile_disasm},
        {":bp", &Compiler::compile_bp},
        {":ubp", &Compiler::compile_ubp},

        // TYPE
        {"deftype", &Compiler::compile_deftype},
        {"defmethod", &Compiler::compile_defmethod},
        {"defenum", &Compiler::compile_defenum},
        {"->", &Compiler::compile_deref},
        {"&", &Compiler::compile_addr_of},
        {"the-as", &Compiler::compile_the_as},
        {"the", &Compiler::compile_the},
        {"print-type", &Compiler::compile_print_type},
        {"new", &Compiler::compile_new},
        {"car", &Compiler::compile_car},
        {"cdr", &Compiler::compile_cdr},
        {"method-of-type", &Compiler::compile_method_of_type},
        {"method-of-object", &Compiler::compile_method_of_object},
        {"declare-type", &Compiler::compile_declare_type},
        {"none", &Compiler::compile_none},
        {"size-of", &Compiler::compile_size_of},
        {"psize-of", &Compiler::compile_psize_of},

        // LAMBDA
        {"lambda", &Compiler::compile_lambda},
        {"declare", &Compiler::compile_declare},
        {"inline", &Compiler::compile_inline},
        {"local-vars", &Compiler::compile_local_vars},
        //        {"with-inline", &Compiler::compile_with_inline},
        //        {"get-ra-ptr", &Compiler::compile_get_ra_ptr},

        // MACRO
        {"quote", &Compiler::compile_quote},
        {"mlet", &Compiler::compile_mlet},
        {"defconstant", &Compiler::compile_defconstant},

        // OBJECT
        //        {"current-method-type", &Compiler::compile_current_method_type},

        // MATH
        {"+", &Compiler::compile_add},
        {"-", &Compiler::compile_sub},
        {"*", &Compiler::compile_mul},
        {"imul64", &Compiler::compile_imul64},
        {"/", &Compiler::compile_div},
        {"shl", &Compiler::compile_shl},
        {"shr", &Compiler::compile_shr},
        {"sar", &Compiler::compile_sar},
        {"mod", &Compiler::compile_mod},
        {"logior", &Compiler::compile_logior},
        {"logxor", &Compiler::compile_logxor},
        {"logand", &Compiler::compile_logand},
        {"lognot", &Compiler::compile_lognot},
        {"=", &Compiler::compile_condition_as_bool},
        {"!=", &Compiler::compile_condition_as_bool},
        {"eq?", &Compiler::compile_condition_as_bool},
        {"neq?", &Compiler::compile_condition_as_bool},
        {"not", &Compiler::compile_condition_as_bool},
        {"<=", &Compiler::compile_condition_as_bool},
        {">=", &Compiler::compile_condition_as_bool},
        {"<", &Compiler::compile_condition_as_bool},
        {">", &Compiler::compile_condition_as_bool},
        {"&+", &Compiler::compile_pointer_add},
        {"fmax", &Compiler::compile_fmax},
        {"fmin", &Compiler::compile_fmin},
        {"sqrtf", &Compiler::compile_sqrtf},

        // BUILDER (build-dgo/build-cgo?)
        {"build-dgos", &Compiler::compile_build_dgo},

        // UTIL
        {"set-config!", &Compiler::compile_set_config},
};

/*!
 * Highest level compile function
 */
Val* Compiler::compile(const goos::Object& code, Env* env) {
  switch (code.type) {
    case goos::ObjectType::PAIR:
      return compile_pair(code, env);
    case goos::ObjectType::INTEGER:
      return compile_integer(code, env);
    case goos::ObjectType::CHAR:
      return compile_char(code, env);
    case goos::ObjectType::SYMBOL:
      return compile_symbol(code, env);
    case goos::ObjectType::STRING:
      return compile_string(code, env);
    case goos::ObjectType::FLOAT:
      return compile_float(code, env);
    default:
      throw_compiler_error(code, "Cannot compile {}.", code.print());
  }
  return get_none();
}

/*!
 * Compile a pair/list.
 * Can be a compiler form, function call (possibly inlined), method call, immediate application of a
 * lambda, or a goos macro.
 */
Val* Compiler::compile_pair(const goos::Object& code, Env* env) {
  auto pair = code.as_pair();
  auto head = pair->car;
  auto rest = pair->cdr;

  if (head.is_symbol()) {
    auto head_sym = head.as_symbol();
    // first try as a goal compiler form
    auto kv_gfs = g_goal_forms.find(head_sym->name);
    if (kv_gfs != g_goal_forms.end()) {
      return ((*this).*(kv_gfs->second))(code, rest, env);
    }

    // next try as a macro
    goos::Object macro_obj;
    if (try_getting_macro_from_goos(head, &macro_obj)) {
      return compile_goos_macro(code, macro_obj, rest, env);
    }

    // next try as an enum
    auto enum_type = m_ts.try_enum_lookup(head_sym->name);
    if (enum_type) {
      return compile_enum_lookup(code, enum_type, rest, env);
    }
  }

  // if none of the above cases worked, then treat it like a function/method call.
  return compile_function_or_method_call(code, env);
}

/*!
 * Compile an integer constant. Returns an IntegerConstantVal and emits no code.
 * These integer constants do not generate static data and are stored directly in the code
 * which is generated with to_gpr.
 * The type is always int.
 */
Val* Compiler::compile_integer(const goos::Object& code, Env* env) {
  assert(code.is_int());
  return compile_integer(code.integer_obj.value, env);
}

Val* Compiler::compile_char(const goos::Object& code, Env* env) {
  assert(code.is_char());
  return compile_integer(uint8_t(code.char_obj.value), env);
}

/*!
 * Compile an integer constant. Returns an IntegerConstantVal and emits no code.
 * These integer constants do not generate static data and are stored directly in the code
 * which is generated with to_gpr.
 * The type is always int.
 */
Val* Compiler::compile_integer(s64 value, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  return fe->alloc_val<IntegerConstantVal>(m_ts.make_typespec("int"), &value, 8);
}

Val* Compiler::compile_integer(const U128& value, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  return fe->alloc_val<IntegerConstantVal>(m_ts.make_typespec("int"), &value, 16);
}

/*!
 * Get a SymbolVal representing a GOAL symbol object.
 */
SymbolVal* Compiler::compile_get_sym_obj(const std::string& name, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  return fe->alloc_val<SymbolVal>(name, m_ts.make_typespec("symbol"));
}

/*!
 * Get a SymbolValueVal representing the value of a GOAL symbol.
 * Will throw a compilation error if the symbol wasn't previously defined.
 * TODO - determine sign extension behavior when loading symbol values.
 */
Val* Compiler::compile_get_symbol_value(const goos::Object& form,
                                        const std::string& name,
                                        Env* env) {
  auto existing_symbol = m_symbol_types.find(name);
  if (existing_symbol == m_symbol_types.end()) {
    throw_compiler_error(
        form, "The symbol {} was looked up as a global variable, but it does not exist.", name);
  }

  auto ts = existing_symbol->second;
  auto sext = m_ts.lookup_type(ts)->get_load_signed();
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto sym = fe->alloc_val<SymbolVal>(name, m_ts.make_typespec("symbol"));
  auto re = fe->alloc_val<SymbolValueVal>(sym, ts, sext);
  return re;
}

/*!
 * Compile a symbol. Can get mlet macro symbols, local variables, constants, or symbols.
 */
Val* Compiler::compile_symbol(const goos::Object& form, Env* env) {
  auto name = symbol_string(form);

  // optimization to look these up as symbol objects, not getting the value of a symbol.
  // so you don't have to type '#f, '#t everywhere to get the best performance.
  if (name == "#t" || name == "#f") {
    return compile_get_sym_obj(name, env);
  }

  // see if the symbol is defined in any enclosing symbol macro envs (mlet's).
  auto mlet_env = get_parent_env_of_type<SymbolMacroEnv>(env);
  while (mlet_env) {
    auto mlkv = mlet_env->macros.find(form.as_symbol());
    if (mlkv != mlet_env->macros.end()) {
      return compile_error_guard(mlkv->second, env);
    }
    mlet_env = get_parent_env_of_type<SymbolMacroEnv>(mlet_env->parent());
  }

  // see if it's a local variable
  auto lexical = env->lexical_lookup(form);
  if (lexical) {
    return lexical;
  }

  auto global_constant = m_global_constants.find(form.as_symbol());
  auto existing_symbol = m_symbol_types.find(form.as_symbol()->name);

  // see if it's a constant
  if (global_constant != m_global_constants.end()) {
    // check there is no symbol with the same name
    if (existing_symbol != m_symbol_types.end()) {
      throw_compiler_error(form,
                           "Ambiguous symbol: {} is both a global variable and a constant and it "
                           "is not clear which should be used here.");
    }

    // got a global constant
    return compile_error_guard(global_constant->second, env);
  }

  // none of those, so get a global symbol.
  return compile_get_symbol_value(form, name, env);
}

/*!
 * Compile a string constant. The constant is placed in the same segment as the parent function.
 */
Val* Compiler::compile_string(const goos::Object& form, Env* env) {
  auto segment = get_parent_env_of_type<FunctionEnv>(env)->segment;
  if (segment == TOP_LEVEL_SEGMENT) {
    segment = MAIN_SEGMENT;
  }
  return compile_string(form.as_string()->data, env, segment);
}

/*!
 * Compile a string constant and place it in the given segment.
 */
Val* Compiler::compile_string(const std::string& str, Env* env, int seg) {
  auto obj = std::make_unique<StaticString>(str, seg);
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto result = fe->alloc_val<StaticVal>(obj.get(), m_ts.make_typespec("string"));
  auto fie = get_parent_env_of_type<FileEnv>(env);
  fie->add_static(std::move(obj));
  return result;
}

/*!
 * Compile a floating point constant and place it in the same segment as the containing function.
 * Unlike integers, all floating point constants are stored separately as static data outside
 * of the code, at least in Jak 1.
 */
Val* Compiler::compile_float(const goos::Object& code, Env* env) {
  auto segment = get_parent_env_of_type<FunctionEnv>(env)->segment;
  if (segment == TOP_LEVEL_SEGMENT) {
    segment = MAIN_SEGMENT;
  }
  assert(code.is_float());
  return compile_float(code.float_obj.value, env, segment);
}

/*!
 * Compile a floating point constant and place it in given segment.
 * Unlike integers, all floating point constants are stored separately as static data outside
 * of the code, at least in Jak 1.
 */
Val* Compiler::compile_float(float value, Env* env, int seg) {
  auto obj = std::make_unique<StaticFloat>(value, seg);
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto result = fe->alloc_val<FloatConstantVal>(m_ts.make_typespec("float"), obj.get());
  auto fie = get_parent_env_of_type<FileEnv>(env);
  fie->add_static(std::move(obj));
  return result;
}

Val* Compiler::compile_pointer_add(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  if (args.unnamed.size() < 2 || !args.named.empty()) {
    throw_compiler_error(form, "&+ must be used with at least two arguments.");
  }
  auto first = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);

  bool ok_type = false;
  for (auto& type : {"pointer", "structure", "inline-array"}) {
    if (m_ts.tc(m_ts.make_typespec(type), first->type())) {
      ok_type = true;
      break;
    }
  }

  if (!ok_type) {
    throw_compiler_error(
        form, "&+ was used with a {}, which is not a pointer, structure, or inline-array.",
        first->type().print());
  }

  auto result = env->make_gpr(first->type());
  env->emit(std::make_unique<IR_RegSet>(result, first));

  for (size_t i = 1; i < args.unnamed.size(); i++) {
    auto second = compile_error_guard(args.unnamed.at(i), env)->to_gpr(env);
    typecheck(form, m_ts.make_typespec("integer"), second->type(), "&+ second argument");
    env->emit(std::make_unique<IR_IntegerMath>(IntegerMathKind::ADD_64, result, second));
  }

  return result;
}
