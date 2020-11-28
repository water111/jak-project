#include "IR.h"
#include "decompiler/util/DecompilerTypeSystem.h"
#include "third-party/fmt/core.h"
#include "common/goos/Object.h"

namespace {
bool is_plain_type(const TP_Type& type, const TypeSpec& ts) {
  return type.kind == TP_Type::OBJECT_OF_TYPE && type.ts == ts;
}

bool is_integer_type(const TP_Type& type) {
  return is_plain_type(type, TypeSpec("int")) || is_plain_type(type, TypeSpec("uint"));
}

/*!
 * If first arg is unsigned, make the result unsigned.
 * Otherwise signed. This is the default GOAL behavior I guess.
 */
TP_Type get_int_type(const TP_Type& one) {
  if (is_plain_type(one, TypeSpec("uint"))) {
    return one;
  } else {
    return TP_Type(TypeSpec("int"));
  }
}

struct RegOffset {
  Register reg;
  int offset;
};

bool get_as_reg_offset(const IR* ir, RegOffset* out) {
  auto as_reg = dynamic_cast<const IR_Register*>(ir);
  if (as_reg) {
    out->reg = as_reg->reg;
    out->offset = 0;
    return true;
  }

  auto as_math = dynamic_cast<const IR_IntMath2*>(ir);
  if (as_math && as_math->kind == IR_IntMath2::ADD) {
    auto first_as_reg = dynamic_cast<const IR_Register*>(as_math->arg0.get());
    auto second_as_const = dynamic_cast<const IR_IntegerConstant*>(as_math->arg1.get());
    if (first_as_reg && second_as_const) {
      out->reg = first_as_reg->reg;
      out->offset = second_as_const->value;
      return true;
    }
  }
  return false;
}

RegKind get_reg_kind(const Register& r) {
  switch (r.get_kind()) {
    case Reg::GPR:
      return RegKind::GPR_64;
    case Reg::FPR:
      return RegKind::FLOAT;
    default:
      assert(false);
  }
}
}  // namespace

void IR_Atomic::propagate_types(const TypeState& input,
                                const LinkedObjectFile& file,
                                DecompilerTypeSystem& dts) {
  (void)input;
  (void)dts;
  throw std::runtime_error(
      fmt::format("Could not propagate types for {}, not yet implemented", print(file)));
}

TP_Type IR::get_expression_type(const TypeState& input,
                                const LinkedObjectFile& file,
                                DecompilerTypeSystem& dts) {
  (void)input;
  (void)dts;
  throw std::runtime_error(
      fmt::format("Could not get expression types for {}, not yet implemented", print(file)));
}

void IR_Set_Atomic::propagate_types(const TypeState& input,
                                    const LinkedObjectFile& file,
                                    DecompilerTypeSystem& dts) {
  // pass through types
  end_types = input;
  // modify as needed
  switch (kind) {
    case IR_Set::REG_64:
    case IR_Set::LOAD:
    case IR_Set::GPR_TO_FPR:
    case IR_Set::FPR_TO_GPR64:
    case IR_Set::REG_FLT:
    case IR_Set::SYM_LOAD: {
      auto as_reg = dynamic_cast<IR_Register*>(dst.get());
      assert(as_reg);
      auto t = src->get_expression_type(input, file, dts);
      end_types.get(as_reg->reg) = t;
    } break;
    default:
      throw std::runtime_error(fmt::format(
          "Could not propagate types through IR_Set_Atomic, kind not handled {}", print(file)));
  }
}

TP_Type IR_Register::get_expression_type(const TypeState& input,
                                         const LinkedObjectFile& file,
                                         DecompilerTypeSystem& dts) {
  (void)file;
  (void)dts;
  return input.get(reg);
}

TP_Type IR_Load::get_expression_type(const TypeState& input,
                                     const LinkedObjectFile& file,
                                     DecompilerTypeSystem& dts) {
  (void)input;
  auto as_static = dynamic_cast<IR_StaticAddress*>(location.get());
  if (as_static && kind == FLOAT) {
    // loading static data with a FLOAT kind load (lwc1), assume result is a float.
    return TP_Type(dts.ts.make_typespec("float"));
  }

  RegOffset ro;
  if (get_as_reg_offset(location.get(), &ro)) {
    // nice
    ReverseDerefInputInfo rd_in;
    rd_in.mem_deref = true;
    rd_in.input_type = input.get(ro.reg).as_typespec();
    rd_in.reg = get_reg_kind(ro.reg);  // bleh
    rd_in.offset = ro.offset;
    rd_in.sign_extend = kind == SIGNED;
    rd_in.load_size = size;

    auto rd = dts.ts.get_reverse_deref_info(rd_in);
    if (!rd.success) {
      throw std::runtime_error(
          fmt::format("Could not get type of load: {}. Reverse Deref Failed.", print(file)));
    }
    return TP_Type(rd.result_type);
  }

  throw std::runtime_error(
      fmt::format("Could not get type of load: {}. Not handled.", print(file)));
}

TP_Type IR_FloatMath2::get_expression_type(const TypeState& input,
                                           const LinkedObjectFile& file,
                                           DecompilerTypeSystem& dts) {
  (void)input;
  (void)file;

  // regardless of input types, the output is going to be a float.
  switch (kind) {
    case DIV:
    case MUL:
    case ADD:
    case SUB:
    case MIN:
    case MAX:
      return TP_Type(dts.ts.make_typespec("float"));
    default:
      assert(false);
  }
}

