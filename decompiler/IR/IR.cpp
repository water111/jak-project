#include "IR.h"
#include "decompiler/ObjectFile/LinkedObjectFile.h"

namespace {
void read_helper(IR* in, std::vector<Register>* out, LinkedObjectFile& file) {
  auto in_as_reg = dynamic_cast<IR_Register*>(in);
  if (in_as_reg) {
    out->push_back(in_as_reg->reg);
    return;
  }
  auto in_as_int = dynamic_cast<IR_IntegerConstant*>(in);
  if (in_as_int) {
    return;
  } else {
    auto temp = in->get_read(file);
    out->insert(out->end(), temp.begin(), temp.end());
    return;
  }
}
}  // namespace

//////////////
/// IR
//////////////

std::vector<std::shared_ptr<IR>> IR::get_all_ir(LinkedObjectFile& file) const {
  (void)file;
  std::vector<std::shared_ptr<IR>> result;
  get_children(&result);
  size_t last_checked = 0;
  size_t last_last_checked = -1;

  while (last_checked != last_last_checked) {
    last_last_checked = last_checked;
    auto end_of_check = result.size();
    for (size_t i = last_checked; i < end_of_check; i++) {
      auto it = result.at(i).get();
      assert(it);
      it->get_children(&result);
    }
    last_checked = end_of_check;
  }

  return result;
}

std::string IR::print(const LinkedObjectFile& file) const {
  return pretty_print::to_string(to_form(file));
}

bool IR::update_types(TypeMap& reg_types, DecompilerTypeSystem& dts, LinkedObjectFile& file) const {
  (void)reg_types;
  (void)dts;
  (void)file;
  return false;
}

std::vector<Register> IR::get_read(LinkedObjectFile& file) const {
  throw std::runtime_error("IR get_read not yet implemented for " + print(file));
}

std::vector<Register> IR::get_write(LinkedObjectFile& file) const {
  throw std::runtime_error("IR get_write not yet implemented for " + print(file));
}

//////////////
/// IR Failed
//////////////

goos::Object IR_Failed::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::build_list("INVALID-OPERATION");
}

void IR_Failed::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

//////////////
/// IR_Register
//////////////

goos::Object IR_Register::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::to_symbol(reg.to_charp());
}

void IR_Register::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

//////////////
/// IR_Set
//////////////

goos::Object IR_Set::to_form(const LinkedObjectFile& file) const {
  return pretty_print::build_list(pretty_print::to_symbol("set!"), dst->to_form(file),
                                  src->to_form(file));
}

void IR_Set::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  // note that we are not returning clobber here because it shouldn't contain anything that
  // the IR simplification code should touch.
  output->push_back(dst);
  output->push_back(src);
}

goos::Object IR_Store::to_form(const LinkedObjectFile& file) const {
  std::string store_operator;
  switch (kind) {
    case FLOAT:
      store_operator = "s.f";
      break;
    case INTEGER:
      switch (size) {
        case 1:
          store_operator = "s.b";
          break;
        case 2:
          store_operator = "s.h";
          break;
        case 4:
          store_operator = "s.w";
          break;
        case 8:
          store_operator = "s.d";
          break;
        case 16:
          store_operator = "s.q";
          break;
        default:
          assert(false);
      }
      break;
    default:
      assert(false);
  }

  return pretty_print::build_list(pretty_print::to_symbol(store_operator), dst->to_form(file),
                                  src->to_form(file));
}

std::vector<Register> IR_Set::get_read(LinkedObjectFile& file) const {
  std::vector<Register> result;
  read_helper(src.get(), &result, file);
  return result;
}

std::vector<Register> IR_Set::get_write(LinkedObjectFile& file) const {
  (void)file;
  auto dst_as_reg = dynamic_cast<IR_Register*>(dst.get());
  assert(dst_as_reg);
  assert(is_basic_op);  // note that after expression compaction an IR_Set can have side effects.
  return {dst_as_reg->reg};
}

goos::Object IR_Symbol::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::to_symbol("'" + name);
}

void IR_Symbol::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_SymbolValue::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::to_symbol(name);
}

void IR_SymbolValue::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_StaticAddress::to_form(const LinkedObjectFile& file) const {
  // return pretty_print::build_list(pretty_print::to_symbol("&"), file.get_label_name(label_id));
  return pretty_print::to_symbol(file.get_label_name(label_id));
}

