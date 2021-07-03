#include <utility>
#include "IR.h"
#include "goalc/emitter/IGen.h"
#include "third-party/fmt/core.h"
#include "common/symbols.h"

using namespace emitter;

namespace {
Register get_reg(const RegVal* rv, const AllocationResult& allocs, emitter::IR_Record irec) {
  if (rv->rlet_constraint().has_value()) {
    auto& range = allocs.ass_as_ranges;
    auto reg = rv->rlet_constraint().value();
    if (rv->ireg().id < int(range.size())) {
      auto& lr = range.at(rv->ireg().id);
      if (irec.ir_id >= lr.min && irec.ir_id <= lr.max) {
        auto ass_reg = range.at(rv->ireg().id).get(irec.ir_id);
        if (ass_reg.kind == Assignment::Kind::REGISTER) {
          assert(ass_reg.reg == reg);
        } else {
          assert(false);
        }
      } else {
        assert(false);
      }
    } else {
      assert(false);
    }
    return reg;
  } else {
    auto& ass = allocs.ass_as_ranges.at(rv->ireg().id).get(irec.ir_id);
    assert(ass.kind == Assignment::Kind::REGISTER);
    return ass.reg;
  }
}

int get_stack_offset(const RegVal* rv, const AllocationResult& allocs) {
  if (rv->rlet_constraint().has_value()) {
    // should be impossible. Can't take the address of an inline assembly form register.
    assert(false);
  } else {
    assert(rv->forced_on_stack());
    auto& ass = allocs.ass_as_ranges.at(rv->ireg().id);
    auto stack_slot = allocs.get_slot_for_spill(ass.assignment.at(0).stack_slot);
    assert(stack_slot >= 0);
    return stack_slot * 8;
  }
}

Register get_no_color_reg(const RegVal* rv) {
  if (!rv->rlet_constraint().has_value()) {
    throw std::runtime_error(
        "Accessed a non-rlet constrained variable without the coloring system.");
  }
  return rv->rlet_constraint().value();
}

Register get_reg_asm(const RegVal* rv,
                     const AllocationResult& allocs,
                     emitter::IR_Record irec,
                     bool use_coloring) {
  return use_coloring ? get_reg(rv, allocs, irec) : get_no_color_reg(rv);
}

void load_constant(u64 value,
                   emitter::ObjectGenerator* gen,
                   emitter::IR_Record irec,
                   Register dest_reg) {
  s64 svalue = value;
  if (svalue == 0) {
    gen->add_instr(IGen::xor_gpr64_gpr64(dest_reg, dest_reg), irec);
  } else if (svalue > 0) {
    if (svalue < UINT32_MAX) {
      gen->add_instr(IGen::mov_gpr64_u32(dest_reg, value), irec);
    } else {
      // need a real 64 bit load
      gen->add_instr(IGen::mov_gpr64_u64(dest_reg, value), irec);
    }
  } else {
    if (svalue >= INT32_MIN) {
      gen->add_instr(IGen::mov_gpr64_s32(dest_reg, svalue), irec);
    } else {
      // need a real 64 bit load
      gen->add_instr(IGen::mov_gpr64_u64(dest_reg, value), irec);
    }
  }
}

void regset_common(emitter::ObjectGenerator* gen,
                   const AllocationResult& allocs,
                   emitter::IR_Record irec,
                   const RegVal* dst,
                   const RegVal* src,
                   bool use_coloring) {
  auto src_reg = use_coloring ? get_reg(src, allocs, irec) : get_no_color_reg(src);
  auto dst_reg = use_coloring ? get_reg(dst, allocs, irec) : get_no_color_reg(dst);
  auto src_class = src->ireg().reg_class;
  auto dst_class = dst->ireg().reg_class;

  bool src_is_xmm128 = (src_class == RegClass::VECTOR_FLOAT || src_class == RegClass::INT_128);
  bool dst_is_xmm128 = (dst_class == RegClass::VECTOR_FLOAT || dst_class == RegClass::INT_128);

  if (src_class == RegClass::GPR_64 && dst_class == RegClass::GPR_64) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->add_instr(IGen::null(), irec);
    } else {
      gen->add_instr(IGen::mov_gpr64_gpr64(dst_reg, src_reg), irec);
    }
  } else if (src_class == RegClass::FLOAT && dst_class == RegClass::FLOAT) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->add_instr(IGen::null(), irec);
    } else {
      gen->add_instr(IGen::mov_xmm32_xmm32(dst_reg, src_reg), irec);
    }
  } else if (src_is_xmm128 && dst_is_xmm128) {
    if (src_reg == dst_reg) {
      // eliminate move
      gen->add_instr(IGen::null(), irec);
    } else {
      gen->add_instr(IGen::mov_vf_vf(dst_reg, src_reg), irec);
    }
  } else if (src_class == RegClass::FLOAT && dst_class == RegClass::GPR_64) {
    // xmm 1x -> gpr
    gen->add_instr(IGen::movd_gpr32_xmm32(dst_reg, src_reg), irec);
  } else if (src_class == RegClass::GPR_64 && dst_class == RegClass::FLOAT) {
    // gpr -> xmm 1x
    gen->add_instr(IGen::movd_xmm32_gpr32(dst_reg, src_reg), irec);
  } else if (src_is_xmm128 && dst_class == RegClass::FLOAT) {
    gen->add_instr(IGen::mov_xmm32_xmm32(dst_reg, src_reg), irec);
  } else if (src_class == RegClass::FLOAT && dst_is_xmm128) {
    gen->add_instr(IGen::mov_xmm32_xmm32(dst_reg, src_reg), irec);
  } else if (src_class == RegClass::GPR_64 && dst_is_xmm128) {
    gen->add_instr(IGen::movq_xmm64_gpr64(dst_reg, src_reg), irec);
  } else if (src_is_xmm128 && dst_class == RegClass::GPR_64) {
    gen->add_instr(IGen::movq_gpr64_xmm64(dst_reg, src_reg), irec);
  } else {
    assert(false);  // unhandled move.
  }
}
}  // namespace

///////////
// Return
///////////
IR_Return::IR_Return(const RegVal* return_reg, const RegVal* value, emitter::Register ret_reg)
    : m_return_reg(return_reg), m_value(value), m_ret_reg(ret_reg) {}
std::string IR_Return::print() {
  return fmt::format("ret {} {}", m_return_reg->print(), m_value->print());
}

RegAllocInstr IR_Return::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_return_reg->ireg());
  rai.read.push_back(m_value->ireg());
  if (m_value->ireg().reg_class == m_return_reg->ireg().reg_class) {
    rai.is_move = true;  // only true if we aren't moving from register kind to register kind
  }
  return rai;
}

void IR_Return::add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
  IRegConstraint c;
  if (dynamic_cast<const None*>(m_return_reg)) {
    return;
  }

  c.ireg = m_return_reg->ireg();
  c.instr_idx = my_id;
  c.desired_register = m_ret_reg;
  constraints->push_back(c);
}