TP_Type IR_IntMath2::get_expression_type(const TypeState& input,
                                         const LinkedObjectFile& file,
                                         DecompilerTypeSystem& dts) {
  auto arg0_type = arg0->get_expression_type(input, file, dts);
  auto arg1_type = arg1->get_expression_type(input, file, dts);

  if (is_integer_type(arg0_type) && is_integer_type(arg1_type)) {
    // case where both arguments are integers.
    // in this case we assume we're actually doing math.
    switch (kind) {
      case ADD:
      case SUB:
      case AND:
      case OR:
      case NOR:
      case XOR:
        // we don't know if we're signed or unsigned. so let's just go with the first type.
        return get_int_type(arg0_type);
      case MUL_SIGNED:
      case DIV_SIGNED:
      case RIGHT_SHIFT_ARITH:
      case MOD_SIGNED:
      case MIN_SIGNED:
      case MAX_SIGNED:
        // result is going to be signed, regardless of inputs.
        return TP_Type(TypeSpec("int"));
      default:
        break;
    }
  }

  throw std::runtime_error(
      fmt::format("Can't get_expression_type on this IR_IntMath2: {}", print(file)));
}

void BranchDelay::type_prop(TypeState& output,
                            const LinkedObjectFile& file,
                            DecompilerTypeSystem& dts) {
  (void)dts;
  switch (kind) {
    case DSLLV: {
      // I think this is only used in ash, in which case the output should be an int/uint
      // welll
      auto dst = dynamic_cast<IR_Register*>(destination.get());
      assert(dst);
      auto src = dynamic_cast<IR_Register*>(source.get());
      assert(src);
      if (is_plain_type(output.get(src->reg), TypeSpec("uint"))) {
        // todo, this won't catch child uint types. I think this doesn't matter though.
        output.get(dst->reg) = TP_Type(TypeSpec("uint"));
      }
      output.get(dst->reg) = TP_Type(TypeSpec("int"));
    } break;
    case NEGATE: {
      auto dst = dynamic_cast<IR_Register*>(destination.get());
      assert(dst);
      output.get(dst->reg) = TP_Type(TypeSpec("int"));
    } break;
    case SET_REG_FALSE: {
      auto dst = dynamic_cast<IR_Register*>(destination.get());
      assert(dst);
      output.get(dst->reg).kind = TP_Type::FALSE;
    } break;
    case SET_REG_REG: {
      auto dst = dynamic_cast<IR_Register*>(destination.get());
      assert(dst);
      auto src = dynamic_cast<IR_Register*>(source.get());
      assert(src);
      output.get(dst->reg) = output.get(src->reg);
      break;
    }

    case NOP:
      break;

    default:
      throw std::runtime_error("Unhandled branch delay in type_prop: " + to_form(file).print());
  }
}

void IR_Branch_Atomic::propagate_types(const TypeState& input,
                                       const LinkedObjectFile& file,
                                       DecompilerTypeSystem& dts) {
  // pass through types
  end_types = input;
  branch_delay.type_prop(end_types, file, dts);
  // todo clobbers.
}

TP_Type IR_IntMath1::get_expression_type(const TypeState& input,
                                         const LinkedObjectFile& file,
                                         DecompilerTypeSystem& dts) {
  (void)input;
  (void)dts;
  auto arg_type = arg->get_expression_type(input, file, dts);
  switch (kind) {
    case NEG:
      // if we negate a thing, let's just make it a signed integer.
      return TP_Type(TypeSpec("int"));
    case NOT:
      return get_int_type(arg_type);
    default:
      throw std::runtime_error("IR_IntMath1::get_expression_type case not handled: " +
                               to_form(file).print());
  }
}

TP_Type IR_SymbolValue::get_expression_type(const TypeState& input,
                                            const LinkedObjectFile& file,
                                            DecompilerTypeSystem& dts) {
  (void)input;
  (void)file;
  if (name == "#f") {
    TP_Type result;
    result.kind = TP_Type::FALSE;
    return result;
  }

  auto type = dts.symbol_types.find(name);
  if (type == dts.symbol_types.end()) {
    throw std::runtime_error("Don't have the type of symbol " + name);
  }

  return TP_Type(type->second);
}

TP_Type IR_Symbol::get_expression_type(const TypeState& input,
                                       const LinkedObjectFile& file,
                                       DecompilerTypeSystem& dts) {
  (void)input;
  (void)file;
  (void)dts;
  if (name == "#f") {
    TP_Type result;
    result.kind = TP_Type::FALSE;
    return result;
  }

  return TP_Type(TypeSpec("symbol"));
}

TP_Type IR_IntegerConstant::get_expression_type(const TypeState& input,
                                                const LinkedObjectFile& file,
                                                DecompilerTypeSystem& dts) {
  (void)input;
  (void)file;
  (void)dts;
  return TP_Type(TypeSpec("int"));
}

TP_Type IR_Compare::get_expression_type(const TypeState& input,
                                        const LinkedObjectFile& file,
                                        DecompilerTypeSystem& dts) {
  (void)input;
  (void)file;
  (void)dts;
  return TP_Type(TypeSpec("symbol"));
}