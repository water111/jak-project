#include "insert_lets.h"

namespace decompiler {

/*
Part 1:
Create a std::unordered_map<ProgVar, std::vector<FormElement*>> which maps a program variable to the
collection of FormElement* which reference it.

Part 2:
For each ProgVar, find the lowest common ancestor Form* of the FormElement*'s in the above map.

Part 3:
For each Form*, find the smallest range of FormElement*s which include all uses of the ProgVar

Part 4:
Sort these from the largest to smaller range.

This makes sure that at a single level (in the original tree), we insert larger lets first, leaving
us with only one nesting case to worry about in the next step.

Check the first FormElement* which uses the ProgVar.
If it is a (set! var xxx), then we can insert a let.

If we are inserting directly inside of another let, at the beginning of that let's body, add to that
let. This makes the scope larger than it needs to be, but this seems like it will lead to more
readable code.

If the previous let variables appear in the definition of new one, make the let into a let*
 */

namespace {
std::vector<Form*> path_up_tree(Form* in) {
  std::vector<Form*> path;

  while (in) {
    path.push_back(in);
    // lg::warn("In: {}\n", in->to_string(env));
    if (in->parent_element) {
      // lg::warn("  {}\n", in->parent_element->to_string(env));
      in = in->parent_element->parent_form;
    } else {
      in = nullptr;
    }
  }
  return path;
}

Form* lca_form(Form* a, Form* b, const Env& env) {
  (void)env;
  if (!a) {
    return b;
  }

  auto a_up = path_up_tree(a);
  auto b_up = path_up_tree(b);

  //fmt::print("lca {} and {}\n", a->to_string(env), b->to_string(env));

  int ai = a_up.size() - 1;
  int bi = b_up.size() - 1;

  Form* result = nullptr;
  while (ai >= 0 && bi >= 0) {
    if (a_up.at(ai) == b_up.at(bi)) {
      result = a_up.at(ai);
    } else {
      break;
    }
    ai--;
    bi--;
  }
  assert(result);

  //fmt::print("{}\n\n", result->to_string(env));
  return result;
}
}  // namespace

LetStats insert_lets(const Function& func, Env& env, FormPool& pool, Form* top_level_form) {
  (void)func;
  //    if (func.guessed_name.to_string() != "(method 4 pair)") {
  //      return {};
  //    }
  LetStats stats;

  // Stored per variable.
  struct PerVarInfo {
    std::string var_name;  // name used to uniquely identify
    RegisterAccess access;
    std::unordered_set<FormElement*> elts_using_var;  // all FormElements using var
    Form* lca_form = nullptr;  // the lowest common form that contains all the above elts
    int start_idx = -1;        // in the above form, first FormElement using var's index
    int end_idx = -1;          // in the above form, 1 + last FormElement using var's index
  };

  std::unordered_map<std::string, PerVarInfo> var_info;

  // Part 1, figure out which forms reference each var
  top_level_form->apply([&](FormElement* elt) {
    // for each element, figure out what vars we reference:
    RegAccessSet reg_accesses;
    elt->collect_vars(reg_accesses);

    // and add it.
    for (auto& access : reg_accesses) {
      if (access.reg().get_kind() == Reg::FPR || access.reg().get_kind() == Reg::GPR) {
        auto name = env.get_variable_name(access);
        var_info[name].elts_using_var.insert(elt);
        var_info[name].var_name = name;
        var_info[name].access = access;
      }
    }
  });

  stats.total_vars = var_info.size();

  // Part 2, figure out the lca form which contains all uses of a var
  for (auto& kv : var_info) {
    // fmt::print("--------------------- {}\n", kv.first);
    Form* lca = nullptr;
    for (auto fe : kv.second.elts_using_var) {
      lca = lca_form(lca, fe->parent_form, env);
    }
    assert(lca);
    var_info[kv.first].lca_form = lca;
  }

  // Part 3, find the minimum range of FormElement's within the lca form that contain
  // all uses. This is the minimum possible range for a set!
  for (auto& kv : var_info) {
    // fmt::print("Setting range for let {}\n", kv.first);
    kv.second.start_idx = std::numeric_limits<int>::max();
    kv.second.end_idx = std::numeric_limits<int>::min();

    bool got_one = false;
    for (int i = 0; i < kv.second.lca_form->size(); i++) {
      if (kv.second.elts_using_var.find(kv.second.lca_form->at(i)) !=
          kv.second.elts_using_var.end()) {
        got_one = true;
        kv.second.start_idx = std::min(kv.second.start_idx, i);
        kv.second.end_idx = std::max(kv.second.end_idx, i + 1);
        // fmt::print("update range {} to {} because of {}\n", kv.second.start_idx,
        // kv.second.end_idx, kv.second.lca_form->at(i)->to_string(env));
      }
    }
    assert(got_one);
  }

  // fmt::print("\n");

  // Part 4, sort the var infos in descending size.
  // this simplifies future passes.
  std::vector<PerVarInfo> sorted_info;
  for (auto& kv : var_info) {
    sorted_info.push_back(kv.second);
  }
  std::sort(sorted_info.begin(), sorted_info.end(), [](const PerVarInfo& a, const PerVarInfo& b) {
    return (a.end_idx - a.start_idx) > (b.end_idx - b.start_idx);
  });

  // Part 5, find where we want to insert lets.  But don't actually do any insertions.
  // Only variables that begin with a set! var value can be used in a let, so we may discard
  // some variables here.  Though I suspect most reasonable functions will not discard any.
  struct LetInsertion {
    Form* form = nullptr;
    int start_elt = -1;  // this is the set!
    SetVarElement* set_form = nullptr;
    int end_elt = -1;
    std::string name;
  };

  // stored per containing form.
  std::unordered_map<Form*, std::vector<LetInsertion>> possible_insertions;
  for (auto& info : sorted_info) {
    auto first_form = info.lca_form->at(info.start_idx);
    auto first_form_as_set = dynamic_cast<SetVarElement*>(first_form);
    if (first_form_as_set &&
        env.get_variable_name(first_form_as_set->dst()) == env.get_variable_name(info.access) &&
        !first_form_as_set->info().is_eliminated_coloring_move) {
      // success!
      // fmt::print("Want let for {} range {} to {}\n",
      // env.get_variable_name(first_form_as_set->dst()), info.start_idx, info.end_idx);
      LetInsertion li;
      li.form = info.lca_form;
      li.start_elt = info.start_idx;
      li.end_elt = info.end_idx;
      li.set_form = first_form_as_set;
      li.name = info.var_name;
      possible_insertions[li.form].push_back(li);
      stats.vars_in_lets++;
    } else {
      // fmt::print("fail for {} : {}\n", info.var_name, first_form->to_string(env));
    }
  }

  // Part 6, expand ends of intervals to prevent "tangled lets"
  for (auto& group : possible_insertions) {
    // Note : this algorithm is not efficient.
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto& let_a : group.second) {
        for (auto& let_b : group.second) {
          // If b starts within a and ends after a, expand a.
          if (let_b.start_elt > let_a.start_elt && let_b.start_elt < let_a.end_elt &&
              let_b.end_elt > let_a.end_elt) {
            changed = true;
            // fmt::print("Resized {}'s end to {}\n", let_a.set_form->dst().to_string(env),
            // let_b.end_elt);
            let_a.end_elt = let_b.end_elt;
          }
        }
      }
    }
  }

  // Part 7: insert lets!
  for (auto& group : possible_insertions) {
    // sort decreasing size.
    std::sort(group.second.begin(), group.second.end(),
              [](const LetInsertion& a, const LetInsertion& b) {
                return (a.end_elt - a.start_elt) > (b.end_elt - b.start_elt);
              });

    // ownership[elt_idx] = the let which actually has this.
    std::vector<int> ownership;
    ownership.resize(group.first->size(), -1);
    for (int let_idx = 0; let_idx < int(group.second.size()); let_idx++) {
      for (int elt_idx = group.second.at(let_idx).start_elt;
           elt_idx < group.second.at(let_idx).end_elt; elt_idx++) {
        ownership.at(elt_idx) = let_idx;
      }
    }

    // build lets
    std::vector<LetElement*> lets;
    lets.resize(group.first->size(), nullptr);
    // start at the smallest.
    for (size_t let_idx = group.second.size(); let_idx-- > 0;) {
      auto& let_desc = group.second.at(let_idx);
      std::vector<FormElement*> body;
      int elt_idx = let_desc.start_elt + 1;  // plus one to skip the variable def.
      while (elt_idx < let_desc.end_elt) {
        if (ownership.at(elt_idx) == int(let_idx)) {
          body.push_back(let_desc.form->at(elt_idx));
          elt_idx++;
        } else {
          auto existing_let = lets.at(ownership[elt_idx]);
          assert(existing_let);
          auto& existing_let_info = group.second.at(ownership[elt_idx]);
          assert(existing_let_info.start_elt == elt_idx);
          body.push_back(existing_let);
          elt_idx = existing_let_info.end_elt;
        }
      }
      assert(elt_idx == let_desc.end_elt);
      auto new_let = pool.alloc_element<LetElement>(pool.alloc_sequence_form(nullptr, body));
      new_let->add_def(let_desc.set_form->dst(), let_desc.set_form->src());
      env.set_defined_in_let(let_desc.name);
      lets.at(let_idx) = new_let;
    }

    // now rebuild form
    int elt_idx = 0;
    std::vector<FormElement*> new_body;
    while (elt_idx < group.first->size()) {
      if (ownership.at(elt_idx) == -1) {
        new_body.push_back(group.first->at(elt_idx));
        elt_idx++;
      } else {
        auto existing_let = lets.at(ownership[elt_idx]);
        assert(existing_let);
        auto& existing_let_info = group.second.at(ownership[elt_idx]);
        assert(existing_let_info.start_elt == elt_idx);
        new_body.push_back(existing_let);
        elt_idx = existing_let_info.end_elt;
      }
    }
    assert(elt_idx == group.first->size());

    group.first->elts() = new_body;
    group.first->claim_all_children();
  }

  // Part 8: (todo) recognize loops and stuff.

  // Part 9: compact recursive lets:
  bool changed = true;
  while (changed) {
    changed = false;
    top_level_form->apply([&](FormElement* f) {
      auto as_let = dynamic_cast<LetElement*>(f);
      if (!as_let) {
        return;
      }

      auto inner_let = dynamic_cast<LetElement*>(as_let->body()->try_as_single_element());
      if (!inner_let) {
        return;
      }

      for (auto& e : inner_let->entries()) {
        as_let->add_entry(e);
      }

      as_let->set_body(inner_let->body());
      changed = true;
    });
  }

  return stats;
}

}  // namespace decompiler