void IR_StaticAddress::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_Load::to_form(const LinkedObjectFile& file) const {
  std::string load_operator;
  switch (kind) {
    case FLOAT:
      load_operator = "l.f";
      break;
    case UNSIGNED:
      switch (size) {
        case 1:
          load_operator = "l.bu";
          break;
        case 2:
          load_operator = "l.hu";
          break;
        case 4:
          load_operator = "l.wu";
          break;
        case 8:
          load_operator = "l.d";
          break;
        case 16:
          load_operator = "l.q";
          break;
        default:
          assert(false);
      }
      break;
    case SIGNED:
      switch (size) {
        case 1:
          load_operator = "l.bs";
          break;
        case 2:
          load_operator = "l.hs";
          break;
        case 4:
          load_operator = "l.ws";
          break;
        default:
          assert(false);
      }
      break;
    default:
      assert(false);
  }
  return pretty_print::build_list(pretty_print::to_symbol(load_operator), location->to_form(file));
}

void IR_Load::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(location);
}

goos::Object IR_FloatMath2::to_form(const LinkedObjectFile& file) const {
  std::string math_operator;
  switch (kind) {
    case DIV:
      math_operator = "/.f";
      break;
    case MUL:
      math_operator = "*.f";
      break;
    case ADD:
      math_operator = "+.f";
      break;
    case SUB:
      math_operator = "-.f";
      break;
    case MIN:
      math_operator = "min.f";
      break;
    case MAX:
      math_operator = "max.f";
      break;
    default:
      assert(false);
  }

  return pretty_print::build_list(pretty_print::to_symbol(math_operator), arg0->to_form(file),
                                  arg1->to_form(file));
}

void IR_FloatMath2::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(arg0);
  output->push_back(arg1);
}

void IR_FloatMath1::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(arg);
}

goos::Object IR_IntMath2::to_form(const LinkedObjectFile& file) const {
  std::string math_operator;
  switch (kind) {
    case ADD:
      math_operator = "+.i";
      break;
    case SUB:
      math_operator = "-.i";
      break;
    case MUL_SIGNED:
      math_operator = "*.si";
      break;
    case MUL_UNSIGNED:
      math_operator = "*.ui";
      break;
    case DIV_SIGNED:
      math_operator = "/.si";
      break;
    case MOD_SIGNED:
      math_operator = "mod.si";
      break;
    case DIV_UNSIGNED:
      math_operator = "/.ui";
      break;
    case MOD_UNSIGNED:
      math_operator = "mod.ui";
      break;
    case OR:
      math_operator = "logior";
      break;
    case AND:
      math_operator = "logand";
      break;
    case NOR:
      math_operator = "lognor";
      break;
    case XOR:
      math_operator = "logxor";
      break;
    case LEFT_SHIFT:
      math_operator = "shl";
      break;
    case RIGHT_SHIFT_ARITH:
      math_operator = "sar";
      break;
    case RIGHT_SHIFT_LOGIC:
      math_operator = "shr";
      break;
    case MIN_SIGNED:
      math_operator = "min.si";
      break;
    case MAX_SIGNED:
      math_operator = "max.si";
      break;
    default:
      assert(false);
  }
  return pretty_print::build_list(pretty_print::to_symbol(math_operator), arg0->to_form(file),
                                  arg1->to_form(file));
}

void IR_IntMath2::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(arg0);
  output->push_back(arg1);
}

std::vector<Register> IR_IntMath2::get_read(LinkedObjectFile& file) const {
  std::vector<Register> result;
  for (auto& src : {arg0, arg1}) {
    read_helper(src.get(), &result, file);
  }
  return result;
}

goos::Object IR_IntMath1::to_form(const LinkedObjectFile& file) const {
  std::string math_operator;
  switch (kind) {
    case NOT:
      math_operator = "lognot";
      break;
    case ABS:
      math_operator = "abs.si";
      break;
    case NEG:
      math_operator = "-.i";
      break;
    default:
      assert(false);
  }
  return pretty_print::build_list(pretty_print::to_symbol(math_operator), arg->to_form(file));
}

void IR_IntMath1::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(arg);
}

goos::Object IR_FloatMath1::to_form(const LinkedObjectFile& file) const {
  std::string math_operator;
  switch (kind) {
    case FLOAT_TO_INT:
      math_operator = "int<-float";
      break;
    case INT_TO_FLOAT:
      math_operator = "float<-int";
      break;
    case ABS:
      math_operator = "abs.f";
      break;
    case NEG:
      math_operator = "neg.f";
      break;
    case SQRT:
      math_operator = "sqrt.f";
      break;
    default:
      assert(false);
  }
  return pretty_print::build_list(pretty_print::to_symbol(math_operator), arg->to_form(file));
}