void IR_Return::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  auto val_reg = get_reg(m_value, allocs, irec);
  auto dest_reg = get_reg(m_return_reg, allocs, irec);

  if (val_reg == dest_reg) {
    gen->add_instr(IGen::null(), irec);
  } else {
    regset_common(gen, allocs, irec, m_return_reg, m_value, true);
    // gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, val_reg), irec);
  }
}

/////////////////////
// LoadConstant64
/////////////////////
IR_LoadConstant64::IR_LoadConstant64(const RegVal* dest, u64 value)
    : m_dest(dest), m_value(value) {}

std::string IR_LoadConstant64::print() {
  return fmt::format("mov-ic {}, {}", m_dest->print(), m_value);
}

RegAllocInstr IR_LoadConstant64::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_LoadConstant64::do_codegen(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  load_constant(m_value, gen, irec, dest_reg);
}

/////////////////////
// LoadSymbolPointer
/////////////////////
IR_LoadSymbolPointer::IR_LoadSymbolPointer(const RegVal* dest, std::string name)
    : m_dest(dest), m_name(std::move(name)) {}

std::string IR_LoadSymbolPointer::print() {
  return fmt::format("mov-symptr {}, '{}", m_dest->print(), m_name);
}

RegAllocInstr IR_LoadSymbolPointer::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_LoadSymbolPointer::do_codegen(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  if (m_name == "#f") {
    static_assert(FIX_SYM_FALSE == 0, "false symbol location");
    gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, gRegInfo.get_st_reg()), irec);
  } else if (m_name == "#t") {
    gen->add_instr(IGen::lea_reg_plus_off8(dest_reg, gRegInfo.get_st_reg(), FIX_SYM_TRUE), irec);
  } else if (m_name == "_empty_") {
    gen->add_instr(IGen::lea_reg_plus_off8(dest_reg, gRegInfo.get_st_reg(), FIX_SYM_EMPTY_PAIR),
                   irec);
  } else {
    auto instr =
        gen->add_instr(IGen::lea_reg_plus_off32(dest_reg, gRegInfo.get_st_reg(), 0x0afecafe), irec);
    gen->link_instruction_symbol_ptr(instr, m_name);
  }
}

/////////////////////
// SetSymbolValue
/////////////////////

IR_SetSymbolValue::IR_SetSymbolValue(const SymbolVal* dest, const RegVal* src)
    : m_dest(dest), m_src(src) {}

std::string IR_SetSymbolValue::print() {
  return fmt::format("mov '{}, {}", m_dest->name(), m_src->print());
}

RegAllocInstr IR_SetSymbolValue::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  return rai;
}

void IR_SetSymbolValue::do_codegen(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto src_reg = get_reg(m_src, allocs, irec);
  auto instr =
      gen->add_instr(IGen::store32_gpr64_gpr64_plus_gpr64_plus_s32(
                         gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), src_reg, 0x0badbeef),
                     irec);
  gen->link_instruction_symbol_mem(instr, m_dest->name());
}

/////////////////////
// GetSymbolValue
/////////////////////

IR_GetSymbolValue::IR_GetSymbolValue(const RegVal* dest, const SymbolVal* src, bool sext)
    : m_dest(dest), m_src(src), m_sext(sext) {}

std::string IR_GetSymbolValue::print() {
  return fmt::format("mov {}, '{}", m_dest->print(), m_src->name());
}

RegAllocInstr IR_GetSymbolValue::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_GetSymbolValue::do_codegen(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dst_reg = get_reg(m_dest, allocs, irec);
  if (m_sext) {
    auto instr =
        gen->add_instr(IGen::load32s_gpr64_gpr64_plus_gpr64_plus_s32(
                           dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), 0x0badbeef),
                       irec);
    gen->link_instruction_symbol_mem(instr, m_src->name());
  } else {
    auto instr =
        gen->add_instr(IGen::load32u_gpr64_gpr64_plus_gpr64_plus_s32(
                           dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), 0x0badbeef),
                       irec);
    gen->link_instruction_symbol_mem(instr, m_src->name());
  }
}

/////////////////////
// RegSet
/////////////////////

IR_RegSet::IR_RegSet(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

RegAllocInstr IR_RegSet::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_src->ireg());
  if (m_dest->ireg().reg_class == m_src->ireg().reg_class) {
    rai.is_move = true;  // only true if we aren't moving from register kind to register kind
  }
  return rai;
}

void IR_RegSet::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  regset_common(gen, allocs, irec, m_dest, m_src, true);
}

std::string IR_RegSet::print() {
  return fmt::format("mov {}, {}", m_dest->print(), m_src->print());
}

/////////////////////
// GotoLabel
/////////////////////

IR_GotoLabel::IR_GotoLabel(const Label* dest) : m_dest(dest) {
  m_resolved = true;
}

IR_GotoLabel::IR_GotoLabel() {
  m_resolved = false;
}

std::string IR_GotoLabel::print() {
  return fmt::format("goto {}", m_dest->print());
}

RegAllocInstr IR_GotoLabel::to_rai() {
  assert(m_resolved);
  RegAllocInstr rai;
  rai.jumps.push_back(m_dest->idx);
  rai.fallthrough = false;
  return rai;
}

void IR_GotoLabel::do_codegen(emitter::ObjectGenerator* gen,
                              const AllocationResult& allocs,
                              emitter::IR_Record irec) {
  (void)allocs;
  auto instr = gen->add_instr(IGen::jmp_32(), irec);
  gen->link_instruction_jump(instr, gen->get_future_ir_record_in_same_func(irec, m_dest->idx));
}

void IR_GotoLabel::resolve(const Label* dest) {
  assert(!m_resolved);
  m_dest = dest;
  m_resolved = true;
}

/////////////////////
// FunctionCall
/////////////////////

IR_FunctionCall::IR_FunctionCall(const RegVal* func,
                                 const RegVal* ret,
                                 std::vector<RegVal*> args,
                                 std::vector<emitter::Register> arg_regs,
                                 std::optional<emitter::Register> ret_reg)
    : m_func(func),
      m_ret(ret),
      m_args(std::move(args)),
      m_arg_regs(std::move(arg_regs)),
      m_ret_reg(ret_reg) {}

std::string IR_FunctionCall::print() {
  std::string result = fmt::format("call {} (ret {}) (args ", m_func->print(), m_ret->print());
  for (const auto& x : m_args) {
    result += fmt::format("{} ", x->print());
  }
  result.pop_back();
  result.push_back(')');
  return result;
}

