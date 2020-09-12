/*!
 * @file Allocator.cpp
 * Implementation of register allocation algorithms
 */

#include <algorithm>
#include "Allocator.h"
#include "LiveInfo.h"

/*!
 * Find basic blocks and add block link info.
 */
void find_basic_blocks(RegAllocCache* cache, const AllocationInput& in) {
  std::vector<int> dividers;

  dividers.push_back(0);
  dividers.push_back(in.instructions.size());

  // loop over instructions, finding jump targets
  for (uint32_t i = 0; i < in.instructions.size(); i++) {
    const auto& instr = in.instructions[i];
    if (!instr.jumps.empty()) {
      dividers.push_back(i + 1);
      for (auto dest : instr.jumps) {
        dividers.push_back(dest);
      }
    }
  }

  // sort dividers, and make blocks
  std::sort(dividers.begin(), dividers.end(), [](int a, int b) { return a < b; });

  for (uint32_t i = 0; i < dividers.size() - 1; i++) {
    if (dividers[i] != dividers[i + 1]) {
      // new basic block!
      RegAllocBasicBlock block;
      for (int j = dividers[i]; j < dividers[i + 1]; j++) {
        block.instr_idx.push_back(j);
      }

      block.idx = cache->basic_blocks.size();
      cache->basic_blocks.push_back(block);
    }
  }

  if (!cache->basic_blocks.empty()) {
    cache->basic_blocks.front().is_entry = true;
    cache->basic_blocks.back().is_exit = true;
  }

  auto find_basic_block_to_target = [&](int instr) {
    bool found = false;
    uint32_t result = -1;
    for (uint32_t i = 0; i < cache->basic_blocks.size(); i++) {
      if (!cache->basic_blocks[i].instr_idx.empty() &&
          cache->basic_blocks[i].instr_idx.front() == instr) {
        assert(!found);
        found = true;
        result = i;
      }
    }
    if (!found) {
      printf("[RegAlloc Error] couldn't find basic block beginning with instr %d of %ld\n", instr,
             in.instructions.size());
    }
    assert(found);
    return result;
  };

  // link blocks
  for (auto& block : cache->basic_blocks) {
    assert(!block.instr_idx.empty());
    auto& last_instr = in.instructions.at(block.instr_idx.back());
    if (last_instr.fallthrough) {
      // try to link to next block:
      int next_idx = block.idx + 1;
      if (next_idx < (int)cache->basic_blocks.size()) {
        cache->basic_blocks.at(next_idx).pred.push_back(block.idx);
        block.succ.push_back(next_idx);
      }
    }
    for (auto target : last_instr.jumps) {
      cache->basic_blocks.at(find_basic_block_to_target(target)).pred.push_back(block.idx);
      block.succ.push_back(find_basic_block_to_target(target));
    }
  }
}

namespace {

/*!
 * Setup live_ranges. Must have found where iregs are live first.
 */
void compute_live_ranges(RegAllocCache* cache, const AllocationInput& in) {
  // then resize live ranges to the correct size
  cache->live_ranges.resize(cache->max_var, LiveInfo(in.instructions.size(), 0));

  // now compute the ranges
  for (auto& block : cache->basic_blocks) {
    // from var use
    for (auto instr_id : block.instr_idx) {
      auto& inst = in.instructions.at(instr_id);

      for (auto& lst : {inst.read, inst.write}) {
        for (auto& x : lst) {
          cache->live_ranges.at(x.id).add_live_instruction(instr_id);
        }
      }
    }

    // and liveliness analysis
    assert(block.live.size() == block.instr_idx.size());
    for (uint32_t i = 0; i < block.live.size(); i++) {
      for (auto& x : block.live[i]) {
        cache->live_ranges.at(x).add_live_instruction(block.instr_idx.at(i));
      }
    }
  }

  // make us alive at any constrained instruction. todo, if this happens is this a sign of an issue
  for (auto& con : in.constraints) {
    cache->live_ranges.at(con.ireg.id).add_live_instruction(con.instr_idx);
  }
}
}  // namespace

