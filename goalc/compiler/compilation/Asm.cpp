#include "goalc/compiler/Compiler.h"

namespace {
const char* reg_names[] = {
    "rax",  "rcx",  "rdx",  "rbx",  "rsp",   "rbp",   "rsi",   "rdi",   "r8",    "r9",    "r10",
    "r11",  "r12",  "r13",  "r14",  "r15",   "xmm0",  "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",
    "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
};

}

emitter::Register Compiler::parse_register(const goos::Object& code) {
  if (!code.is_symbol()) {
    throw_compiler_error(code, "Could not parse {} as a register name", code.print());
  }

  auto nas = code.as_symbol();
  for (int i = 0; i < 32; i++) {
    if (nas->name == reg_names[i]) {
      return emitter::Register(i);
    }
  }

  throw_compiler_error(code, "Could not parse {} as a register name", code.print());
  return {};
}

Val* Compiler::compile_rlet(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  if (args.unnamed.size() < 1 || !args.named.empty()) {
    throw_compiler_error(form, "Must have an rlet body.");
  }

  auto defs = args.unnamed.front();

  auto fenv = get_parent_env_of_type<FunctionEnv>(env);
  auto lenv = fenv->alloc_env<LexicalEnv>(env);

  std::vector<IRegConstraint> constraints;
  std::vector<RegVal*> reset_regs;

  for_each_in_list(defs, [&](const goos::Object& o) {
    // (new-place [:reg old-place] [:type type-spec] [:class reg-type] [:bind #f|lexical|lambda])
    auto def_args = get_va(o, o);
    va_check(o, def_args, {goos::ObjectType::SYMBOL},
             {{"reg", {false, goos::ObjectType::SYMBOL}},
              {"type", {false, {}}},
              {"reset-here", {false, {}}},
              {"class", {false, goos::ObjectType::SYMBOL}}});

    // get the name of the new place
    auto new_place_name = def_args.unnamed.at(0);

    // get the type of the new place
    TypeSpec ts = m_ts.make_typespec("object");
    if (def_args.has_named("type")) {
      ts = parse_typespec(def_args.named.at("type"));
    }

    // figure out the class
    RegClass register_class = RegClass::GPR_64;
    if (def_args.has_named("class")) {
      auto& class_name = def_args.named.at("class").as_symbol()->name;
      if (class_name == "gpr") {
        register_class = RegClass::GPR_64;
      } else if (class_name == "fpr") {
        register_class = RegClass::FLOAT;
      } else if (class_name == "vf") {
        register_class = RegClass::VECTOR_FLOAT;
      } else if (class_name == "i128") {
        register_class = RegClass::INT_128;
      } else {
        throw_compiler_error(o, "Register class {} is unknown.", class_name);
      }
    }

    // alloc a register:
    auto new_place_reg = env->make_ireg(ts, register_class);
    new_place_reg->mark_as_settable();

    if (def_args.has_named("reg")) {
      IRegConstraint constraint;
      constraint.ireg = new_place_reg->ireg();
      constraint.contrain_everywhere = true;
      constraint.desired_register = parse_register(def_args.named.at("reg"));
      if (def_args.has_named("reset-here") &&
          get_true_or_false(form, def_args.get_named("reset-here"))) {
        reset_regs.push_back(new_place_reg);
      }

      new_place_reg->set_rlet_constraint(constraint.desired_register);
      constraints.push_back(constraint);
    }

    lenv->vars[new_place_name.as_symbol()->name] = new_place_reg;
  });

  if (!reset_regs.empty()) {
    lenv->emit_ir<IR_ValueReset>(reset_regs);
  }

  Val* result = get_none();
  for (u64 i = 1; i < args.unnamed.size(); i++) {
    auto& o = args.unnamed.at(i);
    result = compile_error_guard(o, lenv);
    if (!dynamic_cast<None*>(result)) {
      result = result->to_reg(lenv);
    }
  }

  for (auto c : constraints) {
    fenv->constrain(c);
  }

  return result;
}

Val* Compiler::compile_asm_ret(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  env->emit_ir<IR_AsmRet>(color);
  return get_none();
}

Val* Compiler::compile_asm_pop(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }
  auto pop_dest = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);
  if (!pop_dest->settable()) {
    throw_compiler_error(form, "Cannot pop into this destination. Got a {}.", pop_dest->print());
  }
  env->emit_ir<IR_AsmPop>(color, pop_dest);
  return get_none();
}