goos::Object IR_Call::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::build_list("call!");
}

void IR_Call::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_IntegerConstant::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::to_symbol(std::to_string(value));
}

void IR_IntegerConstant::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object BranchDelay::to_form(const LinkedObjectFile& file) const {
  (void)file;
  switch (kind) {
    case NOP:
      return pretty_print::build_list("nop");
    case SET_REG_FALSE:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      "'#f");
    case SET_REG_TRUE:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      "'#t");
    case SET_REG_REG:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      source->to_form(file));
    case SET_BINTEGER:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      "binteger");
    case SET_PAIR:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      "pair");
    case DSLLV:
      return pretty_print::build_list(
          pretty_print::to_symbol("set!"), destination->to_form(file),
          pretty_print::build_list(pretty_print::to_symbol("shl"), source->to_form(file),
                                   source2->to_form(file)));
    case NEGATE:
      return pretty_print::build_list(pretty_print::to_symbol("set!"), destination->to_form(file),
                                      pretty_print::build_list("-", source->to_form(file)));
    case UNKNOWN:
      return pretty_print::build_list("unknown-branch-delay");
    default:
      assert(false);
  }
}

void BranchDelay::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  if (destination) {
    output->push_back(destination);
  }

  if (source) {
    output->push_back(source);
  }

  if (source2) {
    output->push_back(source2);
  }
}

void BranchDelay::get_read(std::vector<Register>* out) const {
  for (auto& src : {source, source2}) {
    auto as_reg = dynamic_cast<IR_Register*>(src.get());
    if (as_reg) {
      out->push_back(as_reg->reg);
    }
  }
}

void BranchDelay::get_write(std::vector<Register>* out) const {
  auto as_reg = dynamic_cast<IR_Register*>(destination.get());
  if (as_reg) {
    out->push_back(as_reg->reg);
  }
}

goos::Object IR_Nop::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::build_list("nop!");
}

void Condition::get_read(std::vector<Register>* out) const {
  for (auto& src : {src0, src1}) {
    auto as_reg = dynamic_cast<IR_Register*>(src.get());
    if (as_reg) {
      out->push_back(as_reg->reg);
    }
  }
}

int Condition::num_args() const {
  switch (kind) {
    case NOT_EQUAL:
    case EQUAL:
    case LESS_THAN_SIGNED:
    case LESS_THAN_UNSIGNED:
    case GREATER_THAN_SIGNED:
    case GREATER_THAN_UNSIGNED:
    case LEQ_SIGNED:
    case GEQ_SIGNED:
    case LEQ_UNSIGNED:
    case GEQ_UNSIGNED:
    case FLOAT_EQUAL:
    case FLOAT_NOT_EQUAL:
    case FLOAT_LESS_THAN:
    case FLOAT_GEQ:
    case FLOAT_GREATER_THAN:
    case FLOAT_LEQ:
      return 2;
    case ZERO:
    case NONZERO:
    case FALSE:
    case TRUTHY:
    case GREATER_THAN_ZERO_SIGNED:
    case GEQ_ZERO_SIGNED:
    case LESS_THAN_ZERO:
    case LEQ_ZERO_SIGNED:
      return 1;
    case ALWAYS:
    case NEVER:
      return 0;
    default:
      assert(false);
  }
}

void Condition::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  if (src0) {
    output->push_back(src0);
  }

  if (src1) {
    output->push_back(src1);
  }
}

