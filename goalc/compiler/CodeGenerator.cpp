#include "CodeGenerator.h"
#include "goalc/emitter/IGen.h"
#include "IR.h"

using namespace emitter;
constexpr int GPR_SIZE = 8;
constexpr int XMM_SIZE = 16;

CodeGenerator::CodeGenerator(FileEnv* env) : m_fe(env) {}

std::vector<u8> CodeGenerator::run() {
  for (auto& f : m_fe->functions()) {
    m_gen.add_function_to_seg(f->segment);
  }

  // todo, static objects
  for (auto& static_obj : m_fe->statics()) {
    static_obj->generate(&m_gen);
  }

  for (size_t i = 0; i < m_fe->functions().size(); i++) {
    do_function(m_fe->functions().at(i).get(), i);
  }
  //  for (auto& f : m_fe->functions()) {
  //    do_function(f.get());
  //  }

  return m_gen.generate_data_v3().to_vector();
}

void CodeGenerator::do_function(FunctionEnv* env, int f_idx) {
  auto f_rec = m_gen.get_existing_function_record(f_idx);
  // auto f_rec = m_gen.add_function_to_seg(env->segment);  // todo, extra alignment settings

  auto& ri = emitter::gRegInfo;
  const auto& allocs = env->alloc_result();

  // compute how much stack we will use
  int stack_offset = 0;

  // back up xmms
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_xmm()) {
      m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm8s(RSP, XMM_SIZE));
      m_gen.add_instr_no_ir(f_rec, IGen::store128_gpr64_xmm128(RSP, saved_reg));
      stack_offset += XMM_SIZE;
    }
  }

  // back up gprs
  for (auto& saved_reg : allocs.used_saved_regs) {
    if (saved_reg.is_gpr()) {
      m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(saved_reg));
      stack_offset += GPR_SIZE;
    }
  }

  bool bonus_push = false;
  int manually_added_stack_offset = GPR_SIZE * allocs.stack_slots;
  stack_offset += manually_added_stack_offset;

  if (manually_added_stack_offset || allocs.needs_aligned_stack_for_spills ||
      env->needs_aligned_stack()) {
    if (!(stack_offset & 15)) {
      if (manually_added_stack_offset) {
        manually_added_stack_offset += 8;
      } else {
        bonus_push = true;
        m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(ri.get_saved_gpr(0)));
      }
      stack_offset += 8;
    }

    assert(stack_offset & 15);

    if (manually_added_stack_offset) {
      m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(RSP, manually_added_stack_offset));
    }
  }

  // TODO EMIT FUNCTIONS
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    auto i_rec = m_gen.add_ir(f_rec);

    auto& bonus = allocs.stack_ops.at(ir_idx);
    for (auto& op : bonus.ops) {
      if (op.load) {
        if (op.reg.is_gpr()) {
          m_gen.add_instr(IGen::load64_gpr64_plus_s32(op.reg, op.slot * GPR_SIZE, RSP), i_rec);
        } else {
          assert(false);
        }
      }
    }
    ir->do_codegen(&m_gen, allocs, i_rec);
    for (auto& op : bonus.ops) {
      if (op.store) {
        if (op.reg.is_gpr()) {
          m_gen.add_instr(IGen::store64_gpr64_plus_s32(RSP, op.slot * GPR_SIZE, op.reg), i_rec);
        } else {
          assert(false);
        }
      }
    }
  }

  // EPILOGUE
  if (manually_added_stack_offset || allocs.needs_aligned_stack_for_spills ||
      env->needs_aligned_stack()) {
    if (manually_added_stack_offset) {
      m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm(RSP, manually_added_stack_offset));
    }

    if (bonus_push) {
      assert(!manually_added_stack_offset);
      m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(ri.get_saved_gpr(0)));
    }
  }

  for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
    auto& saved_reg = allocs.used_saved_regs.at(i);
    if (saved_reg.is_gpr()) {
      m_gen.add_instr_no_ir(f_rec, IGen::pop_gpr64(saved_reg));
    }
  }

  for (int i = int(allocs.used_saved_regs.size()); i-- > 0;) {
    auto& saved_reg = allocs.used_saved_regs.at(i);
    if (saved_reg.is_xmm()) {
      m_gen.add_instr_no_ir(f_rec, IGen::load128_xmm128_gpr64(saved_reg, RSP));
      m_gen.add_instr_no_ir(f_rec, IGen::add_gpr64_imm8s(RSP, XMM_SIZE));
    }
  }

  m_gen.add_instr_no_ir(f_rec, IGen::ret());
}