Val* Compiler::compile_asm_push(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }
  env->emit_ir<IR_AsmPush>(color, compile_error_guard(args.unnamed.at(0), env)->to_gpr(env));
  return get_none();
}

Val* Compiler::compile_asm_sub(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }
  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);
  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot .sub this. Got a {}.", dest->print());
  }
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_gpr(env);
  env->emit_ir<IR_AsmSub>(color, dest, src);
  return get_none();
}

Val* Compiler::compile_asm_add(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }
  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);
  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot .add this. Got a {}.", dest->print());
  }
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_gpr(env);
  env->emit_ir<IR_AsmAdd>(color, dest, src);
  return get_none();
}

Val* Compiler::compile_asm_load_sym(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {goos::ObjectType::SYMBOL}},
      {{"sext", {false, goos::ObjectType::SYMBOL}}, {"color", {false, goos::ObjectType::SYMBOL}}});
  auto& sym_name = args.unnamed.at(1).as_symbol()->name;
  auto sym_kv = m_symbol_types.find(sym_name);
  if (sym_kv == m_symbol_types.end()) {
    throw_compiler_error(form, "Cannot find a symbol named {}.", sym_name);
  }
  auto ts = sym_kv->second;
  bool sext = m_ts.lookup_type(ts)->get_load_signed();
  if (args.has_named("sext")) {
    sext = get_true_or_false(form, args.named.at("sext"));
  }

  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);
  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot .load-sym this. Got a {}.", dest->print());
  }

  env->emit_ir<IR_GetSymbolValueAsm>(color, dest, sym_name, sext);
  return get_none();
}

Val* Compiler::compile_asm_jr(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto src = compile_error_guard(args.unnamed.at(0), env)->to_gpr(env);
  env->emit_ir<IR_JumpReg>(color, src);
  return get_none();
}

Val* Compiler::compile_asm_mov(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }
  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot .mov this. Got a {}.", dest->print());
  }
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  env->emit_ir<IR_RegSetAsm>(color, dest, src);
  return get_none();
}

Val* Compiler::compile_asm_nop_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {}, {});

  env->emit_ir<IR_AsmFNop>();
  return get_none();
}

Val* Compiler::compile_asm_wait_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {}, {});

  env->emit_ir<IR_AsmFWait>();
  return get_none();
}

/*!
 * Load a vector float from memory. Does an aligned load.
 */
Val* Compiler::compile_asm_lvf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}},
           {{"color", {false, goos::ObjectType::SYMBOL}},
            {"offset", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  if (!dest->settable() || dest->ireg().reg_class != RegClass::VECTOR_FLOAT) {
    throw_compiler_error(form, "Cannot .lvf into this. Got a {}.", dest->print());
  }
  auto src = compile_error_guard(args.unnamed.at(1), env);

  auto as_sv = dynamic_cast<StaticVal*>(src);
  // Loading directly from a static value is not supported!
  if (as_sv && args.has_named("offset")) {
    throw_compiler_error(form, "Cannot .lvf from a static value");
  } else if (as_sv && !args.has_named("offset")) {
    env->emit_ir<IR_StaticVarLoad>(dest, as_sv->obj);
    return get_none();
  }

  auto as_co = dynamic_cast<MemoryOffsetConstantVal*>(src);
  RegVal* baseReg = as_co ? as_co->base->to_gpr(env) : src->to_gpr(env);
  int offset = 0;

  if (as_co) {
    if (!args.has_named("offset")) {
      offset = as_co->offset;
    } else {
      offset = as_co->offset + args.named.at("offset").as_int();
    }
  } else if (args.has_named("offset")) {
    offset = args.named.at("offset").as_int();
  }

  MemLoadInfo info;
  info.sign_extend = false;
  info.size = 16;
  info.reg = RegClass::VECTOR_FLOAT;
  env->emit_ir<IR_LoadConstOffset>(dest, offset, baseReg, info, color);

  return get_none();
}

/*!
 * Store a vector float into memory. Does an aligned load.
 */
Val* Compiler::compile_asm_svf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}},
           {{"color", {false, goos::ObjectType::SYMBOL}},
            {"offset", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);

  if (!src->settable() || src->ireg().reg_class != RegClass::VECTOR_FLOAT) {
    throw_compiler_error(form, "Cannot .svf from this. Got a {}.", dest->print());
  }

  auto as_co = dynamic_cast<MemoryOffsetConstantVal*>(dest);
  RegVal* baseReg = as_co ? as_co->base->to_gpr(env) : dest->to_gpr(env);
  int offset = 0;

  if (as_co) {
    if (!args.has_named("offset")) {
      offset = as_co->offset;
    } else {
      offset = as_co->offset + args.named.at("offset").as_int();
    }
  } else if (args.has_named("offset")) {
    offset = args.named.at("offset").as_int();
  }

  MemLoadInfo info;
  info.sign_extend = false;
  info.size = 16;
  info.reg = RegClass::VECTOR_FLOAT;
  env->emit_ir<IR_StoreConstOffset>(src, offset, baseReg, info.size, color);
  return get_none();
}