RegAllocInstr IR_FunctionCall::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_func->ireg());
  rai.write.push_back(m_func->ireg());  // todo, can we avoid this?
  rai.write.push_back(m_ret->ireg());
  for (auto& arg : m_args) {
    rai.read.push_back(arg->ireg());
  }

  for (int i = 0; i < emitter::RegisterInfo::N_REGS; i++) {
    auto info = emitter::gRegInfo.get_info(i);
    if (info.temp()) {
      rai.clobber.emplace_back(i);
    }
  }

  return rai;
}

void IR_FunctionCall::add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
  for (size_t i = 0; i < m_args.size(); i++) {
    IRegConstraint c;
    c.ireg = m_args.at(i)->ireg();
    c.instr_idx = my_id;
    c.desired_register = m_arg_regs.at(i);
    constraints->push_back(c);
  }

  if (m_ret_reg) {
    IRegConstraint c;
    c.ireg = m_ret->ireg();
    c.desired_register = *m_ret_reg;
    c.instr_idx = my_id;
    constraints->push_back(c);
  }
}

void IR_FunctionCall::do_codegen(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  auto freg = get_reg(m_func, allocs, irec);
  gen->add_instr(IGen::add_gpr64_gpr64(freg, emitter::gRegInfo.get_offset_reg()), irec);
  gen->add_instr(IGen::call_r64(freg), irec);
  // todo, can we do a sub to undo the modification to the register? does that actually work?
}

/////////////////////
// RegValAddr
/////////////////////