void Condition::invert() {
  switch (kind) {
    case NOT_EQUAL:
      kind = EQUAL;
      break;
    case EQUAL:
      kind = NOT_EQUAL;
      break;
    case LESS_THAN_SIGNED:
      kind = GEQ_SIGNED;
      break;
    case GREATER_THAN_SIGNED:
      kind = LEQ_SIGNED;
      break;
    case LEQ_SIGNED:
      kind = GREATER_THAN_SIGNED;
      break;
    case GEQ_SIGNED:
      kind = LESS_THAN_SIGNED;
      break;
    case GREATER_THAN_ZERO_SIGNED:
      kind = LEQ_ZERO_SIGNED;
      break;
    case LEQ_ZERO_SIGNED:
      kind = GREATER_THAN_ZERO_SIGNED;
      break;
    case LESS_THAN_ZERO:
      kind = GEQ_ZERO_SIGNED;
      break;
    case GEQ_ZERO_SIGNED:
      kind = LESS_THAN_ZERO;
      break;
    case LESS_THAN_UNSIGNED:
      kind = GEQ_UNSIGNED;
      break;
    case GREATER_THAN_UNSIGNED:
      kind = LEQ_UNSIGNED;
      break;
    case LEQ_UNSIGNED:
      kind = GREATER_THAN_UNSIGNED;
      break;
    case GEQ_UNSIGNED:
      kind = LESS_THAN_UNSIGNED;
      break;
    case ZERO:
      kind = NONZERO;
      break;
    case NONZERO:
      kind = ZERO;
      break;
    case FALSE:
      kind = TRUTHY;
      break;
    case TRUTHY:
      kind = FALSE;
      break;
    case ALWAYS:
      kind = NEVER;
      break;
    case NEVER:
      kind = ALWAYS;
      break;
    case FLOAT_EQUAL:
      kind = FLOAT_NOT_EQUAL;
      break;
    case FLOAT_NOT_EQUAL:
      kind = FLOAT_EQUAL;
      break;
    case FLOAT_LESS_THAN:
      kind = FLOAT_GEQ;
      break;
    case FLOAT_GEQ:
      kind = FLOAT_LESS_THAN;
      break;
    case FLOAT_GREATER_THAN:
      kind = FLOAT_LEQ;
      break;
    case FLOAT_LEQ:
      kind = FLOAT_GREATER_THAN;
      break;
    default:
      assert(false);
  }
}

goos::Object Condition::to_form(const LinkedObjectFile& file) const {
  int nargs = num_args();
  std::string condtion_operator;
  switch (kind) {
    case NOT_EQUAL:
      condtion_operator = "!=";
      break;
    case EQUAL:
      condtion_operator = "=";
      break;
    case LESS_THAN_SIGNED:
      condtion_operator = "<.si";
      break;
    case LESS_THAN_UNSIGNED:
      condtion_operator = "<.ui";
      break;
    case GREATER_THAN_SIGNED:
      condtion_operator = ">.si";
      break;
    case GREATER_THAN_UNSIGNED:
      condtion_operator = ">.ui";
      break;
    case LEQ_SIGNED:
      condtion_operator = "<=.si";
      break;
    case GEQ_SIGNED:
      condtion_operator = ">=.si";
      break;
    case LEQ_UNSIGNED:
      condtion_operator = "<=.ui";
      break;
    case GEQ_UNSIGNED:
      condtion_operator = ">=.ui";
      break;
    case ZERO:
      condtion_operator = "zero?";
      break;
    case NONZERO:
      condtion_operator = "nonzero?";
      break;
    case FALSE:
      condtion_operator = "not";
      break;
    case TRUTHY:
      condtion_operator = "";
      break;
    case ALWAYS:
      condtion_operator = "'#t";
      break;
    case NEVER:
      condtion_operator = "'#f";
      break;
    case FLOAT_EQUAL:
      condtion_operator = "=.f";
      break;
    case FLOAT_NOT_EQUAL:
      condtion_operator = "!=.f";
      break;
    case FLOAT_LESS_THAN:
      condtion_operator = "<.f";
      break;
    case FLOAT_GEQ:
      condtion_operator = ">=.f";
      break;
    case FLOAT_GREATER_THAN:
      condtion_operator = ">.f";
      break;
    case FLOAT_LEQ:
      condtion_operator = "<=.f";
      break;
    case GREATER_THAN_ZERO_SIGNED:
      condtion_operator = ">0.si";
      break;
    case GEQ_ZERO_SIGNED:
      condtion_operator = ">=0.si";
      break;
    case LESS_THAN_ZERO:
      condtion_operator = "<0.si";
      break;
    case LEQ_ZERO_SIGNED:
      condtion_operator = "<=0.si";
      break;
    default:
      assert(false);
  }

  if (nargs == 2) {
    return pretty_print::build_list(pretty_print::to_symbol(condtion_operator), src0->to_form(file),
                                    src1->to_form(file));
  } else if (nargs == 1) {
    if (condtion_operator.empty()) {
      return src0->to_form(file);
    } else {
      return pretty_print::build_list(pretty_print::to_symbol(condtion_operator),
                                      src0->to_form(file));
    }
  } else if (nargs == 0) {
    return pretty_print::to_symbol(condtion_operator);
  } else {
    assert(false);
  }
}