void Compiler::check_vector_float_regs(const goos::Object& form,
                                       Env*,
                                       std::vector<std::pair<std::string, RegVal*>> args) {
  for (std::pair<std::string, RegVal*> arg : args) {
    if (!arg.second->settable() || arg.second->ireg().reg_class != RegClass::VECTOR_FLOAT) {
      throw_compiler_error(form, "Invalid {} register for a vector float operation form. Got a {}.",
                           arg.first, arg.second->print());
    }
  }
}

Val* Compiler::compile_asm_mov_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  check_vector_float_regs(form, env, {{"destination", dest}, {"source", src}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  env->emit_ir<IR_BlendVF>(color, dest, dest, src, mask);
  return get_none();
}

Val* Compiler::compile_asm_blend_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest}, {"first source", src1}, {"second source", src2}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  env->emit_ir<IR_BlendVF>(color, dest, src1, src2, mask);
  return get_none();
}

Val* Compiler::compile_asm_vf_math3(const goos::Object& form,
                                    const goos::Object& rest,
                                    IR_VFMath3Asm::Kind kind,
                                    emitter::Register::VF_ELEMENT broadcastElement,
                                    Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest}, {"first source", src1}, {"second source", src2}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  // If there is a broadcast register, splat that float across the entire src2 register before
  // performing the operation For example vaddx.xyzw vf10, vf20, vf30
  // vf10[x] = vf20[x] + vf30[x]
  // vf10[y] = vf20[y] + vf30[x]
  // vf10[z] = vf20[z] + vf30[x]
  // vf10[w] = vf20[w] + vf30[x]
  if (broadcastElement != emitter::Register::VF_ELEMENT::NONE) {
    auto temp_reg = env->make_vfr(dest->type());
    env->emit_ir<IR_SplatVF>(color, temp_reg, src2, broadcastElement);

    // If the entire destination is to be copied, we can optimize out the blend
    if (mask == 0b1111) {
      env->emit_ir<IR_VFMath3Asm>(color, dest, src1, temp_reg, kind);
    } else {
      // Perform the arithmetic operation on the two vectors into a temporary register
      env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src1, temp_reg, kind);
      // Blend the result back into the destination register using the mask
      env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
    }
  } else {
    // If the entire destination is to be copied, we can optimize out the blend
    if (mask == 0b1111) {
      env->emit_ir<IR_VFMath3Asm>(color, dest, src1, src2, kind);
    } else {
      auto temp_reg = env->make_vfr(dest->type());
      // Perform the arithmetic operation on the two vectors into a temporary register
      env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src1, src2, kind);
      // Blend the result back into the destination register using the mask
      env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
    }
  }

  return get_none();
}

Val* Compiler::compile_asm_int128_math3(const goos::Object& form,
                                        const goos::Object& rest,
                                        IR_Int128Math3Asm::Kind kind,
                                        Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);

  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot set destination");
  }
  env->emit_ir<IR_Int128Math3Asm>(color, dest, src1, src2, kind);
  return get_none();
}

