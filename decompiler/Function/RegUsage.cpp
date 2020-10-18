#include "RegUsage.h"
#include "Function.h"
#include "third-party/fmt/format.h"

namespace {
template <typename T>
static bool in_set(std::set<T>& set, const T& obj) {
  return set.find(obj) != set.end();
}

void setup_basic_ops_in_block(Function& f) {
  for (auto& block : f.basic_blocks) {
    for (int i = block.start_word; i < block.end_word; i++) {
      if (f.instr_starts_basic_op(i)) {
        auto basic_op_idx = f.instruction_to_basic_op.at(i);
        auto basic_op = f.get_basic_op_at_instr(i);
        assert(basic_op->is_basic_op);
        block.basic_ops.push_back(basic_op_idx);
      }
    }
  }
}

void phase1(BasicBlock& block, Function& f, LinkedObjectFile& file) {
  for (int i = block.basic_ops.size(); i-- > 0;) {
    auto& instr = f.basic_ops.at(block.basic_ops.at(i));
    auto& lv = block.live.at(i);
    auto& dd = block.dead.at(i);

    // make all read live out
    auto read = instr->get_read(file);
    lv.clear();
    for (auto& x : read) {
      lv.insert(x);
    }

    // kill things which are overwritten
    dd.clear();
    auto write = instr->get_write(file);
    for (auto& x : write) {
      if (!in_set(lv, x)) {
        dd.insert(x);
      }
    }

    // b.use = i.liveout
    std::set<Register> use_old = block.use;
    block.use.clear();
    for (auto& x : lv) {
      block.use.insert(x);
    }
    // | (bu.use & !i.dead)
    for (auto& x : use_old) {
      if (!in_set(dd, x)) {
        block.use.insert(x);
      }
    }

    // b.defs = i.dead
    std::set<Register> defs_old = block.defs;
    block.defs.clear();
    for (auto& x : dd) {
      block.defs.insert(x);
    }
    // | b.defs & !i.lv
    for (auto& x : defs_old) {
      if (!in_set(lv, x)) {
        block.defs.insert(x);
      }
    }
  }
}

bool phase2(std::vector<BasicBlock>& blocks, BasicBlock& block) {
  bool changed = false;
  auto out = block.defs;

  for (auto s : {block.succ_ft, block.succ_branch}) {
    for (auto in : blocks.at(s).input) {
      out.insert(in);
    }
  }

  std::set<Register> in = block.use;
  for (auto x : out) {
    if (!in_set(block.defs, x)) {
      in.insert(x);
    }
  }

  if (in != block.input || out != block.output) {
    changed = true;
    block.input = in;
    block.output = out;
  }

  return changed;
}

void phase3(std::vector<BasicBlock>& blocks, BasicBlock& block) {
  std::set<Register> live_local;
  for (auto s : {block.succ_ft, block.succ_branch}) {
    for (auto i : blocks.at(s).input) {
      live_local.insert(i);
    }
  }

  for (int i = block.basic_ops.size(); i-- > 0;) {
    auto& lv = block.live.at(i);
    auto& dd = block.dead.at(i);

    std::set<Register> new_live = lv;
    for (auto x : live_local) {
      if (!in_set(dd, x)) {
        new_live.insert(x);
      }
    }
    lv = live_local;
    live_local = new_live;
  }
}

}  // namespace

void analyze_reg_usage(Function& f, LinkedObjectFile& file) {
  setup_basic_ops_in_block(f);
  for (auto& block : f.basic_blocks) {
    block.live.resize(block.basic_ops.size());
    block.dead.resize(block.basic_ops.size());
    phase1(block, f, file);
  }

  // phase 2
  bool changed = false;
  do {
    changed = false;
    for (auto& block : f.basic_blocks) {
      if (phase2(f.basic_blocks, block)) {
        changed = true;
      }
    }
  } while (changed);

  // phase 3
  for (auto& block : f.basic_blocks) {
    phase3(f.basic_blocks, block);
  }
}