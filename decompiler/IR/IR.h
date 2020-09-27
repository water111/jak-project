#ifndef JAK_IR_H
#define JAK_IR_H

#include "decompiler/Disasm/Register.h"
#include "decompiler/util/LispPrint.h"

class LinkedObjectFile;

class IR {
 public:
  virtual std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const = 0;
  std::string print(const LinkedObjectFile& file) const;

  bool is_basic_op = false;
};

class IR_Failed : public IR {
 public:
  IR_Failed() = default;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_Register : public IR {
 public:
  IR_Register(Register _reg, int _instr_idx) : reg(_reg), instr_idx(_instr_idx) {}
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
  Register reg;
  int instr_idx = -1;
};

class IR_Set : public IR {
 public:
  enum Kind { REG_64, LOAD, SYM_LOAD, FPR_TO_GPR64, GPR_TO_FPR, REG_FLT, REG_I128 } kind;
  IR_Set(Kind _kind, std::shared_ptr<IR> _dst, std::shared_ptr<IR> _src)
      : kind(_kind), dst(std::move(_dst)), src(std::move(_src)) {}
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
  std::shared_ptr<IR> dst, src;
};

class IR_Symbol : public IR {
 public:
  IR_Symbol(std::string _name) : name(std::move(_name)) {}
  std::string name;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_SymbolValue : public IR {
 public:
  IR_SymbolValue(std::string _name) : name(std::move(_name)) {}
  std::string name;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_StaticAddress : public IR {
 public:
  IR_StaticAddress(int _label_id) : label_id(_label_id) {}
  int label_id = -1;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_Load : public IR {
 public:
  enum Kind { UNSIGNED, SIGNED, FLOAT } kind;

  IR_Load(Kind _kind, int _size, const std::shared_ptr<IR>& _location)
      : kind(_kind), size(_size), location(_location) {}
  int size;
  std::shared_ptr<IR> location;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_FloatMath2 : public IR {
 public:
  enum Kind { DIV, MUL, ADD, SUB } kind;
  IR_FloatMath2(Kind _kind, std::shared_ptr<IR> _arg0, std::shared_ptr<IR> _arg1)
      : kind(_kind), arg0(std::move(_arg0)), arg1(std::move(_arg1)) {}
  std::shared_ptr<IR> arg0, arg1;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_IntMath2 : public IR {
 public:
  enum Kind { ADD, SUB, MUL_SIGNED, DIV_SIGNED, MOD_SIGNED, OR, AND, NOR, XOR, LEFT_SHIFT } kind;
  IR_IntMath2(Kind _kind, std::shared_ptr<IR> _arg0, std::shared_ptr<IR> _arg1)
      : kind(_kind), arg0(std::move(_arg0)), arg1(std::move(_arg1)) {}
  std::shared_ptr<IR> arg0, arg1;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_IntMath1 : public IR {
 public:
  enum Kind { NOT } kind;
  IR_IntMath1(Kind _kind, std::shared_ptr<IR> _arg) : kind(_kind), arg(std::move(_arg)) {}
  std::shared_ptr<IR> arg;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_Call : public IR {
 public:
  IR_Call() = default;
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

class IR_IntegerConstant : public IR {
 public:
  int64_t value;
  explicit IR_IntegerConstant(int64_t _value) : value(_value) {}
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

struct BranchDelay {
  enum Kind { NOP, SET_REG_FALSE, UNKNOWN } kind;
  std::shared_ptr<IR> destination = nullptr;
  BranchDelay(Kind _kind)
      : kind(_kind) {}
  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const;
};

class IR_Branch2 : public IR {
 public:
  enum Kind { NOT_EQUAL, EQUAL } kind;
  IR_Branch2(Kind _kind,
             int _dest_label_idx,
             std::shared_ptr<IR> _src0,
             std::shared_ptr<IR> _src1,
             BranchDelay _branch_delay)
      : kind(_kind),
        dest_label_idx(_dest_label_idx),
        src0(std::move(_src0)),
        src1(std::move(_src1)),
        branch_delay(std::move(_branch_delay)) {}

  int dest_label_idx;
  std::shared_ptr<IR> src0, src1;
  BranchDelay branch_delay;

  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

//class IR_Branch1 : public IR {
// public:
//  enum Kind { NOT_EQUAL, EQUAL } kind;
//  IR_Branch2(Kind _kind,
//             int _dest_label_idx,
//             std::shared_ptr<IR> _src0,
//             std::shared_ptr<IR> _src1,
//             BranchDelay _branch_delay)
//      : kind(_kind),
//        dest_label_idx(_dest_label_idx),
//        src0(std::move(_src0)),
//        src1(std::move(_src1)),
//        branch_delay(std::move(_branch_delay)) {}
//
//  int dest_label_idx;
//  std::shared_ptr<IR> src0, src1;
//  BranchDelay branch_delay;
//
//  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
//};

class IR_BranchAlways : public IR {
 public:
  IR_BranchAlways(int _dest_label_idx,
             BranchDelay _branch_delay)
      :
        dest_label_idx(_dest_label_idx),
        branch_delay(std::move(_branch_delay)) {}
  int dest_label_idx;
  BranchDelay branch_delay;

  std::shared_ptr<Form> to_form(const LinkedObjectFile& file) const override;
};

#endif  // JAK_IR_H
