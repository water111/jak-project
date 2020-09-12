#include "IR.h"

#include <utility>
#include "goalc/emitter/IGen.h"

using namespace emitter;

namespace {
Register get_reg(const RegVal* rv, const AllocationResult& allocs, emitter::IR_Record irec) {
  auto& ass = allocs.ass_as_ranges.at(rv->ireg().id).get(irec.ir_id);
  assert(ass.kind == Assignment::Kind::REGISTER);
  return ass.reg;
}
}  // namespace

///////////
// Return
///////////
IR_Return::IR_Return(const RegVal* return_reg, const RegVal* value)
    : m_return_reg(return_reg), m_value(value) {}
std::string IR_Return::print() {
  return fmt::format("ret {} {}", m_return_reg->print(), m_value->print());
}

RegAllocInstr IR_Return::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_return_reg->ireg());
  rai.read.push_back(m_value->ireg());
  if (m_value->ireg().kind == m_return_reg->ireg().kind) {
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
  c.desired_register = emitter::RAX;
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
    gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, val_reg), irec);
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
  gen->add_instr(IGen::mov_gpr64_u64(dest_reg, m_value), irec);
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
  // todo, could be single lea opcode
  gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, gRegInfo.get_st_reg()), irec);
  auto add = gen->add_instr(IGen::add_gpr64_imm32s(dest_reg, 0x0afecafe), irec);
  gen->link_instruction_symbol_ptr(add, m_name);
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
  if (m_dest->ireg().kind == m_src->ireg().kind) {
    rai.is_move = true;  // only true if we aren't moving from register kind to register kind
  }
  return rai;
}

void IR_RegSet::do_codegen(emitter::ObjectGenerator* gen,
                           const AllocationResult& allocs,
                           emitter::IR_Record irec) {
  auto val_reg = get_reg(m_src, allocs, irec);
  auto dest_reg = get_reg(m_dest, allocs, irec);

  if (val_reg == dest_reg) {
    gen->add_instr(IGen::null(), irec);
  } else {
    gen->add_instr(IGen::mov_gpr64_gpr64(dest_reg, val_reg), irec);
  }
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

IR_FunctionCall::IR_FunctionCall(const RegVal* func, const RegVal* ret, std::vector<RegVal*> args)
    : m_func(func), m_ret(ret), m_args(std::move(args)) {}

std::string IR_FunctionCall::print() {
  std::string result = fmt::format("call {} (ret {}) (args ", m_func->print(), m_ret->print());
  for (const auto& x : m_args) {
    result += fmt::format("{} ", x->print());
  }
  result.pop_back();
  return result;
}

RegAllocInstr IR_FunctionCall::to_rai() {
  RegAllocInstr rai;
  rai.read.push_back(m_func->ireg());
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

  // todo, clobber call reg?

  return rai;
}

void IR_FunctionCall::add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
  for (size_t i = 0; i < m_args.size(); i++) {
    IRegConstraint c;
    c.ireg = m_args.at(i)->ireg();
    c.instr_idx = my_id;
    c.desired_register = emitter::gRegInfo.get_arg_reg(i);
    constraints->push_back(c);
  }

  IRegConstraint c;
  c.ireg = m_ret->ireg();
  c.desired_register = emitter::gRegInfo.get_ret_reg();
  c.instr_idx = my_id;
  constraints->push_back(c);
}

void IR_FunctionCall::do_codegen(emitter::ObjectGenerator* gen,
                                 const AllocationResult& allocs,
                                 emitter::IR_Record irec) {
  auto freg = get_reg(m_func, allocs, irec);
  gen->add_instr(IGen::add_gpr64_gpr64(freg, emitter::gRegInfo.get_offset_reg()), irec);
  gen->add_instr(IGen::call_r64(freg), irec);
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
///////////////////

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
///////////////////

IR_IntegerMath::IR_IntegerMath(IntegerMathKind kind, RegVal* dest, RegVal* arg)
    : m_kind(kind), m_dest(dest), m_arg(arg) {}

std::string IR_IntegerMath::print() {
  switch (m_kind) {
    case IntegerMathKind::ADD_64:
      return fmt::format("addi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::SUB_64:
      return fmt::format("subi {}, {}", m_dest->print(), m_arg->print());
    case IntegerMathKind::IMUL_32:
      return fmt::format("imul {}, {}", m_dest->print(), m_arg->print());
    default:
      assert(false);
  }
}

RegAllocInstr IR_IntegerMath::to_rai() {
  RegAllocInstr rai;
  rai.write.push_back(m_dest->ireg());
  rai.read.push_back(m_dest->ireg());
  rai.read.push_back(m_arg->ireg());
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
    case IntegerMathKind::IMUL_32: {
      auto dr = get_reg(m_dest, allocs, irec);
      gen->add_instr(IGen::imul_gpr32_gpr32(dr, get_reg(m_arg, allocs, irec)), irec);
      gen->add_instr(IGen::movsx_r64_r32(dr, dr), irec);
    }

    break;
    default:
      assert(false);
  }
}