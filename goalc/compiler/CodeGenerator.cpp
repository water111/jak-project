/*!
 * @file CodeGenerator.cpp
 * Generate object files from a FileEnv using an emitter::ObjectGenerator.
 * Populates a DebugInfo.
 * Currently owns the logic for emitting the function prologues/epilogues and stack spill ops.
 */

#include <unordered_set>
#include <goalc/debugger/DebugInfo.h>
#include "CodeGenerator.h"
#include "goalc/emitter/IGen.h"
#include "IR.h"

using namespace emitter;

CodeGenerator::CodeGenerator(FileEnv* env, DebugInfo* debug_info)
    : m_fe(env), m_debug_info(debug_info) {}

/*!
 * Generate an object file.
 */
std::vector<u8> CodeGenerator::run() {
  std::unordered_set<std::string> function_names;

  // first, add each function to the ObjectGenerator (but don't add any data)
  for (auto& f : m_fe->functions()) {
    if (function_names.find(f->name()) == function_names.end()) {
      function_names.insert(f->name());
    } else {
      printf("Failed to codegen, there are two functions with internal names %s\n",
             f->name().c_str());
      throw std::runtime_error("Failed to codegen.");
    }
    m_gen.add_function_to_seg(f->segment, &m_debug_info->add_function(f->name()));
  }

  // next, add all static objects.
  for (auto& static_obj : m_fe->statics()) {
    static_obj->generate(&m_gen);
  }

  // next, add instructions to functions
  for (size_t i = 0; i < m_fe->functions().size(); i++) {
    do_function(m_fe->functions().at(i).get(), i);
  }

  // generate a v3 object. TODO - support for v4 "data" objects.
  return m_gen.generate_data_v3().to_vector();
}

/*!
 * Add instructions to the function, specified by index.
 * Generates prologues / epilogues.
 */
void CodeGenerator::do_function(FunctionEnv* env, int f_idx) {
  auto f_rec = m_gen.get_existing_function_record(f_idx);
  // todo, extra alignment settings

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

  // do we include an extra push to get 8 more bytes to keep the stack aligned?
  bool bonus_push = false;

  // the offset to add directly to rsp for stack variables (no push/pop)
  int manually_added_stack_offset = GPR_SIZE * allocs.stack_slots;
  stack_offset += manually_added_stack_offset;

  // do we need to align or manually offset?
  if (manually_added_stack_offset || allocs.needs_aligned_stack_for_spills ||
      env->needs_aligned_stack()) {
    if (!(stack_offset & 15)) {
      if (manually_added_stack_offset) {
        // if we're already adding to rsp, just add 8 more.
        manually_added_stack_offset += 8;
      } else {
        // otherwise to an extra push, and remember so we can do an extra pop later on.
        bonus_push = true;
        m_gen.add_instr_no_ir(f_rec, IGen::push_gpr64(ri.get_saved_gpr(0)));
      }
      stack_offset += 8;
    }

    assert(stack_offset & 15);

    // do manual stack offset.
    if (manually_added_stack_offset) {
      m_gen.add_instr_no_ir(f_rec, IGen::sub_gpr64_imm(RSP, manually_added_stack_offset));
    }
  }

  // emit each IR into x86 instructions.
  for (int ir_idx = 0; ir_idx < int(env->code().size()); ir_idx++) {
    auto& ir = env->code().at(ir_idx);
    // start of IR
    auto i_rec = m_gen.add_ir(f_rec);

    // load anything off the stack that was spilled and is needed.
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

    // do the actual op
    ir->do_codegen(&m_gen, allocs, i_rec);

    // store things back on the stack if needed.
    for (auto& op : bonus.ops) {
      if (op.store) {
        if (op.reg.is_gpr()) {
          m_gen.add_instr(IGen::store64_gpr64_plus_s32(RSP, op.slot * GPR_SIZE, op.reg), i_rec);
        } else {
          assert(false);
        }
      }
    }
  }  // end IR loop

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