goos::Object IR_Branch::to_form(const LinkedObjectFile& file) const {
  return pretty_print::build_list(
      pretty_print::to_symbol(likely ? "bl!" : "b!"), condition.to_form(file),
      pretty_print::to_symbol(file.get_label_name(dest_label_idx)), branch_delay.to_form(file));
}

void IR_Branch::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  condition.get_children(output);
  branch_delay.get_children(output);
}

std::vector<Register> IR_Branch::get_read(LinkedObjectFile& file) const {
  (void)file;
  std::vector<Register> result;
  condition.get_read(&result);
  branch_delay.get_read(&result);
  return result;
}

std::vector<Register> IR_Branch::get_write(LinkedObjectFile& file) const {
  (void)file;
  std::vector<Register> result;
  branch_delay.get_write(&result);
  return result;
}

goos::Object IR_Compare::to_form(const LinkedObjectFile& file) const {
  return condition.to_form(file);
}

void IR_Compare::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  condition.get_children(output);
}

std::vector<Register> IR_Compare::get_read(LinkedObjectFile& file) const {
  (void)file;
  std::vector<Register> result;
  condition.get_read(&result);
  return result;
}

goos::Object IR_Suspend::to_form(const LinkedObjectFile& file) const {
  (void)file;
  return pretty_print::build_list("suspend!");
}

void IR_Nop::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

void IR_Suspend::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_Begin::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> list;
  list.push_back(pretty_print::to_symbol("begin"));
  for (auto& x : forms) {
    list.push_back(x->to_form(file));
  }
  return pretty_print::build_list(list);
}

void IR_Begin::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  for (auto& x : forms) {
    output->push_back(x);
  }
}

namespace {
void print_inlining_begin(std::vector<goos::Object>* output, IR* ir, const LinkedObjectFile& file) {
  auto as_begin = dynamic_cast<IR_Begin*>(ir);
  if (as_begin) {
    for (auto& x : as_begin->forms) {
      output->push_back(x->to_form(file));
    }
  } else {
    output->push_back(ir->to_form(file));
  }
}

bool is_single_expression(IR* in) {
  return !dynamic_cast<IR_Begin*>(in);
}
}  // namespace

goos::Object IR_WhileLoop::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> list;
  list.push_back(pretty_print::to_symbol("while"));
  list.push_back(condition->to_form(file));
  print_inlining_begin(&list, body.get(), file);
  return pretty_print::build_list(list);
}

void IR_WhileLoop::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(condition);
  output->push_back(body);
}

goos::Object IR_UntilLoop::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> list;
  list.push_back(pretty_print::to_symbol("until"));
  list.push_back(condition->to_form(file));
  print_inlining_begin(&list, body.get(), file);
  return pretty_print::build_list(list);
}

void IR_UntilLoop::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(condition);
  output->push_back(body);
}

goos::Object IR_CondWithElse::to_form(const LinkedObjectFile& file) const {
  // for now we only turn it into an if statement if both cases won't require a begin at the top
  // level. I think it is more common to write these as a two-case cond instead of an if with begin.
  if (entries.size() == 1 && is_single_expression(entries.front().body.get()) &&
      is_single_expression(else_ir.get())) {
    std::vector<goos::Object> list;
    list.push_back(pretty_print::to_symbol("if"));
    list.push_back(entries.front().condition->to_form(file));
    list.push_back(entries.front().body->to_form(file));
    list.push_back(else_ir->to_form(file));
    return pretty_print::build_list(list);
  } else {
    std::vector<goos::Object> list;
    list.push_back(pretty_print::to_symbol("cond"));
    for (auto& e : entries) {
      std::vector<goos::Object> entry;
      entry.push_back(e.condition->to_form(file));
      print_inlining_begin(&entry, e.body.get(), file);
      list.push_back(pretty_print::build_list(entry));
    }
    std::vector<goos::Object> else_form;
    else_form.push_back(pretty_print::to_symbol("else"));
    print_inlining_begin(&else_form, else_ir.get(), file);
    list.push_back(pretty_print::build_list(else_form));
    return pretty_print::build_list(list);
  }
}

void IR_CondWithElse::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  for (auto& e : entries) {
    output->push_back(e.condition);
    output->push_back(e.body);
  }
  output->push_back(else_ir);
}

goos::Object IR_GetRuntimeType::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> list = {pretty_print::to_symbol("type-of"), object->to_form(file)};
  return pretty_print::build_list(list);
}

void IR_GetRuntimeType::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(object);
}