/*!
 * Analysis pass to find out where registers are live. Must have found basic blocks first.
 */
void analyze_liveliness(RegAllocCache* cache, const AllocationInput& in) {
  cache->max_var = in.max_vars;
  cache->was_colored.resize(cache->max_var, false);
  cache->iregs.resize(cache->max_var);

  for (auto& instr : in.instructions) {
    for (auto& wr : instr.write) {
      cache->iregs.at(wr.id) = wr;
    }

    for (auto& rd : instr.read) {
      cache->iregs.at(rd.id) = rd;
    }
  }

  // phase 1
  for (auto& block : cache->basic_blocks) {
    block.live.resize(block.instr_idx.size());
    block.dead.resize(block.instr_idx.size());
    block.analyze_liveliness_phase1(in.instructions);
  }

  // phase 2
  bool changed = false;
  do {
    changed = false;
    for (auto& block : cache->basic_blocks) {
      if (block.analyze_liveliness_phase2(cache->basic_blocks, in.instructions)) {
        changed = true;
      }
    }
  } while (changed);

  // phase 3
  for (auto& block : cache->basic_blocks) {
    block.analyze_liveliness_phase3(cache->basic_blocks, in.instructions);
  }

  // phase 4
  compute_live_ranges(cache, in);

  // final setup!
  for (size_t i = 0; i < cache->live_ranges.size(); i++) {
    cache->live_ranges.at(i).prepare_for_allocation(i);
  }
  cache->stack_ops.resize(in.instructions.size());
}

namespace {
template <typename T>
bool in_set(std::set<T>& set, const T& obj) {
  return set.find(obj) != set.end();
}
}  // namespace

void RegAllocBasicBlock::analyze_liveliness_phase1(const std::vector<RegAllocInstr>& instructions) {
  for (int i = instr_idx.size(); i-- > 0;) {
    auto ii = instr_idx.at(i);
    auto& instr = instructions.at(ii);
    auto& lv = live.at(i);
    auto& dd = dead.at(i);

    // make all read live out
    lv.clear();
    for (auto& x : instr.read) {
      lv.insert(x.id);
    }

    // kill things which are overwritten
    dd.clear();
    for (auto& x : instr.write) {
      if (!in_set(lv, x.id)) {
        dd.insert(x.id);
      }
    }

    // b.use = i.liveout
    std::set<int> use_old = use;
    use.clear();
    for (auto& x : lv) {
      use.insert(x);
    }
    // | (bu.use & !i.dead)
    for (auto& x : use_old) {
      if (!in_set(dd, x)) {
        use.insert(x);
      }
    }

    // b.defs = i.dead
    std::set<int> defs_old = defs;
    defs.clear();
    for (auto& x : dd) {
      defs.insert(x);
    }
    // | b.defs & !i.lv
    for (auto& x : defs_old) {
      if (!in_set(lv, x)) {
        defs.insert(x);
      }
    }
  }
}

bool RegAllocBasicBlock::analyze_liveliness_phase2(std::vector<RegAllocBasicBlock>& blocks,
                                                   const std::vector<RegAllocInstr>& instructions) {
  (void)instructions;
  bool changed = false;
  auto out = defs;

  for (auto s : succ) {
    for (auto in : blocks.at(s).input) {
      out.insert(in);
    }
  }

  std::set<int> in = use;
  for (auto x : out) {
    if (!in_set(defs, x)) {
      in.insert(x);
    }
  }

  if (in != input || out != output) {
    changed = true;
    input = in;
    output = out;
  }

  return changed;
}

void RegAllocBasicBlock::analyze_liveliness_phase3(std::vector<RegAllocBasicBlock>& blocks,
                                                   const std::vector<RegAllocInstr>& instructions) {
  (void)instructions;
  std::set<int> live_local;
  for (auto s : succ) {
    for (auto i : blocks.at(s).input) {
      live_local.insert(i);
    }
  }

  for (int i = instr_idx.size(); i-- > 0;) {
    auto& lv = live.at(i);
    auto& dd = dead.at(i);

    std::set<int> new_live = lv;
    for (auto x : live_local) {
      if (!in_set(dd, x)) {
        new_live.insert(x);
      }
    }
    lv = live_local;
    live_local = new_live;
  }
}

