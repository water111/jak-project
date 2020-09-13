/*!
 * @file Atoms.cpp
 * Compiler implementation for atoms - things which aren't compound forms.
 */

#include "goalc/compiler/Compiler.h"
#include "goalc/compiler/IR.h"

/*!
 * Main table for compiler forms
 */
static const std::unordered_map<
    std::string,
    Val* (Compiler::*)(const goos::Object& form, const goos::Object& rest, Env* env)>
    goal_forms = {
        //        // inline asm
        //        {".ret", &Compiler::compile_asm},
        //        {".push", &Compiler::compile_asm},
        //        {".pop", &Compiler::compile_asm},
        //        {".jmp", &Compiler::compile_asm},
        //        {".sub", &Compiler::compile_asm},
        //        {".ret-reg", &Compiler::compile_asm},
        //
        //        // BLOCK FORMS
        {"top-level", &Compiler::compile_top_level},
        {"begin", &Compiler::compile_begin},
        {"block", &Compiler::compile_block},
        {"return-from", &Compiler::compile_return_from},
        {"label", &Compiler::compile_label},
        {"goto", &Compiler::compile_goto},
        //
        //        // COMPILER CONTROL
        {"gs", &Compiler::compile_gs},
        {":exit", &Compiler::compile_exit},
        {"asm-file", &Compiler::compile_asm_file},
        {"listen-to-target", &Compiler::compile_listen_to_target},
        {"reset-target", &Compiler::compile_reset_target},
        {":status", &Compiler::compile_poke},
        //        {"test", &Compiler::compile_test},
        {"in-package", &Compiler::compile_in_package},
        //
        //        // CONDITIONAL COMPILATION
        {"#cond", &Compiler::compile_gscond},
        {"defglobalconstant", &Compiler::compile_defglobalconstant},
        {"seval", &Compiler::compile_seval},
        //
        //        // CONTROL FLOW
        //        {"cond", &Compiler::compile_cond},
        //        {"when-goto", &Compiler::compile_when_goto},
        //
        //        // DEFINITION
        {"define", &Compiler::compile_define},
        {"define-extern", &Compiler::compile_define_extern},
        //        {"set!", &Compiler::compile_set},
        //        {"defun-extern", &Compiler::compile_defun_extern},
        //        {"declare-method", &Compiler::compile_declare_method},
        //
        //        // DEFTYPE
        //        {"deftype", &Compiler::compile_deftype},
        //
        //        // ENUM
        //        {"defenum", &Compiler::compile_defenum},
        //
        //        // Field Access
        //        {"->", &Compiler::compile_deref},
        //        {"&", &Compiler::compile_addr_of},
        //
        //
        //        // LAMBDA
        {"lambda", &Compiler::compile_lambda},
        {"declare", &Compiler::compile_declare},
        {"inline", &Compiler::compile_inline},
        //        {"with-inline", &Compiler::compile_with_inline},
        //        {"rlet", &Compiler::compile_rlet},
        //        {"mlet", &Compiler::compile_mlet},
        //        {"get-ra-ptr", &Compiler::compile_get_ra_ptr},
        //
        //
        //
        //        // MACRO
        //        {"print-type", &Compiler::compile_print_type},
        {"quote", &Compiler::compile_quote},
        //        {"defconstant", &Compiler::compile_defconstant},
        //
        //        // OBJECT
        //
        //        {"the", &Compiler::compile_the},
        //        {"the-as", &Compiler::compile_the_as},
        //
        //        {"defmethod", &Compiler::compile_defmethod},
        //
        //        {"current-method-type", &Compiler::compile_current_method_type},
        //        {"new", &Compiler::compile_new},
        //        {"method", &Compiler::compile_method},
        //
        //        // PAIR
        //        {"car", &Compiler::compile_car},
        //        {"cdr", &Compiler::compile_cdr},
        //
        //        // IT IS MATH
        {"+", &Compiler::compile_add},
        {"-", &Compiler::compile_sub},
        {"*", &Compiler::compile_mul},
        //        {"/", &Compiler::compile_divide},
        //        {"shlv", &Compiler::compile_shlv},
        //        {"shrv", &Compiler::compile_shrv},
        //        {"sarv", &Compiler::compile_sarv},
        //        {"shl", &Compiler::compile_shl},
        //        {"shr", &Compiler::compile_shr},
        //        {"sar", &Compiler::compile_sar},
        //        {"mod", &Compiler::compile_mod},
        //        {"logior", &Compiler::compile_logior},
        //        {"logxor", &Compiler::compile_logxor},
        //        {"logand", &Compiler::compile_logand},
        //        {"lognot", &Compiler::compile_lognot},
        //        {"=", &Compiler::compile_condition_as_bool},
        //        {"!=", &Compiler::compile_condition_as_bool},
        //        {"eq?", &Compiler::compile_condition_as_bool},
        //        {"not", &Compiler::compile_condition_as_bool},
        //        {"<=", &Compiler::compile_condition_as_bool},
        //        {">=", &Compiler::compile_condition_as_bool},
        //        {"<", &Compiler::compile_condition_as_bool},
        //        {">", &Compiler::compile_condition_as_bool},
        //
        //        // BUILDER (build-dgo/build-cgo?)
        {"build-dgos", &Compiler::compile_build_dgo},
        //
        //        // UTIL
        {"set-config!", &Compiler::compile_set_config},

        //
        //        // temporary testing hacks...
        //        {"send-test", &Compiler::compile_send_test_data},
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
    case goos::ObjectType::SYMBOL:
      return compile_symbol(code, env);
    case goos::ObjectType::STRING:
      return compile_string(code, env);
    case goos::ObjectType::FLOAT:
      return compile_float(code, env);
    default:
      ice("Don't know how to compile " + code.print());
  }
  return get_none();
}