Val* Compiler::compile_asm_vf_math2(const goos::Object& form,
                                    const goos::Object& rest,
                                    IR_VFMath2Asm::Kind kind,
                                    Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  check_vector_float_regs(form, env, {{"destination", dest}, {"source", src}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  // If the entire destination is to be copied, we can optimize out the blend
  if (mask == 0b1111) {
    env->emit_ir<IR_VFMath2Asm>(color, dest, src, kind);
  } else {
    auto temp_reg = env->make_vfr(dest->type());
    // Perform the arithmetic operation on the two vectors into a temporary register
    env->emit_ir<IR_VFMath2Asm>(color, temp_reg, src, kind);
    // Blend the result back into the destination register using the mask
    env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
  }

  return get_none();
}

Val* Compiler::compile_asm_int128_math2_imm_u8(const goos::Object& form,
                                               const goos::Object& rest,
                                               IR_Int128Math2Asm::Kind kind,
                                               Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  s64 imm;
  if (!try_getting_constant_integer(args.unnamed.at(2), &imm, env)) {
    throw_compiler_error(form, "Could not evaluate {} as a compile-time integer.",
                         args.unnamed.at(2).print());
  }

  if (imm < 0 || imm > 255) {
    throw_compiler_error(form, "Immediate {} is invalid. The value {} is out of range for a uint8.",
                         args.unnamed.at(2).print(), imm);
  }

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  // If the entire destination is to be copied, we can optimize out the blend
  if (mask == 0b1111) {
    env->emit_ir<IR_Int128Math2Asm>(color, dest, src, kind, imm);
  } else {
    auto temp_reg = env->make_vfr(dest->type());
    // Perform the arithmetic operation on the two vectors into a temporary register
    env->emit_ir<IR_Int128Math2Asm>(color, temp_reg, src, kind, imm);
    // Blend the result back into the destination register using the mask
    env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
  }

  return get_none();
}

Val* Compiler::compile_asm_pw_sll(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math2_imm_u8(form, rest, IR_Int128Math2Asm::Kind::PW_SLL, env);
}

Val* Compiler::compile_asm_pw_srl(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math2_imm_u8(form, rest, IR_Int128Math2Asm::Kind::PW_SRL, env);
}

Val* Compiler::compile_asm_pw_sra(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math2_imm_u8(form, rest, IR_Int128Math2Asm::Kind::PW_SRA, env);
}

Val* Compiler::compile_asm_por(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::POR, env);
}

Val* Compiler::compile_asm_pnor(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}}, {});

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);  // rs
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);  // rt
  auto temp = env->make_ireg(TypeSpec("uint128"), RegClass::INT_128);

  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot set destination");
  }

  // A NOR is equivalent to an inverted input AND
  // Use the destination register and a temp register.
  // There is also no PNOT instruction, so the easiest way to is to XOR with an all-ones register
  // Remember - we do not want to mutate the source registers!

  // How do we create an all-ones-register? Compare it with itself
  env->emit_ir<IR_Int128Math3Asm>(true, temp, temp, temp, IR_Int128Math3Asm::Kind::PCEQW);
  // Then NOT the first input, store in destination
  env->emit_ir<IR_Int128Math3Asm>(true, dest, temp, src1, IR_Int128Math3Asm::Kind::PXOR);
  // NOT the second input, we not longer require the all-ones-register
  env->emit_ir<IR_Int128Math3Asm>(true, temp, temp, src2, IR_Int128Math3Asm::Kind::PXOR);
  // Preform the AND aka NOR
  env->emit_ir<IR_Int128Math3Asm>(true, dest, dest, temp, IR_Int128Math3Asm::Kind::PAND);

  return get_none();
}

Val* Compiler::compile_asm_pand(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PAND, env);
}

Val* Compiler::compile_asm_pceqb(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCEQB, env);
}

Val* Compiler::compile_asm_pceqh(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCEQH, env);
}

Val* Compiler::compile_asm_pceqw(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCEQW, env);
}

Val* Compiler::compile_asm_pcgtb(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCGTB, env);
}

Val* Compiler::compile_asm_pcgth(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCGTH, env);
}

Val* Compiler::compile_asm_pcgtw(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCGTW, env);
}

Val* Compiler::compile_asm_pextub(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTUB, env);
}

Val* Compiler::compile_asm_pextuh(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTUH, env);
}

Val* Compiler::compile_asm_pextuw(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTUW, env);
}

Val* Compiler::compile_asm_pextlb(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTLB, env);
}

Val* Compiler::compile_asm_pextlh(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTLH, env);
}

Val* Compiler::compile_asm_pextlw(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PEXTLW, env);
}

Val* Compiler::compile_asm_pcpyud(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCPYUD, env);
}

Val* Compiler::compile_asm_pcpyld(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PCPYLD, env);
}

Val* Compiler::compile_asm_psubw(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_int128_math3(form, rest, IR_Int128Math3Asm::Kind::PSUBW, env);
}