IR_RegValAddr::IR_RegValAddr(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_RegValAddr::print() {
  return fmt::format("mov {}, &{}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_RegValAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  // we don't actually read the value in m_src, so we don't need to add it here.
  return rai;
}

void IR_RegValAddr::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  int stack_offset = get_stack_offset(m_src, allocs);
  auto dst = get_reg(m_dest, allocs, irec);
  // x86 pointer to var
  gen->add_instr(IGen::lea_reg_plus_off(dst, RSP, stack_offset), irec);
  // x86 -> GOAL pointer
  gen->add_instr(IGen::sub_gpr64_gpr64(dst, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// StaticVarAddr
/////////////////////

IR_StaticVarAddr::IR_StaticVarAddr(const RegVal* dest, const StaticObject* src)
    : m_dest(dest), m_src(src) {}

std::string IR_StaticVarAddr::print() {
  return fmt::format("mov-sva {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_StaticVarAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_StaticVarAddr::do_codegen(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(dr, 0), irec);
  gen->link_instruction_static(instr, m_src->rec, m_src->get_addr_offset());
  gen->add_instr(IGen::sub_gpr64_gpr64(dr, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// FunctionAddr
/////////////////////

IR_FunctionAddr::IR_FunctionAddr(const RegVal* dest, FunctionEnv* src) : m_dest(dest), m_src(src) {}

std::string IR_FunctionAddr::print() {
  return fmt::format("mov-fa {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_FunctionAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_FunctionAddr::do_codegen(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  auto dr = get_reg(m_dest, allocs, irec);
  auto instr = gen->add_instr(IGen::static_addr(dr, 0), irec);
  gen->link_instruction_to_function(instr, gen->get_existing_function_record(m_src->idx_in_file));
  gen->add_instr(IGen::sub_gpr64_gpr64(dr, emitter::gRegInfo.get_offset_reg()), irec);
}

/////////////////////
// IntegerMath
/////////////////////

IR_IntegerMath::IR_IntegerMath(IntegerMathKind kind, RegVal* dest, RegVal* arg)
    : m_kind(kind), m_dest(dest), m_arg(arg) {}

IR_IntegerMath::IR_IntegerMath(IntegerMathKind kind, RegVal* dest, u8 shift_amount)
    : m_kind(kind), m_dest(dest), m_shift_amount(shift_amount) {}

std::string IR_IntegerMath::print() {
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      return fmt::format("addi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SUB_64:
      return fmt::format("subi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMUL_32:
      return fmt::format("imul {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMUL_64:
      return fmt::format("imul64 {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IDIV_32:
      return fmt::format("idiv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::UDIV_32:
      return fmt::format("udiv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMOD_32:
      return fmt::format("imod {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SARV_64:
      return fmt::format("sarv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SHLV_64:
      return fmt::format("shlv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SHRV_64:
      return fmt::format("shrv {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SAR_64:
      return fmt::format("sar {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::SHL_64:
      return fmt::format("shl {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::SHR_64:
      return fmt::format("shr {}, {}", m_dest->print(), m_shift_amount);
    case IntegerMathKind::AND_64:
      return fmt::format("and {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::OR_64:
      return fmt::format("or {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::XOR_64:
      return fmt::format("xor {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::NOT_64:
      return fmt::format("not {}", m_dest->print());
    default:
      throw std::runtime_error("Unsupported IntegerMathKind");
  }
}

RegAllocInstr IR_IntegerMath::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_dest->ireg());

  if (m_kind != IntegerMathKind::NOT_64 && m_kind != IntegerMathKind::SHL_64 &&
      m_kind != IntegerMathKind::SAR_64 && m_kind != IntegerMathKind::SHR_64) {
    rai.read.push_back(m_arg->ireg());
  }

  if (m_kind == IntegerMathKind::IDIV_32 || m_kind == IntegerMathKind::IMOD_32 ||
      m_kind == IntegerMathKind::UDIV_32) {
    rai.exclude.emplace_back(emitter::RDX);
  }
  return rai;
}

void IR_IntegerMath::do_codegen(emitter::ObjectGenerator* gen,
                                const AllocationResult& allocs,
                                emitter::IR_Record irec) {
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      gen->add_instr(
          IGen::add_gpr64_gpr64(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case IntegerMathKind::SUB_64:
      gen->add_instr(
          IGen::sub_gpr64_gpr64(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case IntegerMathKind::AND_64:
      gen->add_instr(
          IGen::and_gpr64_gpr64(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case IntegerMathKind::OR_64:
      gen->add_instr(
          IGen::or_gpr64_gpr64(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case IntegerMathKind::XOR_64:
      gen->add_instr(
          IGen::xor_gpr64_gpr64(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case IntegerMathKind::NOT_64:
      gen->add_instr(IGen::not_gpr64(get_reg(m_dest, allocs, irec)), irec);
      assert(!m_arg);
      break;
    case IntegerMathKind::SHLV_64:
      gen->add_instr(IGen::shl_gpr64_cl(get_reg(m_dest, allocs, irec)), irec);
      assert(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SHRV_64:
      gen->add_instr(IGen::shr_gpr64_cl(get_reg(m_dest, allocs, irec)), irec);
      assert(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SARV_64:
      gen->add_instr(IGen::sar_gpr64_cl(get_reg(m_dest, allocs, irec)), irec);
      assert(get_reg(m_arg, allocs, irec) == emitter::RCX);
      break;
    case IntegerMathKind::SHL_64:
      gen->add_instr(IGen::shl_gpr64_u8(get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SHR_64:
      gen->add_instr(IGen::shr_gpr64_u8(get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::SAR_64:
      gen->add_instr(IGen::sar_gpr64_u8(get_reg(m_dest, allocs, irec), m_shift_amount), irec);
      break;
    case IntegerMathKind::IMUL_32: {
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr32_gpr32(dr, get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(dr, dr), irec);
    } break;
    case IntegerMathKind::IMUL_64: {
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr64_gpr64(dr, get_reg(m_arg, allocs, irec)), irec);
    } break;
    case IntegerMathKind::IDIV_32: {
      gen->add_instr(IGen::cdq(), irec);
      gen->add_instr(IGen::idiv_gpr32(get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(get_reg(m_dest, allocs, irec), emitter::RAX), irec);
    } break;
    case IntegerMathKind::UDIV_32: {
      // zero extend, not sign extend to avoid overflow
      gen->add_instr(IGen::xor_gpr64_gpr64(Register(RDX), Register(RDX)), irec);
      gen->add_instr(IGen::unsigned_div_gpr32(get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(get_reg(m_dest, allocs, irec), emitter::RAX), irec);
    } break;
    case IntegerMathKind::IMOD_32: {
      gen->add_instr(IGen::cdq(), irec);
      gen->add_instr(IGen::idiv_gpr32(get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(get_reg(m_dest, allocs, irec), emitter::RDX), irec);
    } break;

    default:
      assert(false);
  }
}

/////////////////////
// FloatMath
/////////////////////

IR_FloatMath::IR_FloatMath(FloatMathKind kind, RegVal* dest, RegVal* arg)
    : m_kind(kind), m_dest(dest), m_arg(arg) {}

std::string IR_FloatMath::print() {
  switch (m_kind) {
    case FloatMathKind::DIV_SS:
      return fmt::format("divss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MUL_SS:
      return fmt::format("mulss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::ADD_SS:
      return fmt::format("addss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::SUB_SS:
      return fmt::format("subss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MAX_SS:
      return fmt::format("maxss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::MIN_SS:
      return fmt::format("minss {}, {}", m_dest->print(), m_arg->print());
    case FloatMathKind::SQRT_SS:
      return fmt::format("sqrtss {}, {}", m_dest->print(), m_arg->print());
    default:
      throw std::runtime_error("Unsupported FloatMathKind");
  }
}

RegAllocInstr IR_FloatMath::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  if (m_kind != FloatMathKind::SQRT_SS) {
    rai.read.push_back(m_dest->ireg());
  }
  rai.read.push_back(m_arg->ireg());
  return rai;
}

void IR_FloatMath::do_codegen(emitter::ObjectGenerator* gen,
                              const AllocationResult& allocs,
                              emitter::IR_Record irec) {
  switch (m_kind) {
    case FloatMathKind::DIV_SS:
      gen->add_instr(
          IGen::divss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::MUL_SS:
      gen->add_instr(
          IGen::mulss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::ADD_SS:
      gen->add_instr(
          IGen::addss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::SUB_SS:
      gen->add_instr(
          IGen::subss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::MAX_SS:
      gen->add_instr(
          IGen::maxss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::MIN_SS:
      gen->add_instr(
          IGen::minss_xmm_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)), irec);
      break;
    case FloatMathKind::SQRT_SS:
      gen->add_instr(IGen::sqrts_xmm(get_reg(m_dest, allocs, irec), get_reg(m_arg, allocs, irec)),
                     irec);
      break;
    default:
      assert(false);
  }
}

/////////////////////
// StaticVarLoad
/////////////////////

IR_StaticVarLoad::IR_StaticVarLoad(const RegVal* dest, const StaticObject* src)
    : m_dest(dest), m_src(src) {}

std::string IR_StaticVarLoad::print() {
  return fmt::format("mov-svl {}, [{}]", m_dest->print(), m_src->print());
}

RegAllocInstr IR_StaticVarLoad::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_StaticVarLoad::do_codegen(emitter::ObjectGenerator* gen,
                                  const AllocationResult& allocs,
                                  emitter::IR_Record irec) {
  auto load_info = m_src->get_load_info();
  assert(m_src->get_addr_offset() == 0);

  if (m_dest->ireg().reg_class == RegClass::FLOAT) {
    assert(load_info.load_signed == false);
    assert(load_info.load_size == 4);
    assert(load_info.requires_load == true);

    auto instr = gen->add_instr(IGen::static_load_xmm32(get_reg(m_dest, allocs, irec), 0), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
  } else if (m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT) {
    // we don't check the load info intentionally because we want to allow loading an entire
    // vector structure.
    auto instr = gen->add_instr(IGen::loadvf_rip_plus_s32(get_reg(m_dest, allocs, irec), 0), irec);
    gen->link_instruction_static(instr, m_src->rec, 0);
  } else {
    assert(false);
  }
}

/////////////////////
// ConditionalBranch
/////////////////////

std::string Condition::print() const {
  switch (kind) {
    case ConditionKind::NOT_EQUAL:
      return a->print() + " != " + b->print();
    case ConditionKind::EQUAL:
      return a->print() + " == " + b->print();
    case ConditionKind::LEQ:
      return a->print() + " <= " + b->print();
    case ConditionKind::GEQ:
      return a->print() + " >= " + b->print();
    case ConditionKind::LT:
      return a->print() + " < " + b->print();
    case ConditionKind::GT:
      return a->print() + " > " + b->print();
    default:
      throw std::runtime_error("unknown condition type in GoalCondition::print()");
  }
}

RegAllocInstr Condition::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(a->ireg());
  rai.read.push_back(b->ireg());
  return rai;
}

IR_ConditionalBranch::IR_ConditionalBranch(const Condition& _condition, Label _label)
    : condition(_condition), label(_label) {}

std::string IR_ConditionalBranch::print() {
  // todo, float/signed info?
  return fmt::format("j({}) {}", condition.print(), label.print());
}

RegAllocInstr IR_ConditionalBranch::to_rai() {
  auto rai = condition.to_rai();
  assert(m_resolved);
  rai.jumps.push_back(label.idx);
  return rai;
}

void IR_ConditionalBranch::do_codegen(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  Instruction jump_instr(0);
  assert(m_resolved);
  switch (condition.kind) {
    case ConditionKind::EQUAL:
      jump_instr = IGen::je_32();
      break;
    case ConditionKind::NOT_EQUAL:
      jump_instr = IGen::jne_32();
      break;
    case ConditionKind::LEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jle_32();
      } else {
        jump_instr = IGen::jbe_32();
      }
      break;
    case ConditionKind::GEQ:
      if (condition.is_signed) {
        jump_instr = IGen::jge_32();
      } else {
        jump_instr = IGen::jae_32();
      }
      break;

    case ConditionKind::LT:
      if (condition.is_signed) {
        jump_instr = IGen::jl_32();
      } else {
        jump_instr = IGen::jb_32();
      }
      break;
    case ConditionKind::GT:
      if (condition.is_signed) {
        jump_instr = IGen::jg_32();
      } else {
        jump_instr = IGen::ja_32();
      }
      break;
    default:
      assert(false);
  }

  if (condition.is_float) {
    gen->add_instr(
        IGen::cmp_flt_flt(get_reg(condition.a, allocs, irec), get_reg(condition.b, allocs, irec)),
        irec);
  } else {
    gen->add_instr(IGen::cmp_gpr64_gpr64(get_reg(condition.a, allocs, irec),
                                         get_reg(condition.b, allocs, irec)),
                   irec);
  }

  auto jump_rec = gen->add_instr(jump_instr, irec);
  gen->link_instruction_jump(jump_rec, gen->get_future_ir_record_in_same_func(irec, label.idx));
}

/////////////////////
// LoadConstantOffset
/////////////////////

IR_LoadConstOffset::IR_LoadConstOffset(const RegVal* dest,
                                       int offset,
                                       const RegVal* base,
                                       MemLoadInfo info,
                                       bool use_coloring)
    : IR_Asm(use_coloring), m_dest(dest), m_offset(offset), m_base(base), m_info(info) {}

std::string IR_LoadConstOffset::print() {
  return fmt::format("mov {}, [{} + {}]", m_dest->print(), m_base->print(), m_offset);
}

RegAllocInstr IR_LoadConstOffset::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_base->ireg());
  return rai;
}

void IR_LoadConstOffset::do_codegen(emitter::ObjectGenerator* gen,
                                    const AllocationResult& allocs,
                                    emitter::IR_Record irec) {
  auto dest_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);

  if (m_dest->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::load_goal_gpr(dest_reg, base_reg, emitter::gRegInfo.get_offset_reg(),
                                       m_offset, m_info.size, m_info.sign_extend),
                   irec);
  } else if (m_dest->ireg().reg_class == RegClass::FLOAT && m_info.size == 4 &&
             m_info.sign_extend == false && m_info.reg == RegClass::FLOAT) {
    gen->add_instr(
        IGen::load_goal_xmm32(dest_reg, base_reg, emitter::gRegInfo.get_offset_reg(), m_offset),
        irec);
  } else if ((m_dest->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_dest->ireg().reg_class == RegClass::INT_128) &&
             m_info.size == 16 && m_info.sign_extend == false &&
             m_info.reg == m_dest->ireg().reg_class) {
    gen->add_instr(
        IGen::load_goal_xmm128(dest_reg, base_reg, emitter::gRegInfo.get_offset_reg(), m_offset),
        irec);
  } else {
    throw std::runtime_error("IR_LoadConstOffset::do_codegen not supported");
  }
}

///////////////////////
// StoreConstantOffset
///////////////////////
IR_StoreConstOffset::IR_StoreConstOffset(const RegVal* value,
                                         int offset,
                                         const RegVal* base,
                                         int size,
                                         bool use_coloring)
    : IR_Asm(use_coloring), m_value(value), m_offset(offset), m_base(base), m_size(size) {}

std::string IR_StoreConstOffset::print() {
  return fmt::format("move [{} + {}], {}", m_base->print(), m_offset, m_value->print());
}

RegAllocInstr IR_StoreConstOffset::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_value->ireg());
  rai.read.push_back(m_base->ireg());
  return rai;
}

void IR_StoreConstOffset::do_codegen(emitter::ObjectGenerator* gen,
                                     const AllocationResult& allocs,
                                     emitter::IR_Record irec) {
  auto base_reg = m_use_coloring ? get_reg(m_base, allocs, irec) : get_no_color_reg(m_base);
  auto value_reg = m_use_coloring ? get_reg(m_value, allocs, irec) : get_no_color_reg(m_value);

  if (m_value->ireg().reg_class == RegClass::GPR_64) {
    gen->add_instr(IGen::store_goal_gpr(base_reg, value_reg, emitter::gRegInfo.get_offset_reg(),
                                        m_offset, m_size),
                   irec);
  } else if (m_value->ireg().reg_class == RegClass::FLOAT && m_size == 4) {
    gen->add_instr(
        IGen::store_goal_xmm32(base_reg, value_reg, emitter::gRegInfo.get_offset_reg(), m_offset),
        irec);
  } else if ((m_value->ireg().reg_class == RegClass::VECTOR_FLOAT ||
              m_value->ireg().reg_class == RegClass::INT_128) &&
             m_size == 16) {
    gen->add_instr(
        IGen::store_goal_vf(base_reg, value_reg, emitter::gRegInfo.get_offset_reg(), m_offset),
        irec);
  } else {
    throw std::runtime_error("IR_StoreConstOffset::do_codegen can't handle this");
  }
}

///////////////////////
// Null
///////////////////////
std::string IR_Null::print() {
  return "null";
}

RegAllocInstr IR_Null::to_rai() {
  return {};
}

void IR_Null::do_codegen(emitter::ObjectGenerator* gen,
                         const AllocationResult& allocs,
                         emitter::IR_Record irec) {
  (void)gen;
  (void)allocs;
  (void)irec;
}

///////////////////////
// ValueReset
///////////////////////
IR_ValueReset::IR_ValueReset(std::vector<RegVal*> args) : m_args(std::move(args)) {}

std::string IR_ValueReset::print() {
  return "value-reset";
}

RegAllocInstr IR_ValueReset::to_rai() {
  RegAllocInstr rai;
  for (auto& x : m_args) {
    rai.write.push_back(x->ireg());
  }
  return rai;
}

void IR_ValueReset::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  (void)gen;
  (void)allocs;
  (void)irec;
}

///////////////////////
// FloatToInt
///////////////////////

IR_FloatToInt::IR_FloatToInt(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_FloatToInt::print() {
  return fmt::format("f2i {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_FloatToInt::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_FloatToInt::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  gen->add_instr(IGen::float_to_int32(get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)),
                 irec);
}

///////////////////////
// IntToFloat
///////////////////////

IR_IntToFloat::IR_IntToFloat(const RegVal* dest, const RegVal* src) : m_dest(dest), m_src(src) {}

std::string IR_IntToFloat::print() {
  return fmt::format("i2f {}, {}", m_dest->print(), m_src->print());
}

RegAllocInstr IR_IntToFloat::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_src->ireg());
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_IntToFloat::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  gen->add_instr(IGen::int32_to_float(get_reg(m_dest, allocs, irec), get_reg(m_src, allocs, irec)),
                 irec);
}

///////////////////////
// GetStackAddr
///////////////////////

IR_GetStackAddr::IR_GetStackAddr(const RegVal* dest, int slot) : m_dest(dest), m_slot(slot) {}

std::string IR_GetStackAddr::print() {
  return fmt::format("mov {}, stack-slot-{}", m_dest->print(), m_slot);
}

RegAllocInstr IR_GetStackAddr::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  return rai;
}

void IR_GetStackAddr::do_codegen(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  auto dest_reg = get_reg(m_dest, allocs, irec);
  int offset = GPR_SIZE * allocs.get_slot_for_var(m_slot);

  if (offset == 0) {
    gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, RSP), irec);
    gen->add_instr(IGen::sub_gpr64_gpr64(dest_reg, gRegInfo.get_offset_reg()), irec);
  } else {
    // dest = offset + RSP
    gen->add_instr(IGen::lea_reg_plus_off(dest_reg, RSP, offset), irec);
    // dest = offset + RSP - offset
    gen->add_instr(IGen::sub_gpr64_gpr64(dest_reg, gRegInfo.get_offset_reg()), irec);
  }
}

///////////////////////
// Nop
///////////////////////

IR_Nop::IR_Nop() {}

std::string IR_Nop::print() {
  return fmt::format("nop");
}

RegAllocInstr IR_Nop::to_rai() {
  return {};
}

void IR_Nop::do_codegen(emitter::ObjectGenerator* gen,
                        const AllocationResult&,
                        emitter::IR_Record irec) {
  gen->add_instr(IGen::nop(), irec);
}

///////////////////////
// Asm
///////////////////////

IR_Asm::IR_Asm(bool use_coloring) : m_use_coloring(use_coloring) {}

std::string IR_Asm::get_color_suffix_string() {
  if (m_use_coloring) {
    return "";
  } else {
    return " :no-color";
  }
}

///////////////////////
// AsmRet
///////////////////////

IR_AsmRet::IR_AsmRet(bool use_coloring) : IR_Asm(use_coloring) {}

std::string IR_AsmRet::print() {
  return fmt::format(".ret{}", get_color_suffix_string());
}

RegAllocInstr IR_AsmRet::to_rai() {
  return {};
}

void IR_AsmRet::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::ret(), irec);
}

///////////////////////
// AsmFNop
///////////////////////

IR_AsmFNop::IR_AsmFNop() : IR_Asm(false) {}

std::string IR_AsmFNop::print() {
  return ".nop.vf";
}

RegAllocInstr IR_AsmFNop::to_rai() {
  return {};
}

void IR_AsmFNop::do_codegen(emitter::ObjectGenerator* gen,
                            const AllocationResult& allocs,
                            emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::nop_vf(), irec);
}

///////////////////////
// AsmFWait
///////////////////////

IR_AsmFWait::IR_AsmFWait() : IR_Asm(false) {}

std::string IR_AsmFWait::print() {
  return ".wait.vf";
}

RegAllocInstr IR_AsmFWait::to_rai() {
  return {};
}

void IR_AsmFWait::do_codegen(emitter::ObjectGenerator* gen,
                             const AllocationResult& allocs,
                             emitter::IR_Record irec) {
  (void)allocs;
  gen->add_instr(IGen::wait_vf(), irec);
}

///////////////////////
// AsmPush
///////////////////////

IR_AsmPush::IR_AsmPush(bool use_coloring, const RegVal* src) : IR_Asm(use_coloring), m_src(src) {}

std::string IR_AsmPush::print() {
  return fmt::format(".push{} {}", get_color_suffix_string(), m_src->print());
}

RegAllocInstr IR_AsmPush::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmPush::do_codegen(emitter::ObjectGenerator* gen,
                            const AllocationResult& allocs,
                            emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(IGen::push_gpr64(get_reg(m_src, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::push_gpr64(get_no_color_reg(m_src)), irec);
  }
}

///////////////////////
// AsmPop
///////////////////////

IR_AsmPop::IR_AsmPop(bool use_coloring, const RegVal* dst) : IR_Asm(use_coloring), m_dst(dst) {}

std::string IR_AsmPop::print() {
  return fmt::format(".pop{} {}", get_color_suffix_string(), m_dst->print());
}

RegAllocInstr IR_AsmPop::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
  }
  return rai;
}

void IR_AsmPop::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(IGen::pop_gpr64(get_reg(m_dst, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::pop_gpr64(get_no_color_reg(m_dst)), irec);
  }
}

///////////////////////
// AsmSub
///////////////////////

IR_AsmSub::IR_AsmSub(bool use_coloring, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_coloring), m_dst(dst), m_src(src) {}

std::string IR_AsmSub::print() {
  return fmt::format(".sub{} {}, {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_AsmSub::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmSub::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(
        IGen::sub_gpr64_gpr64(get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::sub_gpr64_gpr64(get_no_color_reg(m_dst), get_no_color_reg(m_src)), irec);
  }
}

///////////////////////
// AsmAdd
///////////////////////

IR_AsmAdd::IR_AsmAdd(bool use_coloring, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_coloring), m_dst(dst), m_src(src) {}

std::string IR_AsmAdd::print() {
  return fmt::format(".add{} {}, {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_AsmAdd::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_AsmAdd::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  if (m_use_coloring) {
    gen->add_instr(
        IGen::add_gpr64_gpr64(get_reg(m_dst, allocs, irec), get_reg(m_src, allocs, irec)), irec);
  } else {
    gen->add_instr(IGen::add_gpr64_gpr64(get_no_color_reg(m_dst), get_no_color_reg(m_src)), irec);
  }
}

///////////////////////
// AsmGetSymbolValue
///////////////////////

IR_GetSymbolValueAsm::IR_GetSymbolValueAsm(bool use_coloring,
                                           const RegVal* dest,
                                           std::string sym_name,
                                           bool sext)
    : IR_Asm(use_coloring), m_dest(dest), m_sym_name(std::move(sym_name)), m_sext(sext) {}

std::string IR_GetSymbolValueAsm::print() {
  return fmt::format(".load-sym{} {} [{}]", get_color_suffix_string(), m_dest->print(), m_sym_name);
}

RegAllocInstr IR_GetSymbolValueAsm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dest->ireg());
  }
  return rai;
}

void IR_GetSymbolValueAsm::do_codegen(emitter::ObjectGenerator* gen,
                                      const AllocationResult& allocs,
                                      emitter::IR_Record irec) {
  auto dst_reg = m_use_coloring ? get_reg(m_dest, allocs, irec) : get_no_color_reg(m_dest);
  if (m_sext) {
    auto instr =
        gen->add_instr(IGen::load32s_gpr64_gpr64_plus_gpr64_plus_s32(
                           dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), 0x0badbeef),
                       irec);
    gen->link_instruction_symbol_mem(instr, m_sym_name);
  } else {
    auto instr =
        gen->add_instr(IGen::load32u_gpr64_gpr64_plus_gpr64_plus_s32(
                           dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), 0x0badbeef),
                       irec);
    gen->link_instruction_symbol_mem(instr, m_sym_name);
  }
}

///////////////////////
// AsmJumpReg
///////////////////////

IR_JumpReg::IR_JumpReg(bool use_coloring, const RegVal* src) : IR_Asm(use_coloring), m_src(src) {}

std::string IR_JumpReg::print() {
  return fmt::format(".jr{} {}", get_color_suffix_string(), m_src->print());
}

RegAllocInstr IR_JumpReg::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_JumpReg::do_codegen(emitter::ObjectGenerator* gen,
                            const AllocationResult& allocs,
                            emitter::IR_Record irec) {
  auto src_reg = m_use_coloring ? get_reg(m_src, allocs, irec) : get_no_color_reg(m_src);
  gen->add_instr(IGen::jmp_r64(src_reg), irec);
}

///////////////////////
// AsmRegSet
///////////////////////

IR_RegSetAsm::IR_RegSetAsm(bool use_color, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_color), m_dst(dst), m_src(src) {}

std::string IR_RegSetAsm::print() {
  return fmt::format(".mov{} {} {}", get_color_suffix_string(), m_dst->print(), m_src->print());
}

RegAllocInstr IR_RegSetAsm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_RegSetAsm::do_codegen(emitter::ObjectGenerator* gen,
                              const AllocationResult& allocs,
                              emitter::IR_Record irec) {
  regset_common(gen, allocs, irec, m_dst, m_src, m_use_coloring);
}

///////////////////////
// AsmVF3
///////////////////////

IR_VFMath3Asm::IR_VFMath3Asm(bool use_color,
                             const RegVal* dst,
                             const RegVal* src1,
                             const RegVal* src2,
                             Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_kind(kind) {}

std::string IR_VFMath3Asm::print() {
  std::string function = "";
  switch (m_kind) {
    case Kind::XOR:
      function = ".xor.vf";
      break;
    case Kind::SUB:
      function = ".sub.vf";
      break;
    case Kind::ADD:
      function = ".add.vf";
      break;
    case Kind::MUL:
      function = ".mul.vf";
      break;
    case Kind::MAX:
      function = ".max.vf";
      break;
    case Kind::MIN:
      function = ".min.vf";
      break;
    case Kind::DIV:
      function = ".div.vf";
      break;
    default:
      assert(false);
  }
  return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print());
}

RegAllocInstr IR_VFMath3Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_VFMath3Asm::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::XOR:
      gen->add_instr(IGen::xor_vf(dst, src1, src2), irec);
      break;
    case Kind::SUB:
      gen->add_instr(IGen::sub_vf(dst, src1, src2), irec);
      break;
    case Kind::ADD:
      gen->add_instr(IGen::add_vf(dst, src1, src2), irec);
      break;
    case Kind::MUL:
      gen->add_instr(IGen::mul_vf(dst, src1, src2), irec);
      break;
    case Kind::MAX:
      gen->add_instr(IGen::max_vf(dst, src1, src2), irec);
      break;
    case Kind::MIN:
      gen->add_instr(IGen::min_vf(dst, src1, src2), irec);
      break;
    case Kind::DIV:
      gen->add_instr(IGen::div_vf(dst, src1, src2), irec);
      break;
    default:
      assert(false);
  }
}

///////////////////////
// IR_Int128Math3Asm
///////////////////////

IR_Int128Math3Asm::IR_Int128Math3Asm(bool use_color,
                                     const RegVal* dst,
                                     const RegVal* src1,
                                     const RegVal* src2,
                                     Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_kind(kind) {}

std::string IR_Int128Math3Asm::print() {
  std::string function = "";
  switch (m_kind) {
    case Kind::PEXTLB:
      function = ".pextlb";
      break;
    case Kind::PEXTLH:
      function = ".pextlh";
      break;
    case Kind::PEXTLW:
      function = ".pextlw";
      break;
    case Kind::PEXTUB:
      function = ".pextub";
      break;
    case Kind::PEXTUH:
      function = ".pextuh";
      break;
    case Kind::PEXTUW:
      function = ".pextuw";
      break;
    case Kind::PCPYLD:
      function = ".pcpyld";
      break;
    case Kind::PCPYUD:
      function = ".pcpyud";
      break;
    case Kind::PSUBW:
      function = ".psubw";
      break;
    case Kind::PCEQB:
      function = ".pceqb";
      break;
    case Kind::PCEQH:
      function = ".pceqh";
      break;
    case Kind::PCEQW:
      function = ".pceqw";
      break;
    case Kind::PCGTB:
      function = ".pcgtb";
      break;
    case Kind::PCGTH:
      function = ".pcgth";
      break;
    case Kind::PCGTW:
      function = ".pcgtw";
      break;
    case Kind::POR:
      function = ".por";
      break;
    case Kind::PXOR:
      function = ".pxor";
      break;
    case Kind::PAND:
      function = ".pand";
      break;
    default:
      assert(false);
  }
  return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print());
}

RegAllocInstr IR_Int128Math3Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_Int128Math3Asm::do_codegen(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PEXTUB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextub_swapped(dst, src2, src1), irec);
      break;
    case Kind::PEXTUH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuh_swapped(dst, src2, src1), irec);
      break;
    case Kind::PEXTUW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextuw_swapped(dst, src2, src1), irec);
      break;
    case Kind::PEXTLB:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlb_swapped(dst, src2, src1), irec);
      break;
    case Kind::PEXTLH:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlh_swapped(dst, src2, src1), irec);
      break;
    case Kind::PEXTLW:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pextlw_swapped(dst, src2, src1), irec);
      break;
    case Kind::PCPYLD:
      // NOTE: this is intentionally swapped because x86 and PS2 do this opposite ways.
      gen->add_instr(IGen::pcpyld_swapped(dst, src2, src1), irec);
      break;
    case Kind::PCPYUD:
      gen->add_instr(IGen::pcpyud(dst, src1, src2), irec);
      break;
    case Kind::PCEQB:
      gen->add_instr(IGen::parallel_compare_e_b(dst, src2, src1), irec);
      break;
    case Kind::PCEQH:
      gen->add_instr(IGen::parallel_compare_e_h(dst, src2, src1), irec);
      break;
    case Kind::PCEQW:
      gen->add_instr(IGen::parallel_compare_e_w(dst, src2, src1), irec);
      break;
    case Kind::PCGTB:
      gen->add_instr(IGen::parallel_compare_gt_b(dst, src2, src1), irec);
      break;
    case Kind::PCGTH:
      gen->add_instr(IGen::parallel_compare_gt_h(dst, src2, src1), irec);
      break;
    case Kind::PCGTW:
      gen->add_instr(IGen::parallel_compare_gt_w(dst, src2, src1), irec);
      break;
    case Kind::PSUBW:
      // psubW on mips is psubD on x86...
      gen->add_instr(IGen::vpsubd(dst, src1, src2), irec);
      break;
    case Kind::POR:
      gen->add_instr(IGen::parallel_bitwise_or(dst, src2, src1), irec);
      break;
    case Kind::PXOR:
      gen->add_instr(IGen::parallel_bitwise_xor(dst, src2, src1), irec);
      break;
    case Kind::PAND:
      gen->add_instr(IGen::parallel_bitwise_and(dst, src2, src1), irec);
      break;
    default:
      assert(false);
  }
}

///////////////////////
// AsmVF2
///////////////////////

IR_VFMath2Asm::IR_VFMath2Asm(bool use_color, const RegVal* dst, const RegVal* src, Kind kind)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_kind(kind) {}

std::string IR_VFMath2Asm::print() {
  std::string function;
  switch (m_kind) {
    case Kind::ITOF:
      function = ".itof.vf";
      break;
    case Kind::FTOI:
      function = ".ftoi.vf";
      break;
    default:
      assert(false);
  }

  return fmt::format("{}{} {}, {}", function, get_color_suffix_string(), m_dst->print(),
                     m_src->print());
}

RegAllocInstr IR_VFMath2Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_VFMath2Asm::do_codegen(emitter::ObjectGenerator* gen,
                               const AllocationResult& allocs,
                               emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::ITOF:
      gen->add_instr(IGen::itof_vf(dst, src), irec);
      break;
    case Kind::FTOI:
      gen->add_instr(IGen::ftoi_vf(dst, src), irec);
      break;
    default:
      assert(false);
  }
}

///////////////////////
// AsmInt128-2
///////////////////////

IR_Int128Math2Asm::IR_Int128Math2Asm(bool use_color,
                                     const RegVal* dst,
                                     const RegVal* src,
                                     Kind kind,
                                     std::optional<int64_t> imm)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_kind(kind), m_imm(std::move(imm)) {}

std::string IR_Int128Math2Asm::print() {
  std::string function;
  bool use_imm = false;
  switch (m_kind) {
    case Kind::PW_SLL:
      use_imm = true;
      function = ".pw.sll";
      break;
    case Kind::PW_SRL:
      use_imm = true;
      function = ".pw.srl";
      break;
    case Kind::PW_SRA:
      use_imm = true;
      function = ".pw.sra";
      break;
    case Kind::VPSRLDQ:
      use_imm = true;
      function = ".VPSRLDQ";
      break;
    case Kind::VPSLLDQ:
      use_imm = true;
      function = ".VPSLLDQ";
      break;
    case Kind::VPSHUFLW:
      use_imm = true;
      function = ".VPSHUFLW";
      break;
    case Kind::VPSHUFHW:
      use_imm = true;
      function = ".VPSHUFHW";
      break;
    default:
      assert(false);
  }

  if (use_imm) {
    assert(m_imm.has_value());
    return fmt::format("{}{} {}, {}, {}", function, get_color_suffix_string(), m_dst->print(),
                       m_src->print(), *m_imm);
  } else {
    return fmt::format("{}{} {}, {}", function, get_color_suffix_string(), m_dst->print(),
                       m_src->print());
  }
}

RegAllocInstr IR_Int128Math2Asm::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_Int128Math2Asm::do_codegen(emitter::ObjectGenerator* gen,
                                   const AllocationResult& allocs,
                                   emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);

  switch (m_kind) {
    case Kind::PW_SLL:
      // you are technically allowed to put values > 32 in here.
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::pw_sll(dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRL:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::pw_srl(dst, src, *m_imm), irec);
      break;
    case Kind::PW_SRA:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::pw_sra(dst, src, *m_imm), irec);
      break;
    case Kind::VPSRLDQ:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::vpsrldq(dst, src, *m_imm), irec);
      break;
    case Kind::VPSLLDQ:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::vpslldq(dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFLW:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::vpshuflw(dst, src, *m_imm), irec);
      break;
    case Kind::VPSHUFHW:
      assert(m_imm.has_value());
      assert(*m_imm >= 0);
      assert(*m_imm <= 255);
      gen->add_instr(IGen::vpshufhw(dst, src, *m_imm), irec);
      break;
    default:
      assert(false);
  }
}

// ---- Blend VF

IR_BlendVF::IR_BlendVF(bool use_color,
                       const RegVal* dst,
                       const RegVal* src1,
                       const RegVal* src2,
                       u8 mask)
    : IR_Asm(use_color), m_dst(dst), m_src1(src1), m_src2(src2), m_mask(mask) {}

std::string IR_BlendVF::print() {
  return fmt::format(".blend.vf{} {}, {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src1->print(), m_src2->print(), m_mask);
}

RegAllocInstr IR_BlendVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src1->ireg());
    rai.read.push_back(m_src2->ireg());
  }
  return rai;
}

void IR_BlendVF::do_codegen(emitter::ObjectGenerator* gen,
                            const AllocationResult& allocs,
                            emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src1 = get_reg_asm(m_src1, allocs, irec, m_use_coloring);
  auto src2 = get_reg_asm(m_src2, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::blend_vf(dst, src1, src2, m_mask), irec);
}

// ----- Splat VF

IR_SplatVF::IR_SplatVF(bool use_color,
                       const RegVal* dst,
                       const RegVal* src,
                       const emitter::Register::VF_ELEMENT element)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_element(element) {}

std::string IR_SplatVF::print() {
  return fmt::format(".splat.vf{} {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print(), m_element);
}

RegAllocInstr IR_SplatVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SplatVF::do_codegen(emitter::ObjectGenerator* gen,
                            const AllocationResult& allocs,
                            emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::splat_vf(dst, src, m_element), irec);
}

// ---- Swizzle VF

IR_SwizzleVF::IR_SwizzleVF(bool use_color,
                           const RegVal* dst,
                           const RegVal* src,
                           const u8 controlBytes)
    : IR_Asm(use_color), m_dst(dst), m_src(src), m_controlBytes(controlBytes) {}

std::string IR_SwizzleVF::print() {
  return fmt::format(".swizzle.vf{} {}, {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print(), m_controlBytes);
}

RegAllocInstr IR_SwizzleVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SwizzleVF::do_codegen(emitter::ObjectGenerator* gen,
                              const AllocationResult& allocs,
                              emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::swizzle_vf(dst, src, m_controlBytes), irec);
}

// ---- Square Root VF

IR_SqrtVF::IR_SqrtVF(bool use_color, const RegVal* dst, const RegVal* src)
    : IR_Asm(use_color), m_dst(dst), m_src(src) {}

std::string IR_SqrtVF::print() {
  return fmt::format(".sqrt.vf{} {}, {}", get_color_suffix_string(), m_dst->print(),
                     m_src->print());
}

RegAllocInstr IR_SqrtVF::to_rai() {
  RegAllocInstr rai;
  if (m_use_coloring) {
    rai.write.push_back(m_dst->ireg());
    rai.read.push_back(m_src->ireg());
  }
  return rai;
}

void IR_SqrtVF::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  auto dst = get_reg_asm(m_dst, allocs, irec, m_use_coloring);
  auto src = get_reg_asm(m_src, allocs, irec, m_use_coloring);
  gen->add_instr(IGen::sqrt_vf(dst, src), irec);
}