std::string RegAllocBasicBlock::print_summary() const {
  std::string result = "block " + std::to_string(idx) + "\nsucc: ";
  for (auto s : succ) {
    result += std::to_string(s) + " ";
  }
  result += "\npred: ";
  for (auto p : pred) {
    result += std::to_string(p) + " ";
  }
  result += "\nuse: ";
  for (auto x : use) {
    result += std::to_string(x) + " ";
  }
  result += "\ndef: ";
  for (auto x : defs) {
    result += std::to_string(x) + " ";
  }
  result += "\ninput: ";
  for (auto x : input) {
    result += std::to_string(x) + " ";
  }
  result += "\noutput: ";
  for (auto x : output) {
    result += std::to_string(x) + " ";
  }

  return result;
}

std::string RegAllocBasicBlock::print(const std::vector<RegAllocInstr>& insts) const {
  std::string result = print_summary() + "\n";
  int k = 0;
  for (auto instr : instr_idx) {
    std::string line = insts.at(instr).print();
    constexpr int pad_len = 30;
    if (line.length() < pad_len) {
      // line.insert(line.begin(), pad_len - line.length(), ' ');
      line.append(pad_len - line.length(), ' ');
    }

    result += "  " + line + " live: ";
    for (auto j : live.at(k)) {
      result += std::to_string(j) + " ";
    }
    result += "\n";

    k++;
  }
  return result;
}

/*!
 * Assign registers which are constrained. If constraints are inconsistent, won't succeed in
 * satisfying them (of course), and won't error either. Use check_constrained_alloc to confirm
 * that all constraints are then satisfied.
 */
void do_constrained_alloc(RegAllocCache* cache, const AllocationInput& in, bool trace_debug) {
  for (auto& constr : in.constraints) {
    auto var_id = constr.ireg.id;
    if (trace_debug) {
      fmt::print("[RA] Apply constraint {}\n", constr.to_string());
    }
    cache->live_ranges.at(var_id).constrain_at_one(constr.instr_idx, constr.desired_register);
  }
}

/*!
 * Check to run after do_constrained_alloc to see if the constraints could actually be satisfied.
 */
bool check_constrained_alloc(RegAllocCache* cache, const AllocationInput& in) {
  bool ok = true;
  for (auto& constr : in.constraints) {
    if (!cache->live_ranges.at(constr.ireg.id)
             .conflicts_at(constr.instr_idx, constr.desired_register)) {
      fmt::print("[RegAlloc Error] There are conflicting constraints on {}: {} and {}\n",
                 constr.ireg.to_string(), constr.desired_register.print(),
                 cache->live_ranges.at(constr.ireg.id).get(constr.instr_idx).to_string());
      ok = false;
    }
  }

  for (uint32_t i = 0; i < in.instructions.size(); i++) {
    for (auto& lr1 : cache->live_ranges) {
      if (!lr1.seen || !lr1.is_live_at_instr(i))
        continue;
      for (auto& lr2 : cache->live_ranges) {
        if (!lr2.seen || !lr2.is_live_at_instr(i) || (&lr1 == &lr2))
          continue;
        // if lr1 is assigned...
        auto& ass1 = lr1.get(i);
        if (ass1.kind != Assignment::Kind::UNASSIGNED) {
          auto& ass2 = lr2.get(i);
          if (ass1.occupies_same_reg(ass2)) {
            // todo, this error won't be helpful
            fmt::print(
                "[RegAlloc Error] Cannot satisfy constraints at instruction {} due to constraints "
                "on {} and {}\n",
                i, lr1.var, lr2.var);
            ok = false;
          }
        }
      }
    }
  }
  return ok;
}