Val* Compiler::compile_asm_ppach(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}}, {});

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);  // rs
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);  // rt
  auto temp = env->make_ireg(TypeSpec("uint128"), RegClass::INT_128);

  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot set destination");
  }

  env->emit_ir<IR_Int128Math2Asm>(true, temp, src1, IR_Int128Math2Asm::Kind::VPSHUFLW, 0x88);
  env->emit_ir<IR_Int128Math2Asm>(true, dest, src2, IR_Int128Math2Asm::Kind::VPSHUFLW, 0x88);
  env->emit_ir<IR_Int128Math2Asm>(true, temp, temp, IR_Int128Math2Asm::Kind::VPSHUFHW, 0x88);
  env->emit_ir<IR_Int128Math2Asm>(true, dest, dest, IR_Int128Math2Asm::Kind::VPSHUFHW, 0x88);
  env->emit_ir<IR_Int128Math2Asm>(true, temp, temp, IR_Int128Math2Asm::Kind::VPSRLDQ, 4);
  env->emit_ir<IR_Int128Math2Asm>(true, dest, dest, IR_Int128Math2Asm::Kind::VPSRLDQ, 4);
  // is actually a VPUNPCKLQDQ with srcs swapped.
  env->emit_ir<IR_Int128Math3Asm>(true, dest, temp, dest, IR_Int128Math3Asm::Kind::PCPYLD);

  return get_none();
}

Val* Compiler::compile_asm_xorp(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}}, {});

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);  // rs
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);  // rt

  if (!dest->settable()) {
    throw_compiler_error(form, "Cannot set destination");
  }

  env->emit_ir<IR_VFMath3Asm>(true, dest, src1, src2, IR_VFMath3Asm::Kind::XOR);
  return get_none();
}

Val* Compiler::compile_asm_itof_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math2(form, rest, IR_VFMath2Asm::Kind::ITOF, env);
}

Val* Compiler::compile_asm_ftoi_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math2(form, rest, IR_VFMath2Asm::Kind::FTOI, env);
}

Val* Compiler::compile_asm_xor_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::XOR,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_max_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MAX,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_max_x_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MAX,
                              emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_max_y_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MAX,
                              emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_max_z_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MAX,
                              emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_max_w_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MAX,
                              emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_min_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MIN,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_min_x_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MIN,
                              emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_min_y_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MIN,
                              emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_min_z_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MIN,
                              emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_min_w_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MIN,
                              emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_sub_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::SUB,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_sub_x_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::SUB,
                              emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_sub_y_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::SUB,
                              emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_sub_z_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::SUB,
                              emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_sub_w_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::SUB,
                              emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_add_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::ADD,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_add_x_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::ADD,
                              emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_add_y_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::ADD,
                              emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_add_z_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::ADD,
                              emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_add_w_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::ADD,
                              emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_mul_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MUL,
                              emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_mul_x_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MUL,
                              emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_mul_y_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MUL,
                              emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_mul_z_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MUL,
                              emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_mul_w_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  return compile_asm_vf_math3(form, rest, IR_VFMath3Asm::Kind::MUL,
                              emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_vf_math4_two_operation(const goos::Object& form,
                                                  const goos::Object& rest,
                                                  IR_VFMath3Asm::Kind first_op_kind,
                                                  IR_VFMath3Asm::Kind second_op_kind,
                                                  emitter::Register::VF_ELEMENT broadcastElement,
                                                  Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}, {}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  // This third register is intended for the ACC/Q/ETC, and is used to temporarily store the value
  // that eventually goes into the destination
  //
  // For example VMADDA:
  // > ACC += src1 * src2
  // > DEST = ACC
  auto src3 = compile_error_guard(args.unnamed.at(3), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest},
                           {"first source", src1},
                           {"second source", src2},
                           {"third source", src3}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(form, "The value {} is out of range for a blend mask (0-15 inclusive).",
                           mask);
    }
  }

  // First we clear a temporary register
  auto temp_reg = env->make_vfr(dest->type());

  // If there is a broadcast register, splat that float across the entire src2 register before
  // performing the operation For example vaddx.xyzw vf10, vf20, vf30
  // vf10[x] = vf20[x] + vf30[x]
  // vf10[y] = vf20[y] + vf30[x]
  // vf10[z] = vf20[z] + vf30[x]
  // vf10[w] = vf20[w] + vf30[x]
  if (broadcastElement != emitter::Register::VF_ELEMENT::NONE) {
    env->emit_ir<IR_SplatVF>(color, temp_reg, src2, broadcastElement);

    // Perform the first operation
    env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src1, temp_reg, first_op_kind);

    // If the entire destination is to be copied, we can optimize out the blend
    if (mask == 0b1111) {
      env->emit_ir<IR_VFMath3Asm>(color, dest, src3, temp_reg, second_op_kind);
    } else {
      // Perform the second operation on the two vectors into the temporary register
      env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src3, temp_reg, second_op_kind);
      // Blend the result back into the destination register using the mask
      env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
    }
  } else {
    // Perform the first operation
    env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src1, src2, first_op_kind);

    // If the entire destination is to be copied, we can optimize out the blend
    if (mask == 0b1111) {
      env->emit_ir<IR_VFMath3Asm>(color, dest, src3, temp_reg, second_op_kind);
    } else {
      // Perform the second operation on the two vectors into the temporary register
      env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src3, temp_reg, second_op_kind);
      // Blend the result back into the destination register using the mask
      env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
    }
  }

  return get_none();
}