Val* Compiler::compile_pair(const goos::Object& code, Env* env) {
  auto pair = code.as_pair();
  auto head = pair->car;
  auto rest = pair->cdr;

  if (head.is_symbol()) {
    auto head_sym = head.as_symbol();
    // first try as a goal compiler form
    auto kv_gfs = goal_forms.find(head_sym->name);
    if (kv_gfs != goal_forms.end()) {
      return ((*this).*(kv_gfs->second))(code, rest, env);
    }

    goos::Object macro_obj;
    if (try_getting_macro_from_goos(head, &macro_obj)) {
      return compile_goos_macro(code, macro_obj, rest, env);
    }

    // todo enum
  }

  // todo function or method call
  return compile_function_or_method_call(code, env);
  //  throw_compile_error(code, "Unrecognized symbol at head of form");
  //  return nullptr;
}

Val* Compiler::compile_integer(const goos::Object& code, Env* env) {
  return compile_integer(code.integer_obj.value, env);
}

Val* Compiler::compile_integer(s64 value, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  return fe->alloc_val<IntegerConstantVal>(m_ts.make_typespec("int"), value);
}

SymbolVal* Compiler::compile_get_sym_obj(const std::string& name, Env* env) {
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  return fe->alloc_val<SymbolVal>(name, m_ts.make_typespec("symbol"));
}

Val* Compiler::compile_symbol(const goos::Object& form, Env* env) {
  auto name = symbol_string(form);

  if (name == "none") {
    return get_none();
  }

  // todo mlet

  auto lexical = env->lexical_lookup(form);
  if (lexical) {
    return lexical;
  }

  auto global_constant = m_global_constants.find(form.as_symbol());
  auto existing_symbol = m_symbol_types.find(form.as_symbol()->name);

  if (global_constant != m_global_constants.end()) {
    // check there is no symbol with the same name
    if (existing_symbol != m_symbol_types.end()) {
      throw_compile_error(form,
                          "symbol is both a runtime symbol and a global constant.  Something is "
                          "likely very wrong.");
    }

    // got a global constant
    return compile_error_guard(global_constant->second, env);
  }

  return compile_get_symbol_value(name, env);
}

Val* Compiler::compile_get_symbol_value(const std::string& name, Env* env) {
  auto existing_symbol = m_symbol_types.find(name);
  if (existing_symbol == m_symbol_types.end()) {
    // assert(false);
    throw std::runtime_error("The symbol " + name + " was not defined");
  }

  auto ts = existing_symbol->second;
  auto sext = m_ts.lookup_type(ts)->get_load_signed();
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto sym = fe->alloc_val<SymbolVal>(name, m_ts.make_typespec("symbol"));
  auto re = fe->alloc_val<SymbolValueVal>(sym, ts, sext);
  return re;
}

Val* Compiler::compile_string(const goos::Object& form, Env* env) {
  return compile_string(form.as_string()->data, env, MAIN_SEGMENT);
}

Val* Compiler::compile_string(const std::string& str, Env* env, int seg) {
  auto obj = std::make_unique<StaticString>(str, seg);
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto result = fe->alloc_val<StaticVal>(obj.get(), m_ts.make_typespec("string"));
  auto fie = get_parent_env_of_type<FileEnv>(env);
  fie->add_static(std::move(obj));
  return result;
}

Val* Compiler::compile_float(const goos::Object& code, Env* env) {
  assert(code.is_float());
  return compile_float(code.float_obj.value, env, MAIN_SEGMENT);
}

Val* Compiler::compile_float(float value, Env* env, int seg) {
  auto obj = std::make_unique<StaticFloat>(value, seg);
  auto fe = get_parent_env_of_type<FunctionEnv>(env);
  auto result = fe->alloc_val<FloatConstantVal>(m_ts.make_typespec("float"), obj.get());
  auto fie = get_parent_env_of_type<FileEnv>(env);
  fie->add_static(std::move(obj));
  return result;
}