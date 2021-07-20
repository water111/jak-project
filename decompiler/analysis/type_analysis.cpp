#include "type_analysis.h"

namespace decompiler {
namespace {
TypeState construct_initial_typestate(const TypeSpec& f_ts, const Env& env) {
  TypeState result;
  int goal_args[] = {Reg::A0, Reg::A1, Reg::A2, Reg::A3, Reg::T0, Reg::T1, Reg::T2, Reg::T3};
  assert(f_ts.base_type() == "function");
  assert(f_ts.arg_count() >= 1);
  assert(f_ts.arg_count() <= 8 + 1);  // 8 args + 1 return.
  for (int i = 0; i < int(f_ts.arg_count()) - 1; i++) {
    auto reg_id = goal_args[i];
    auto reg_type = f_ts.get_arg(i);
    result.get(Register(Reg::GPR, reg_id)) = TP_Type::make_from_ts(reg_type);
  }

  // todo, more specific process types for behaviors.
  result.get(Register(Reg::GPR, Reg::S6)) =
      TP_Type::make_from_ts(TypeSpec(f_ts.try_get_tag("behavior").value_or("process")));

  // initialize stack slots as uninitialized
  for (auto slot_info : env.stack_spills().map()) {
    result.spill_slots.insert({slot_info.first, {}});
  }
  return result;
}

/*!
 * Modify the the given type state based on the given casts.
 */
void modify_input_types_for_casts(
    const std::vector<RegisterTypeCast>& casts,
    TypeState* state,
    std::unordered_map<Register, TP_Type, Register::hash>* changed_types,
    DecompilerTypeSystem& dts) {
  for (auto& cast : casts) {
    try {
      auto type_from_cast = TP_Type::make_from_ts(dts.parse_type_spec(cast.type_name));
      auto original_type = state->get(cast.reg);
      // fmt::print("Cast reg {} : {} -> {}\n", cast.reg.to_string(), original_type.print(),
      // type_from_cast.print());
      if (original_type != type_from_cast) {
        // the cast will have an effect on types. If we are removing the original type, remember it
        if (changed_types && changed_types->find(cast.reg) == changed_types->end()) {
          (*changed_types)[cast.reg] = original_type;
        }

        // and actually update the typ
        state->get(cast.reg) = type_from_cast;
      }
    } catch (std::exception& e) {
      printf("failed to parse hint: %s\n", e.what());
      assert(false);
    }
  }
}

void try_modify_input_types_for_casts(
    int idx,
    const std::unordered_map<int, std::vector<RegisterTypeCast>>& casts,
    const std::unordered_map<int, StackTypeCast>& stack_casts,
    TypeState* state,
    std::unordered_map<Register, TP_Type, Register::hash>* changed_types,
    DecompilerTypeSystem& dts) {
  auto kv = casts.find(idx);
  if (kv != casts.end()) {
    // fmt::print("at idx {}, casting:\n", idx);
    modify_input_types_for_casts(kv->second, state, changed_types, dts);
  }

  for (const auto& [offset, cast] : stack_casts) {
    auto stack_kv = state->spill_slots.find(offset);
    if (stack_kv == state->spill_slots.end()) {
      throw std::runtime_error(
          fmt::format("Got a stack cast at offset {}, but didn't find a variable there.", offset));
    }
    state->spill_slots[offset] = TP_Type::make_from_ts(dts.parse_type_spec(cast.type_name));
  }
}
}  // namespace

bool run_type_analysis_ir2(const TypeSpec& my_type, DecompilerTypeSystem& dts, Function& func) {
  // STEP 0 - set decompiler type system settings for this function. In config we can manually
  if (func.guessed_name.kind == FunctionName::FunctionKind::METHOD) {
    dts.type_prop_settings.current_method_type = func.guessed_name.type_name;
  }

  if (my_type.last_arg() == TypeSpec("none")) {
    auto as_end = dynamic_cast<FunctionEndOp*>(func.ir2.atomic_ops->ops.back().get());
    assert(as_end);
    as_end->mark_function_as_no_return_value();
  }

  std::vector<TypeState> block_init_types, op_types;
  std::vector<bool> block_needs_update(func.basic_blocks.size(), true);
  block_init_types.resize(func.basic_blocks.size());
  op_types.resize(func.ir2.atomic_ops->ops.size());
  auto& aop = func.ir2.atomic_ops;

  // STEP 1 - topological sort the blocks. This gives us an order where we:
  // - never visit unreachable blocks (we can't type propagate these)
  // - always visit at least one predecessor of a block before that block
  auto order = func.bb_topo_sort();
  assert(!order.vist_order.empty());
  assert(order.vist_order.front() == 0);

  // STEP 2 - initialize type state for the first block to the function argument types.
  block_init_types.at(0) = construct_initial_typestate(my_type, func.ir2.env);

  // STEP 3 - propagate types until the result stops changing
  bool run_again = true;
  while (run_again) {
    run_again = false;
    // do each block in the topological sort order:
    for (auto block_id : order.vist_order) {
      if (!block_needs_update.at(block_id)) {
        continue;
      }
      auto& block = func.basic_blocks.at(block_id);
      TypeState* init_types = &block_init_types.at(block_id);
      for (int op_id = aop->block_id_to_first_atomic_op.at(block_id);
           op_id < aop->block_id_to_end_atomic_op.at(block_id); op_id++) {
        std::unordered_map<Register, TP_Type, Register::hash> restore_cast_types;
        try_modify_input_types_for_casts(op_id, func.ir2.env.casts(), func.ir2.env.stack_casts(),
                                         init_types, &restore_cast_types, dts);

        auto& op = aop->ops.at(op_id);

        try {
          op_types.at(op_id) = op->propagate_types(*init_types, func.ir2.env, dts);
        } catch (std::runtime_error& e) {
          lg::warn("Function {} failed type prop at op {}: {}", func.guessed_name.to_string(),
                   op_id, e.what());
          func.warnings.type_prop_warning("Failed type prop at op {} ({}): {}", op_id,
                                          op->to_string(func.ir2.env), e.what());
          func.ir2.env.set_types(block_init_types, op_types, *func.ir2.atomic_ops, my_type);
          return false;
        }

        // undo casts.
        for (auto& restore_kv : restore_cast_types) {
          if (std::find(op->write_regs().begin(), op->write_regs().end(), restore_kv.first) ==
              op->write_regs().end()) {
            op_types.at(op_id).get(restore_kv.first) = restore_kv.second;
          }

          init_types->get(restore_kv.first) = restore_kv.second;
        }

        // for the next op...
        init_types = &op_types.at(op_id);
      }
      block_needs_update.at(block_id) = false;

      // propagate the types: for each possible succ
      for (auto succ_block_id : {block.succ_ft, block.succ_branch}) {
        if (succ_block_id != -1) {
          // set types to LCA (current, new)
          if (dts.tp_lca(&block_init_types.at(succ_block_id), *init_types)) {
            // if something changed, run again!
            run_again = true;
            block_needs_update.at(succ_block_id) = true;
          }
        }
      }
    }
  }

  auto last_type = op_types.back().get(Register(Reg::GPR, Reg::V0)).typespec();
  if (last_type != my_type.last_arg()) {
    func.warnings.info("Return type mismatch {} vs {}.", last_type.print(),
                       my_type.last_arg().print());
  }

  // and apply final casts:
  for (auto block_id : order.vist_order) {
    for (int op_id = aop->block_id_to_first_atomic_op.at(block_id);
         op_id < aop->block_id_to_end_atomic_op.at(block_id); op_id++) {
      if (op_id == aop->block_id_to_first_atomic_op.at(block_id)) {
        try_modify_input_types_for_casts(op_id, func.ir2.env.casts(), func.ir2.env.stack_casts(),
                                         &block_init_types.at(block_id), nullptr, dts);
      } else {
        try_modify_input_types_for_casts(op_id, func.ir2.env.casts(), func.ir2.env.stack_casts(),
                                         &op_types.at(op_id - 1), nullptr, dts);
      }
    }
  }

  // figure out the types of stack spill variables:
  auto& env = func.ir2.env;
  bool changed;
  for (auto& type_info : op_types) {
    for (auto& spill : type_info.spill_slots) {
      auto& slot_info = env.stack_slot_entries[spill.first];
      slot_info.tp_type =
          dts.tp_lca(env.stack_slot_entries[spill.first].tp_type, spill.second, &changed);
      slot_info.offset = spill.first;
    }
  }

  for (auto& type_info : block_init_types) {
    for (auto& spill : type_info.spill_slots) {
      auto& slot_info = env.stack_slot_entries[spill.first];
      slot_info.tp_type =
          dts.tp_lca(env.stack_slot_entries[spill.first].tp_type, spill.second, &changed);
      slot_info.offset = spill.first;
    }
  }

  // convert to typespec
  for (auto& info : env.stack_slot_entries) {
    info.second.typespec = info.second.tp_type.typespec();
    //     debug
    // fmt::print("STACK {} : {} ({})\n", info.first, info.second.typespec.print(),
    //         info.second.tp_type.print());
  }

  func.ir2.env.set_types(block_init_types, op_types, *func.ir2.atomic_ops, my_type);

  return true;
}
}  // namespace decompiler