Val* Compiler::compile_asm_mul_add_vf(const goos::Object& form,
                                      const goos::Object& rest,
                                      Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::ADD,
                                            emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_mul_add_x_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::ADD,
                                            emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_mul_add_y_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::ADD,
                                            emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_mul_add_z_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::ADD,
                                            emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_mul_add_w_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::ADD,
                                            emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_mul_sub_vf(const goos::Object& form,
                                      const goos::Object& rest,
                                      Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::SUB,
                                            emitter::Register::VF_ELEMENT::NONE, env);
}

Val* Compiler::compile_asm_mul_sub_x_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::SUB,
                                            emitter::Register::VF_ELEMENT::X, env);
}

Val* Compiler::compile_asm_mul_sub_y_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::SUB,
                                            emitter::Register::VF_ELEMENT::Y, env);
}

Val* Compiler::compile_asm_mul_sub_z_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::SUB,
                                            emitter::Register::VF_ELEMENT::Z, env);
}

Val* Compiler::compile_asm_mul_sub_w_vf(const goos::Object& form,
                                        const goos::Object& rest,
                                        Env* env) {
  return compile_asm_vf_math4_two_operation(form, rest, IR_VFMath3Asm::Kind::MUL,
                                            IR_VFMath3Asm::Kind::SUB,
                                            emitter::Register::VF_ELEMENT::W, env);
}

Val* Compiler::compile_asm_abs_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"mask", {false, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  check_vector_float_regs(form, env, {{"destination", dest}, {"source", src}});

  u8 mask = 0b1111;
  if (args.has_named("mask")) {
    mask = args.named.at("mask").as_int();
    if (mask > 15) {
      throw_compiler_error(
          form, "The value {} is out of range for a destination mask (0-15 inclusive).", mask);
    }
  }

  // There is no single instruction ABS on AVX, so there are a number of ways to do it manually,
  // this is one of them. For example, assume the original vec = <1, -2, -3, 4>

  // First we clear a temporary register, XOR'ing itself
  auto temp_reg = env->make_vfr(dest->type());
  env->emit_ir<IR_VFMath3Asm>(color, temp_reg, temp_reg, temp_reg, IR_VFMath3Asm::Kind::XOR);

  // Next, find the difference between our source operand and 0, use the same temp register, no need
  // to use another <0, 0, 0, 0> - <1, -2, -3, 4> = <-1, 2, 3, 4>
  env->emit_ir<IR_VFMath3Asm>(color, temp_reg, temp_reg, src, IR_VFMath3Asm::Kind::SUB);

  // Finally, find the maximum between our difference, and the original value
  // MAX_OF(<-1, 2, 3, 4>, <1, -2, -3, 4>) = <1, 2, 3, 4>
  if (mask == 0b1111) {  // If the entire destination is to be copied, we can optimize out the blend
    env->emit_ir<IR_VFMath3Asm>(color, dest, src, temp_reg, IR_VFMath3Asm::Kind::MAX);
  } else {
    env->emit_ir<IR_VFMath3Asm>(color, temp_reg, src, temp_reg, IR_VFMath3Asm::Kind::MAX);

    // Blend the result back into the destination register using the mask
    env->emit_ir<IR_BlendVF>(color, dest, dest, temp_reg, mask);
  }

  return get_none();
}

u8 Compiler::ftf_fsf_to_blend_mask(u8 val) {
  // 00 -> x
  // ...
  // 11 -> w
  return 0b0001 << val;
}