namespace {

/*!
 * Assign variable to register. Don't check if its safe. If it's already assigned, and this would
 * change that assignment, throw.
 */
void assign_var_no_check(int var, Assignment ass, RegAllocCache* cache) {
  cache->live_ranges.at(var).assign_no_overwrite(ass);
}

/*!
 * Can var be assigned to ass?
 */
bool can_var_be_assigned(int var,
                         Assignment ass,
                         RegAllocCache* cache,
                         const AllocationInput& in,
                         int debug_trace) {
  // our live range:
  auto& lr = cache->live_ranges.at(var);
  // check against all other live ranges:
  for (auto& other_lr : cache->live_ranges) {
    if (other_lr.var == var /*|| !other_lr.seen*/)
      continue;  // but not us!
    for (int instr = lr.min; instr <= lr.max; instr++) {
      if (other_lr.is_live_at_instr(instr)) {
        // LR's overlap
        if (/*(instr != other_lr.max) && */ other_lr.conflicts_at(instr, ass)) {
          bool allowed_by_move_eliminator = false;
          if (move_eliminator) {
            if (enable_fancy_coloring) {
              if (lr.dies_next_at_instr(instr) && other_lr.becomes_live_at_instr(instr) &&
                  in.instructions.at(instr).is_move) {
                allowed_by_move_eliminator = true;
              }

              if (lr.becomes_live_at_instr(instr) && other_lr.dies_next_at_instr(instr) &&
                  in.instructions.at(instr).is_move) {
                allowed_by_move_eliminator = true;
              }
            } else {
              // case to allow rename (from us to them)
              if (instr == lr.max && instr == other_lr.min && in.instructions.at(instr).is_move) {
                allowed_by_move_eliminator = true;
              }

              if (instr == lr.min && instr == other_lr.min && in.instructions.at(instr).is_move) {
                allowed_by_move_eliminator = true;
              }
            }
          }

          if (!allowed_by_move_eliminator) {
            if (debug_trace >= 2) {
              printf("at idx %d, %s conflicts\n", instr, other_lr.print_assignment().c_str());
            }

            return false;
          }
        }
      }
    }
  }

  // can clobber on the last one or first one - check that we don't interfere with a clobber
  for (int instr = lr.min + 1; instr <= lr.max - 1; instr++) {
    for (auto clobber : in.instructions.at(instr).clobber) {
      if (ass.occupies_reg(clobber)) {
        if (debug_trace >= 2) {
          printf("at idx %d clobber\n", instr);
        }

        return false;
      }
    }
  }

  for (int instr = lr.min; instr <= lr.max; instr++) {
    for (auto exclusive : in.instructions.at(instr).exclude) {
      if (ass.occupies_reg(exclusive)) {
        if (debug_trace >= 2) {
          printf("at idx %d exclusive conflict\n", instr);
        }

        return false;
      }
    }
  }

  // check we don't violate any others.
  for (int instr = lr.min; instr <= lr.max; instr++) {
    if (lr.has_constraint && lr.assignment.at(instr - lr.min).is_assigned()) {
      if (!(ass.occupies_same_reg(lr.assignment.at(instr - lr.min)))) {
        if (debug_trace >= 2) {
          printf("at idx %d self bad (%s) (%s)\n", instr,
                 lr.assignment.at(instr - lr.min).to_string().c_str(), ass.to_string().c_str());
        }

        return false;
      }
    }
  }

  return true;
}

bool assignment_ok_at(int var,
                      int idx,
                      Assignment ass,
                      RegAllocCache* cache,
                      const AllocationInput& in,
                      int debug_trace) {
  auto& lr = cache->live_ranges.at(var);
  for (auto& other_lr : cache->live_ranges) {
    if (other_lr.var == var /*|| !other_lr.seen*/)
      continue;
    if (other_lr.is_live_at_instr(idx)) {
      if (/*(idx != other_lr.max) &&*/ other_lr.conflicts_at(idx, ass)) {
        bool allowed_by_move_eliminator = false;
        if (move_eliminator) {
          if (enable_fancy_coloring) {
            if (lr.dies_next_at_instr(idx) && other_lr.becomes_live_at_instr(idx) &&
                in.instructions.at(idx).is_move) {
              allowed_by_move_eliminator = true;
            }

            if (lr.becomes_live_at_instr(idx) && other_lr.dies_next_at_instr(idx) &&
                in.instructions.at(idx).is_move) {
              allowed_by_move_eliminator = true;
            }
          } else {
            // case to allow rename (from us to them)
            if (idx == lr.max && idx == other_lr.min && in.instructions.at(idx).is_move) {
              allowed_by_move_eliminator = true;
            }

            if (idx == lr.min && idx == other_lr.min && in.instructions.at(idx).is_move) {
              allowed_by_move_eliminator = true;
            }
          }
        }

        if (!allowed_by_move_eliminator) {
          if (debug_trace >= 2) {
            printf("at idx %d, %s conflicts\n", idx, other_lr.print_assignment().c_str());
          }
          return false;
        }
      }
    }
  }

  // check we aren't violating a clobber
  if (idx != lr.min && idx != lr.max) {
    for (auto clobber : in.instructions.at(idx).clobber) {
      if (ass.occupies_reg(clobber)) {
        if (debug_trace >= 2) {
          printf("at idx %d clobber\n", idx);
        }

        return false;
      }
    }
  }

  for (auto exclusive : in.instructions.at(idx).exclude) {
    if (ass.occupies_reg(exclusive)) {
      if (debug_trace >= 2) {
        printf("at idx %d exclusive conflict\n", idx);
      }

      return false;
    }
  }

  // check we aren't violating ourselves
  if (lr.assignment.at(idx - lr.min).is_assigned()) {
    if (!(ass.occupies_same_reg(lr.assignment.at(idx - lr.min)))) {
      if (debug_trace >= 2) {
        printf("at idx %d self bad\n", idx);
      }

      return false;
    }
  }

  return true;
}

bool try_assignment_for_var(int var,
                            Assignment ass,
                            RegAllocCache* cache,
                            const AllocationInput& in,
                            int debug_trace) {
  if (can_var_be_assigned(var, ass, cache, in, debug_trace)) {
    assign_var_no_check(var, ass, cache);
    return true;
  }
  return false;
}

int get_stack_slot_for_var(int var, RegAllocCache* cache) {
  auto kv = cache->var_to_stack_slot.find(var);
  if (kv == cache->var_to_stack_slot.end()) {
    auto slot = cache->current_stack_slot++;
    cache->var_to_stack_slot[var] = slot;
    return slot;
  } else {
    return kv->second;
  }
}

const std::vector<emitter::Register>& get_default_alloc_order_for_var_spill(int v,
                                                                            RegAllocCache* cache) {
  auto& info = cache->iregs.at(v);
  assert(info.kind != emitter::RegKind::INVALID);
  if (info.kind == emitter::RegKind::GPR) {
    return emitter::gRegInfo.get_gpr_spill_alloc_order();
  } else if (info.kind == emitter::RegKind::XMM) {
    return emitter::gRegInfo.get_xmm_spill_alloc_order();
  } else {
    assert(false);
  }
}

const std::vector<emitter::Register>& get_default_alloc_order_for_var(int v, RegAllocCache* cache) {
  auto& info = cache->iregs.at(v);
  //  assert(info.kind != emitter::RegKind::INVALID);
  if (info.kind == emitter::RegKind::GPR || info.kind == emitter::RegKind::INVALID) {
    return emitter::gRegInfo.get_gpr_alloc_order();
  } else if (info.kind == emitter::RegKind::XMM) {
    return emitter::gRegInfo.get_xmm_alloc_order();
  } else {
    assert(false);
  }
}

bool try_spill_coloring(int var, RegAllocCache* cache, const AllocationInput& in, int debug_trace) {
  // todo, reject flagged "unspillables"
  if (debug_trace >= 1) {
    printf("---- SPILL VAR %d ----\n", var);
  }

  auto& lr = cache->live_ranges.at(var);

  // possibly get a hint assignment
  Assignment hint_assignment;
  hint_assignment.kind = Assignment::Kind::UNASSIGNED;

  // loop over live range
  for (int instr = lr.min; instr <= lr.max; instr++) {
    //    bonus_instructions.at(instr).clear();
    StackOp::Op bonus;

    // we may have a constaint in here
    auto& current_assignment = lr.assignment.at(instr - lr.min);

    auto& op = in.instructions.at(instr);
    bool is_read = op.reads(var);
    bool is_written = op.writes(var);

    // we have a constraint!
    if (current_assignment.is_assigned()) {
      if (debug_trace >= 2) {
        printf("  [%02d] already assigned %s\n", instr, current_assignment.to_string().c_str());
      }

      // remember this assignment as a hint for later
      hint_assignment = current_assignment;
      // check that this assignment is ok
      if (!assignment_ok_at(var, instr, current_assignment, cache, in, debug_trace)) {
        // this shouldn't be possible with feasible constraints
        printf("-- SPILL FAILED -- IMPOSSIBLE CONSTRAINT @ %d %s. This is likely a RegAlloc bug!\n",
               instr, current_assignment.to_string().c_str());
        assert(false);
        return false;
      }

      // flag it as spilled, but currently in a GPR.
      current_assignment.spilled = true;
      bonus.reg = current_assignment.reg;
    } else {
      // not assigned.
      if (debug_trace >= 1) {
        printf("  [%02d] nya rd? %d wr? %d\n", instr, is_read, is_written);
      }

      // We'd like to keep it on the stack if possible
      Assignment spill_assignment;
      spill_assignment.spilled = true;
      spill_assignment.kind = Assignment::Kind::STACK;
      spill_assignment.reg = -1;  // for now

      // needs a temp register
      if (is_read || is_written) {
        // we need to put it in a register here!
        // first check if the hint works?
        // todo floats?
        if (hint_assignment.kind == Assignment::Kind::REGISTER) {
          if (debug_trace >= 2) {
            printf("   try hint %s\n", hint_assignment.to_string().c_str());
          }

          if (assignment_ok_at(var, instr, hint_assignment, cache, in, debug_trace)) {
            // it's ok!
            if (debug_trace >= 2) {
              printf("   it worked!\n");
            }
            spill_assignment.reg = hint_assignment.reg;
          }
        }

        // hint didn't work
        // auto reg_order = get_default_reg_alloc_order();
        auto reg_order = get_default_alloc_order_for_var_spill(var, cache);
        if (spill_assignment.reg == -1) {
          for (auto reg : reg_order) {
            Assignment ass;
            ass.kind = Assignment::Kind::REGISTER;
            ass.reg = reg;
            if (debug_trace >= 2) {
              printf("  try %s\n", ass.to_string().c_str());
            }

            if (assignment_ok_at(var, instr, ass, cache, in, debug_trace)) {
              if (debug_trace >= 2) {
                printf("  it worked!\n");
              }
              spill_assignment.reg = ass.reg;
              break;
            }
          }
        }

        if (spill_assignment.reg == -1) {
          printf("SPILLING FAILED BECAUSE WE COULDN'T FIND A TEMP REGISTER!\n");
          assert(false);
          //          std::vector<bool> can_try_spilling;
          //          for(uint32_t other_spill = 0; other_spill < was_colored.size(); other_spill++)
          //          {
          //            if((int)other_spill != var && was_colored.at(other_spill)) {
          //              LOG("TRY SPILL %d?\n", other_spill);
          //              if(try_spill_coloring(other_spill)) {
          //                LOG("SPILL OK.\n");
          //                if(try_spill_coloring(var)) {
          //                  return true;
          //                }
          //              } else {
          //                LOG("SPILL %d failed.\n", other_spill);
          //              }
          //            }
          //          }
          return false;
        }

        // mark that it's in a GPR!
        spill_assignment.kind = Assignment::Kind::REGISTER;
      }  // end need temp reg
      spill_assignment.stack_slot = get_stack_slot_for_var(var, cache);
      lr.assignment.at(instr - lr.min) = spill_assignment;
      bonus.reg = spill_assignment.reg;
      bonus.slot = spill_assignment.stack_slot;
    }  // end not constrained

    bonus.slot = get_stack_slot_for_var(var, cache);
    bonus.load = is_read;
    bonus.store = is_written;
    cache->stack_ops.at(instr).ops.push_back(bonus);
  }
  return true;
}

template <typename T>
bool in_vec(const std::vector<T>& vec, const T& obj) {
  for (const auto& x : vec) {
    if (x == obj)
      return true;
  }
  return false;
}

bool do_allocation_for_var(int var,
                           RegAllocCache* cache,
                           const AllocationInput& in,
                           int debug_trace) {
  // first, let's see if there's a hint...
  auto& lr = cache->live_ranges.at(var);
  bool colored = false;
  if (lr.best_hint.is_assigned()) {
    colored = try_assignment_for_var(var, lr.best_hint, cache, in, debug_trace);
    if (debug_trace >= 2) {
      printf("var %d reg %s ? %d\n", var, lr.best_hint.to_string().c_str(), colored);
    }
  }

  auto reg_order = get_default_alloc_order_for_var(var, cache);

  // todo, try other regs..
  if (!colored && move_eliminator) {
    auto& first_instr = in.instructions.at(lr.min);
    auto& last_instr = in.instructions.at(lr.max);

    if (first_instr.is_move) {
      auto& possible_coloring = cache->live_ranges.at(first_instr.read.front().id).get(lr.min);
      if (possible_coloring.is_assigned() && in_vec(reg_order, possible_coloring.reg)) {
        colored = try_assignment_for_var(var, possible_coloring, cache, in, debug_trace);
      }
    }

    if (!colored && last_instr.is_move) {
      auto& possible_coloring = cache->live_ranges.at(last_instr.write.front().id).get(lr.max);
      if (possible_coloring.is_assigned() && in_vec(reg_order, possible_coloring.reg)) {
        colored = try_assignment_for_var(var, possible_coloring, cache, in, debug_trace);
      }
    }
  }

  // auto reg_order = get_default_reg_alloc_order();

  for (auto reg : reg_order) {
    if (colored)
      break;
    Assignment ass;
    ass.kind = Assignment::Kind::REGISTER;
    ass.reg = reg;
    colored = try_assignment_for_var(var, ass, cache, in, debug_trace);
    if (debug_trace >= 1) {
      printf("var %d reg %s ? %d\n", var, ass.to_string().c_str(), colored);
    }
  }

  if (!colored) {
    colored = try_spill_coloring(var, cache, in, debug_trace);
    if (colored) {
      cache->used_stack = true;
    }
  }

  // todo, try spilling
  if (!colored) {
    printf("[ERROR] var %d could not be colored:\n%s\n", var,
           cache->live_ranges.at(var).print_assignment().c_str());

    return false;
  } else {
    if (debug_trace >= 2) {
      printf("Colored var %d\n", var);
    }

    cache->was_colored.at(var) = true;
    return true;
  }
}

}  // namespace

bool run_allocator(RegAllocCache* cache, const AllocationInput& in, int debug_trace) {
  // here we allocate
  std::vector<int> allocation_order;
  for (uint32_t i = 0; i < cache->live_ranges.size(); i++) {
    if (cache->live_ranges.at(i).seen && cache->live_ranges.at(i).has_constraint) {
      allocation_order.push_back(i);
    }
  }

  for (uint32_t i = 0; i < cache->live_ranges.size(); i++) {
    if (cache->live_ranges.at(i).seen && !cache->live_ranges.at(i).has_constraint) {
      allocation_order.push_back(i);
    }
  }

  for (int var : allocation_order) {
    if (!do_allocation_for_var(var, cache, in, debug_trace)) {
      return false;
    }
  }
  return true;
}