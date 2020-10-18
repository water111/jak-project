#pragma once

#include <vector>
#include <memory>
#include <set>

#include "CfgVtx.h"
#include "decompiler/Disasm/Register.h"

class LinkedObjectFile;
class Function;

struct BasicBlock {
  int start_word;
  int end_word;

  std::vector<int> pred;
  int succ_ft = -1;
  int succ_branch = -1;

  // these are all in terms of basic ops.
  std::vector<int> basic_ops;
  std::vector<std::set<Register>> live, dead;
  std::set<Register> use, defs;
  std::set<Register> input, output;

  int size() const { return end_word - start_word; }

  int at(int i) const {
    assert(i >= 0 && i < size());
    return start_word + i;
  }

  BasicBlock(int _start_word, int _end_word) : start_word(_start_word), end_word(_end_word) {}
};

std::vector<BasicBlock> find_blocks_in_function(const LinkedObjectFile& file,
                                                int seg,
                                                const Function& func);