emitter::Register::VF_ELEMENT Compiler::ftf_fsf_to_vector_element(u8 val) {
  // 00 -> x
  // ...
  // 11 -> w
  switch (val) {
    case 0b00:
      return emitter::Register::VF_ELEMENT::X;
    case 0b01:
      return emitter::Register::VF_ELEMENT::Y;
    case 0b10:
      return emitter::Register::VF_ELEMENT::Z;
    case 0b11:
      return emitter::Register::VF_ELEMENT::W;
    default:
      throw_compiler_error_no_code("Invalid vector element {}", (int)val);
      return emitter::Register::VF_ELEMENT::NONE;
  }
}

Val* Compiler::compile_asm_div_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}},
           {
               {"color", {false, goos::ObjectType::SYMBOL}},
               {"fsf", {true, goos::ObjectType::INTEGER}},
               {"ftf", {true, goos::ObjectType::INTEGER}},
           });
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest}, {"first source", src1}, {"second source", src2}});

  u8 fsf = args.named.at("fsf").as_int();
  if (fsf > 3) {
    throw_compiler_error(form, "The value {} is out of range for fsf (0-3 inclusive).", fsf);
  }
  u8 ftf = args.named.at("ftf").as_int();
  if (ftf > 3) {
    throw_compiler_error(form, "The value {} is out of range for ftf (0-3 inclusive).", ftf);
  }

  // VDIV in the VU stores its result in a single 32bit `Q` Register, it does not compute the packed
  // division result
  //
  // Further more, you can mix and match the vector elements (ex. src1's X component divided by
  // src2's Y) Because of this, we need to blend the two components into corresponding locations,
  // perform the divide then place into the cleared dest. register.
  //
  // Why do we even bother using VDIVPS instead of FDIV? Because otherwise in x86, you have to use
  // the FPU stack Registers are nicer.

  // Save one temp reg, use the destination as one
  auto temp_reg = env->make_vfr(dest->type());

  // Splat src1's value into the dest reg, keep it simple, this way no matter which vector component
  // is accessed from the final result will be the correct answer
  env->emit_ir<IR_SplatVF>(color, dest, src1, ftf_fsf_to_vector_element(fsf));
  // Splat src1's value into the the temp reg
  env->emit_ir<IR_SplatVF>(color, temp_reg, src2, ftf_fsf_to_vector_element(ftf));

  // Perform the Division
  env->emit_ir<IR_VFMath3Asm>(color, dest, dest, temp_reg, IR_VFMath3Asm::Kind::DIV);
  return get_none();
}

Val* Compiler::compile_asm_sqrt_vf(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest);
  va_check(
      form, args, {{}, {}},
      {{"color", {false, goos::ObjectType::SYMBOL}}, {"ftf", {true, goos::ObjectType::INTEGER}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  check_vector_float_regs(form, env, {{"destination", dest}, {"source", src}});

  u8 ftf = args.named.at("ftf").as_int();
  if (ftf > 3) {
    throw_compiler_error(form, "The value {} is out of range for ftf (0-3 inclusive).", ftf);
  }

  // VSQRT in the VU stores its result in a single 32bit `Q` Register, it does not compute the
  // packed division result
  //
  // Because of this, we need to blend the relevent component into a cleared register and then
  // perform the SQRT
  //
  // Why do we even bother using VSQRTPS instead of FSQRT? Because otherwise in x86, you have to use
  // the FPU stack Registers are nicer.

  // Splat src's value into the dest reg, keep it simple, this way no matter which vector component
  // is accessed from the final result will be the correct answer
  env->emit_ir<IR_SplatVF>(color, dest, src, ftf_fsf_to_vector_element(ftf));

  env->emit_ir<IR_SqrtVF>(color, dest, dest);
  return get_none();
}

Val* Compiler::compile_asm_inv_sqrt_vf(const goos::Object& form,
                                       const goos::Object& rest,
                                       Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}},
           {
               {"color", {false, goos::ObjectType::SYMBOL}},
               {"fsf", {true, goos::ObjectType::INTEGER}},
               {"ftf", {true, goos::ObjectType::INTEGER}},
           });
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest}, {"first source", src1}, {"second source", src2}});

  u8 fsf = args.named.at("fsf").as_int();
  if (fsf > 3) {
    throw_compiler_error(form, "The value {} is out of range for fsf (0-3 inclusive).", fsf);
  }
  u8 ftf = args.named.at("ftf").as_int();
  if (ftf > 3) {
    throw_compiler_error(form, "The value {} is out of range for ftf (0-3 inclusive).", ftf);
  }

  // Save one temp reg, use the destination as one
  auto temp_reg = env->make_vfr(dest->type());

  // Splat src1's value into the dest reg, keep it simple, this way no matter which vector component
  // is accessed from the final result will be the correct answer
  env->emit_ir<IR_SplatVF>(color, dest, src1, ftf_fsf_to_vector_element(fsf));
  // Splat src1's value into the the temp reg
  env->emit_ir<IR_SplatVF>(color, temp_reg, src2, ftf_fsf_to_vector_element(ftf));
  // Square Root the temp reg
  env->emit_ir<IR_SqrtVF>(color, temp_reg, temp_reg);

  // Perform the Division
  env->emit_ir<IR_VFMath3Asm>(color, dest, dest, temp_reg, IR_VFMath3Asm::Kind::DIV);
  return get_none();
}

