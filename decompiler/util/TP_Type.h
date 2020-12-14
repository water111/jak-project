#pragma once
#include <string>
#include <cassert>
#include "common/type_system/TypeSpec.h"
#include "common/common_types.h"
#include "decompiler/Disasm/Register.h"

// struct TP_Type {
//  enum Kind {
//    OBJECT_OF_TYPE,
//    TYPE_OBJECT,
//    FALSE,
//    NONE,
//    PRODUCT,
//    OBJ_PLUS_PRODUCT,
//    PARTIAL_METHOD_TABLE_ACCESS,  // type + method_number * 4
//    METHOD_NEW_OF_OBJECT,
//    STRING
//  } kind = NONE;
//  // in the case that we are type_object, just store the type name in a single arg ts.
//  TypeSpec ts;
//  int multiplier;
//  std::string str_data;
//
//  TP_Type() = default;
//  explicit TP_Type(const TypeSpec& _ts) {
//    kind = OBJECT_OF_TYPE;
//    ts = _ts;
//  }
//
//  TP_Type simplify() const;
//  std::string print() const;
//
//  bool is_object_of_type() const { return kind == TYPE_OBJECT || ts == TypeSpec("type"); }
//
//  TypeSpec as_typespec() const {
//    switch (kind) {
//      case OBJECT_OF_TYPE:
//        return ts;
//      case TYPE_OBJECT:
//        return TypeSpec("type");
//      case FALSE:
//        return TypeSpec("symbol");
//      case NONE:
//        return TypeSpec("none");
//      case PRODUCT:
//      case METHOD_NEW_OF_OBJECT:
//        return ts;
//      default:
//        assert(false);
//    }
//  }
//
//  static TP_Type make_partial_method_table_access(TypeSpec ts) {
//    TP_Type result;
//    result.kind = PARTIAL_METHOD_TABLE_ACCESS;
//    result.ts = std::move(ts);
//    return result;
//  }
//
//  static TP_Type make_type_object(const std::string& name) {
//    TP_Type result;
//    result.kind = TYPE_OBJECT;
//    result.ts = TypeSpec(name);
//    return result;
//  }
//
//  static TP_Type make_string_object(const std::string& str) {
//    TP_Type result;
//    result.kind = STRING;
//    result.ts = TypeSpec("string");
//    result.str_data = str;
//    return result;
//  }
//
//  static TP_Type make_none() {
//    TP_Type result;
//    result.kind = NONE;
//    return result;
//  }
//
//  bool operator==(const TP_Type& other) const;
//};

/*!
 * A TP_Type is a specialized typespec used in the type propagation algorithm.
 * It is basically a normal typespec plus some optional information.
 * It should always use register types.
 */
class TP_Type {
 public:
  enum class Kind {
    TYPESPEC,                           // just a normal typespec
    TYPE_OF_TYPE_OR_CHILD,              // a type object, of the given type of a child type.
    FALSE_AS_NULL,                      // the GOAL "false" object, possibly used as a null.
    UNINITIALIZED,                      // representing data which is uninitialized.
    PRODUCT_WITH_CONSTANT,              // representing: (val * multiplier)
    OBJECT_PLUS_PRODUCT_WITH_CONSTANT,  // address: obj + (val * multiplier)
    OBJECT_NEW_METHOD,      // the method new of object, as used in an (object-new) or similar.
    STRING_CONSTANT,        // a string that's part of the string pool
    INTEGER_CONSTANT,       // a constant integer.
    DYNAMIC_METHOD_ACCESS,  // partial access into a
    INVALID
  } kind = Kind::UNINITIALIZED;
  TP_Type() = default;
  std::string print() const;
  bool operator==(const TP_Type& other) const;
  bool operator!=(const TP_Type& other) const;
  TypeSpec typespec() const;

  bool is_constant_string() const { return kind == Kind::STRING_CONSTANT; }
  bool is_integer_constant() const { return kind == Kind::INTEGER_CONSTANT; }
  bool is_integer_constant(int64_t value) const { return is_integer_constant() && m_int == value; }

  bool is_product_with(int64_t value) const {
    return kind == Kind::PRODUCT_WITH_CONSTANT && m_int == value;
  }

  const std::string& get_string() const {
    assert(is_constant_string());
    return m_str;
  }

  static TP_Type make_from_typespec(const TypeSpec& ts) {
    TP_Type result;
    result.kind = Kind::TYPESPEC;
    result.m_ts = ts;
    return result;
  }

  static TP_Type make_from_string(const std::string& str) {
    TP_Type result;
    result.kind = Kind::STRING_CONSTANT;
    result.m_str = str;
    return result;
  }

  static TP_Type make_type_object(const TypeSpec& type) {
    TP_Type result;
    result.kind = Kind::TYPE_OF_TYPE_OR_CHILD;
    result.m_ts = type;
    return result;
  }

  static TP_Type make_false() {
    TP_Type result;
    result.kind = Kind::FALSE_AS_NULL;
    return result;
  }

  static TP_Type make_uninitialized() {
    TP_Type result;
    result.kind = Kind::UNINITIALIZED;
    return result;
  }

  static TP_Type make_from_integer(int64_t value) {
    TP_Type result;
    result.kind = Kind::INTEGER_CONSTANT;
    result.m_int = value;
    return result;
  }

  static TP_Type make_from_product(int64_t multiplier) {
    TP_Type result;
    result.kind = Kind::PRODUCT_WITH_CONSTANT;
    result.m_int = multiplier;
    return result;
  }

  static TP_Type make_partial_dyanmic_vtable_access() {
    TP_Type result;
    result.kind = Kind::DYNAMIC_METHOD_ACCESS;
    return result;
  }

  static TP_Type make_object_new(const TypeSpec& ts) {
    TP_Type result;
    result.kind = Kind::OBJECT_NEW_METHOD;
    result.m_ts = ts;
    return result;
  }

  const TypeSpec& get_objects_typespec() const {
    assert(kind == Kind::TYPESPEC);
    return m_ts;
  }

  const TypeSpec& get_type_objects_typespec() const {
    assert(kind == Kind::TYPE_OF_TYPE_OR_CHILD);
    return m_ts;
  }

  const TypeSpec& get_method_new_object_typespec() const {
    assert(kind == Kind::OBJECT_NEW_METHOD);
    return m_ts;
  }

  uint64_t get_multiplier() const {
    assert(kind == Kind::PRODUCT_WITH_CONSTANT || kind == Kind::OBJECT_PLUS_PRODUCT_WITH_CONSTANT);
    return m_int;
  }

 private:
  TypeSpec m_ts;
  std::string m_str;
  int64_t m_int = 0;
};

struct TypeState {
  TP_Type gpr_types[32];
  TP_Type fpr_types[32];

  std::string print_gpr_masked(u32 mask) const;
  TP_Type& get(const Register& r) {
    switch (r.get_kind()) {
      case Reg::GPR:
        return gpr_types[r.get_gpr()];
      case Reg::FPR:
        return fpr_types[r.get_fpr()];
      default:
        assert(false);
    }
  }

  const TP_Type& get(const Register& r) const {
    switch (r.get_kind()) {
      case Reg::GPR:
        return gpr_types[r.get_gpr()];
      case Reg::FPR:
        return fpr_types[r.get_fpr()];
      default:
        assert(false);
    }
  }
};