goos::Object IR_Cond::to_form(const LinkedObjectFile& file) const {
  if (entries.size() == 1 && is_single_expression(entries.front().body.get())) {
    // print as an if statement if we can put the body in a single form.
    std::vector<goos::Object> list;
    list.push_back(pretty_print::to_symbol("if"));
    list.push_back(entries.front().condition->to_form(file));
    list.push_back(entries.front().body->to_form(file));
    return pretty_print::build_list(list);
  } else if (entries.size() == 1) {
    // turn into a when if the body requires multiple forms
    // todo check to see if the condition starts with a NOT and this can be simplified to an
    // unless.
    std::vector<goos::Object> list;
    list.push_back(pretty_print::to_symbol("when"));
    list.push_back(entries.front().condition->to_form(file));
    print_inlining_begin(&list, entries.front().body.get(), file);
    return pretty_print::build_list(list);
  } else {
    std::vector<goos::Object> list;
    list.push_back(pretty_print::to_symbol("cond"));
    for (auto& e : entries) {
      std::vector<goos::Object> entry;
      entry.push_back(e.condition->to_form(file));
      print_inlining_begin(&entry, e.body.get(), file);
      list.push_back(pretty_print::build_list(entry));
    }
    return pretty_print::build_list(list);
  }
}

void IR_Cond::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  for (auto& e : entries) {
    output->push_back(e.condition);
    output->push_back(e.body);
  }
}

goos::Object IR_ShortCircuit::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> forms;
  switch (kind) {
    case UNKNOWN:
      forms.push_back(pretty_print::to_symbol("unknown-sc"));
      break;
    case AND:
      forms.push_back(pretty_print::to_symbol("and"));
      break;
    case OR:
      forms.push_back(pretty_print::to_symbol("or"));
      break;
    default:
      assert(false);
  }
  for (auto& x : entries) {
    forms.push_back(x.condition->to_form(file));
  }
  return pretty_print::build_list(forms);
}

void IR_ShortCircuit::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  for (auto& x : entries) {
    output->push_back(x.condition);
    if (x.output) {
      output->push_back(x.output);
    }
  }
}

goos::Object IR_Ash::to_form(const LinkedObjectFile& file) const {
  return pretty_print::build_list(pretty_print::to_symbol(is_signed ? "ash.si" : "ash.ui"),
                                  value->to_form(file), shift_amount->to_form(file));
}

void IR_Ash::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(value);
  output->push_back(shift_amount);
}

goos::Object IR_AsmOp::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> forms;
  forms.push_back(pretty_print::to_symbol(name));
  for (auto& x : {dst, src0, src1, src2}) {
    if (x) {
      forms.push_back(x->to_form(file));
    }
  }
  return pretty_print::build_list(forms);
}

void IR_AsmOp::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  for (auto& x : {dst, src0, src1}) {
    if (x) {
      output->push_back(x);
    }
  }
}

goos::Object IR_CMoveF::to_form(const LinkedObjectFile& file) const {
  return pretty_print::build_list(
      pretty_print::to_symbol(on_zero ? "cmove-false-on-zero" : "cmove-false-on-nonzero"),
      src->to_form(file));
}

void IR_CMoveF::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(src);
}

goos::Object IR_AsmReg::to_form(const LinkedObjectFile& file) const {
  (void)file;
  switch (kind) {
    case VU_Q:
      return pretty_print::to_symbol("Q");
    case VU_ACC:
      return pretty_print::to_symbol("ACC");
    default:
      assert(false);
  }
}

void IR_AsmReg::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  (void)output;
}

goos::Object IR_Return::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> forms;
  forms.push_back(pretty_print::to_symbol("return"));
  forms.push_back(pretty_print::build_list(return_code->to_form(file)));
  forms.push_back(pretty_print::build_list(dead_code->to_form(file)));
  return pretty_print::build_list(forms);
}

void IR_Return::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(return_code);
  output->push_back(dead_code);
}

goos::Object IR_Break::to_form(const LinkedObjectFile& file) const {
  std::vector<goos::Object> forms;
  forms.push_back(pretty_print::to_symbol("break"));  // todo break destination...
  forms.push_back(pretty_print::build_list(return_code->to_form(file)));
  forms.push_back(pretty_print::build_list(dead_code->to_form(file)));
  return pretty_print::build_list(forms);
}

void IR_Break::get_children(std::vector<std::shared_ptr<IR>>* output) const {
  output->push_back(return_code);
  output->push_back(dead_code);
}