Val* Compiler::compile_asm_outer_product_vf(const goos::Object& form,
                                            const goos::Object& rest,
                                            Env* env) {
  auto args = get_va(form, rest);
  va_check(form, args, {{}, {}, {}}, {{"color", {false, goos::ObjectType::SYMBOL}}});
  bool color = true;
  if (args.has_named("color")) {
    color = get_true_or_false(form, args.named.at("color"));
  }

  auto dest = compile_error_guard(args.unnamed.at(0), env)->to_reg(env);
  auto src1 = compile_error_guard(args.unnamed.at(1), env)->to_reg(env);
  auto src2 = compile_error_guard(args.unnamed.at(2), env)->to_reg(env);
  check_vector_float_regs(form, env,
                          {{"destination", dest}, {"first source", src1}, {"second source", src2}});

  // Given 2 vectors V1 = <1,2,3,4> and V2 = <5,6,7,8> and assume VDEST = <0, 0, 0, 999>
  // The outer product is computed like so (only x,y,z components are operated on):
  // x = (V1y * V2z) - (V2y * V1z) => (2 * 7) - (6 * 3) => -4
  // y = (V1z * V2x) - (V2z * V1x) => (3 * 5) - (7 * 1) =>  8
  // z = (V1x * V2y) - (V2x * V1y) => (1 * 6) - (5 * 2) => -4
  // w = N/A, left alone                                => 999
  //
  // There is probably a more optimized alg for this, but we can just do this in two stages
  // First swizzle the first two vectors accordingly, and store in `dest`
  // Then follow up with the second half.
  //
  // Some temporary regs are required AND its important to not modify dest's `w` or the source
  // registers at all

  // Init two temp registers
  auto temp1 = env->make_vfr(dest->type());
  auto temp2 = env->make_vfr(dest->type());
  auto temp_dst = env->make_vfr(dest->type());

  // First Portion
  // - Swizzle src1 appropriately
  env->emit_ir<IR_SwizzleVF>(color, temp1, src1, 0b00001001);
  // - Move it into 'dest' safely (avoid mutating `w`)
  env->emit_ir<IR_BlendVF>(color, temp_dst, temp_dst, temp1, 0b0111);
  // - Swizzle src2 appropriately
  env->emit_ir<IR_SwizzleVF>(color, temp1, src2, 0b00010010);
  // - Multiply - Result in `dest`
  env->emit_ir<IR_VFMath3Asm>(color, temp1, temp_dst, temp1, IR_VFMath3Asm::Kind::MUL);
  // - Move it into 'dest' safely (avoid mutating `w`)
  env->emit_ir<IR_BlendVF>(color, temp_dst, temp_dst, temp1, 0b0111);

  // Second Portion
  // - Swizzle src2 appropriately
  env->emit_ir<IR_SwizzleVF>(color, temp1, src2, 0b00001001);
  // - Swizzle src1 appropriately
  env->emit_ir<IR_SwizzleVF>(color, temp2, src1, 0b00010010);
  // - Multiply - Result in `temp1`
  env->emit_ir<IR_VFMath3Asm>(color, temp1, temp1, temp2, IR_VFMath3Asm::Kind::MUL);

  // Finalize
  // - Subtract
  env->emit_ir<IR_VFMath3Asm>(color, temp2, temp_dst, temp1, IR_VFMath3Asm::Kind::SUB);
  // - Blend result, as to avoid not modifying dest's `w` component
  env->emit_ir<IR_BlendVF>(color, dest, dest, temp2, 0b0111);
  return get_none();
}
