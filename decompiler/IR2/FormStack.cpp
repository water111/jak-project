#include "FormStack.h"
#include "Form.h"

namespace decompiler {
std::string FormStack::StackEntry::print(const Env& env) const {
  if (destination.has_value()) {
    assert(source && !elt);
    return fmt::format("d: {} s: {} | {} <- {} f: {}", active, sequence_point,
                       destination.value().reg().to_charp(), source->to_string(env),
                       non_seq_source.has_value());
  } else {
    assert(elt && !source);
    return fmt::format("d: {} s: {} | {} f: {}", active, sequence_point, elt->to_string(env),
                       non_seq_source.has_value());
  }
}

std::string FormStack::print(const Env& env) {
  std::string result;
  for (auto& x : m_stack) {
    result += x.print(env);
    result += '\n';
  }
  return result;
}

void FormStack::push_value_to_reg(Variable var, Form* value, bool sequence_point) {
  StackEntry entry;
  entry.active = true;  // by default, we should display everything!
  entry.sequence_point = sequence_point;
  entry.destination = var;
  entry.source = value;
  m_stack.push_back(entry);
}

void FormStack::push_non_seq_reg_to_reg(const Variable& dst,
                                        const Variable& src,
                                        Form* src_as_form) {
  StackEntry entry;
  entry.active = true;
  entry.sequence_point = false;
  entry.destination = dst;
  entry.non_seq_source = src;
  entry.source = src_as_form;
  m_stack.push_back(entry);
}

bool FormStack::is_single_expression() {
  int count = 0;
  for (auto& e : m_stack) {
    if (e.active) {
      count++;
    }
  }
  return count == 1;
}

void FormStack::push_form_element(FormElement* elt, bool sequence_point) {
  StackEntry entry;
  entry.active = true;
  entry.elt = elt;
  entry.sequence_point = sequence_point;
  m_stack.push_back(entry);
}

std::vector<Form*> FormStack::pop(const std::vector<Register>& regs, const Env& env) {
//  fmt::print("pop {}\n", regs.front().to_string());
  std::vector<Form*> result;

  // first, we build up a list of how far back we can safely go.
  // entry_idx = max_map[reg] is the lowest value of entry_idx we can use
  std::unordered_map<Register, int, Register::hash> max_map;

  RegSet modified;
  RegSet unmodified(regs.begin(), regs.end());
  for (size_t i = m_stack.size(); i-- > 0;) {
    // update modified
    auto& entry = m_stack.at(i);
    if (entry.source) {
      assert(!entry.elt);
      entry.source->get_modified_regs(modified);
    } else {
      assert(entry.elt);
      entry.elt->get_modified_regs(modified);
    }

    // update unmodified and map
    for (auto it = unmodified.begin(); it != unmodified.end();) {
      if (modified.find(*it) != modified.end()) {
        // modified at i. two cases here:
        if (entry.destination.has_value() && entry.destination->reg() == *it) {
          // 1). the entry at i writes the reg. it is safe to use this entry.
          bool added = max_map.insert(std::make_pair(*it, i)).second;
          assert(added);
        } else {
          // 2). the entry at i doesn't directly write the reg. Unsafe to use.
          bool added = max_map.insert(std::make_pair(*it, i + 1)).second;
          assert(added);
        }
        it = unmodified.erase(it);
      } else {
        it++;
      }
    }
  }
  // never modified ever.
  for (auto reg : unmodified) {
    bool added = max_map.insert(std::make_pair(reg, 0)).second;
    assert(added);
  }

//  fmt::print("max: {}\n", max_map[regs.front()]);
//  fmt::print("stack:\n{}\n", print(env));

  // second, we build up a collection of where to pop things.
  // this should line up with the registers exactly
  std::vector<int> pop_idx_per_reg;
  pop_idx_per_reg.resize(regs.size(), -1);

  bool success = true;
  size_t stack_idx = m_stack.size();
  for (size_t reg_idx = regs.size(); reg_idx-- > 0;) {
    if (stack_idx == 0) {
      success = false;
      break;
    }
    for (; stack_idx-- > 0;) {
      auto& entry = m_stack.at(stack_idx);
      if (entry.active) {
        if (entry.destination.has_value() && entry.destination->reg() == regs.at(reg_idx)) {
//          fmt::print("got a match!\n");
          // it's a match!
          assert(pop_idx_per_reg.at(reg_idx) == -1);
          pop_idx_per_reg.at(reg_idx) = stack_idx;
          break;
        } else {
          if (entry.sequence_point) {
//            fmt::print("failed to match, hit sequence point.\n");
            success = false;
            goto end_of_pops;
          }
        }
      }
      if (stack_idx == 0) {
        success = false;
        goto end_of_pops;
      }
    }
  }

end_of_pops:

  if (success) {
    // third: if successful, double check for success in the map:
    for (size_t i = 0; i < regs.size(); i++) {
      if (pop_idx_per_reg.at(i) < max_map.at(regs.at(i))) {
        fmt::print("Failed map check {} vs. {}\n", pop_idx_per_reg.at(i), max_map.at(regs.at(i)));
            success = false;
        break;
      }
    }
  }

  if (success) {
    // ok, we are successful for real!
    for (size_t i = 0; i < regs.size(); i++) {
      auto& entry = m_stack.at(pop_idx_per_reg.at(i));
      assert(entry.destination.has_value());
      assert(entry.destination->reg() == regs.at(i));
      assert(entry.active);
      entry.active = false;
      result.push_back(entry.source);
      assert(entry.source);
    }

    assert(result.size() == regs.size());
//    fmt::print("it worked, got {}\n", result.front()->to_string(env));
    return result;
  } else {

    return {};
  }
}

// Form* FormStack::pop_reg(Register reg) {
//  for (size_t i = m_stack.size(); i-- > 0;) {
//    auto& entry = m_stack.at(i);
//    if (entry.active) {
//      if (entry.destination.has_value() && entry.destination->reg() == reg) {
//        entry.active = false;
//        assert(entry.source);
//        if (entry.non_seq_source.has_value()) {
//          assert(entry.sequence_point == false);
//          auto result = pop_reg(entry.non_seq_source->reg());
//          if (result) {
//            return result;
//          }
//        }
//        return entry.source;
//      } else {
//        // we didn't match
//        if (entry.sequence_point) {
//          // and it's a sequence point! can't look any more back than this.
//          return nullptr;
//        }
//      }
//    }
//  }
//  // we didn't have it...
//  return nullptr;
//}

// Form* FormStack::pop_reg(const Variable& var) {
//  return pop_reg(var.reg());
//}

std::vector<FormElement*> FormStack::rewrite(FormPool& pool) {
  std::vector<FormElement*> result;

  for (auto& e : m_stack) {
    if (!e.active) {
      continue;
    }

    if (e.destination.has_value()) {
      auto elt = pool.alloc_element<SetVarElement>(*e.destination, e.source, e.sequence_point);
      e.source->parent_element = elt;
      result.push_back(elt);
    } else {
      result.push_back(e.elt);
    }
  }
  return result;
}

std::vector<FormElement*> FormStack::rewrite_to_get_var(FormPool& pool,
                                                        const Variable& var,
                                                        const Env&) {
  // first, rewrite as normal.
  auto default_result = rewrite(pool);

  // try a few different ways to "naturally" rewrite this so the value of the form is the
  // value in the given register.

  auto last_op_as_set = dynamic_cast<SetVarElement*>(default_result.back());
  if (last_op_as_set && last_op_as_set->dst().reg() == var.reg()) {
    default_result.pop_back();
    for (auto form : last_op_as_set->src()->elts()) {
      form->parent_form = nullptr;  // will get set later, this makes it obvious if I forget.
      default_result.push_back(form);
    }
    return default_result;
  } else {
    default_result.push_back(pool.alloc_element<SimpleAtomElement>(SimpleAtom::make_var(var)));
    return default_result;
  }
}
}  // namespace decompiler