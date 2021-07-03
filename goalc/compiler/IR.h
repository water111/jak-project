#pragma once

#include <string>
#include "CodeGenerator.h"
#include "goalc/regalloc/allocate.h"
#include "Val.h"
#include "goalc/emitter/ObjectGenerator.h"
#include "goalc/emitter/Register.h"

class IR {
 public:
  virtual std::string print() = 0;
  virtual RegAllocInstr to_rai() = 0;
  virtual void do_codegen(emitter::ObjectGenerator* gen,
                          const AllocationResult& allocs,
                          emitter::IR_Record irec) = 0;
  virtual void add_constraints(std::vector<IRegConstraint>* constraints, int my_id) {
    (void)constraints;
    (void)my_id;
  }
  virtual ~IR() = default;
};

class IR_Return : public IR {
 public:
  IR_Return(const RegVal* return_reg, const RegVal* value, emitter::Register ret_reg);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void add_constraints(std::vector<IRegConstraint>* constraints, int my_id) override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
  const RegVal* value() { return m_value; }

 protected:
  const RegVal* m_return_reg = nullptr;
  const RegVal* m_value = nullptr;
  emitter::Register m_ret_reg;
};

class IR_LoadConstant64 : public IR {
 public:
  IR_LoadConstant64(const RegVal* dest, u64 value);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  u64 m_value = 0;
};

class IR_LoadSymbolPointer : public IR {
 public:
  IR_LoadSymbolPointer(const RegVal* dest, std::string name);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  std::string m_name;
};

class IR_SetSymbolValue : public IR {
 public:
  IR_SetSymbolValue(const SymbolVal* dest, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const SymbolVal* m_dest = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_GetSymbolValue : public IR {
 public:
  IR_GetSymbolValue(const RegVal* dest, const SymbolVal* src, bool sext);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  const SymbolVal* m_src = nullptr;
  bool m_sext = false;
};

class IR_RegSet : public IR {
 public:
  IR_RegSet(const RegVal* dest, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_FunctionCall : public IR {
 public:
  IR_FunctionCall(const RegVal* func,
                  const RegVal* ret,
                  std::vector<RegVal*> args,
                  std::vector<emitter::Register> arg_regs,
                  std::optional<emitter::Register> ret_reg);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
  void add_constraints(std::vector<IRegConstraint>* constraints, int my_id) override;

 protected:
  const RegVal* m_func = nullptr;
  const RegVal* m_ret = nullptr;
  std::vector<RegVal*> m_args;
  std::vector<emitter::Register> m_arg_regs;
  std::optional<emitter::Register> m_ret_reg;
};

class IR_RegValAddr : public IR {
 public:
  IR_RegValAddr(const RegVal* dest, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_StaticVarAddr : public IR {
 public:
  IR_StaticVarAddr(const RegVal* dest, const StaticObject* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  const StaticObject* m_src = nullptr;
};

class IR_StaticVarLoad : public IR {
 public:
  IR_StaticVarLoad(const RegVal* dest, const StaticObject* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  const StaticObject* m_src = nullptr;
};

class IR_FunctionAddr : public IR {
 public:
  IR_FunctionAddr(const RegVal* dest, FunctionEnv* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  FunctionEnv* m_src = nullptr;
};

enum class IntegerMathKind {
  ADD_64,
  SUB_64,
  IMUL_32,
  IMUL_64,
  IDIV_32,
  UDIV_32,
  SHLV_64,
  SARV_64,
  SHRV_64,
  SHL_64,
  SAR_64,
  SHR_64,
  IMOD_32,
  OR_64,
  AND_64,
  XOR_64,
  NOT_64
};

class IR_IntegerMath : public IR {
 public:
  IR_IntegerMath(IntegerMathKind kind, RegVal* dest, RegVal* arg);
  IR_IntegerMath(IntegerMathKind kind, RegVal* dest, u8 shift_amount);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
  IntegerMathKind get_kind() const { return m_kind; }

 protected:
  IntegerMathKind m_kind;
  RegVal* m_dest;
  RegVal* m_arg = nullptr;
  u8 m_shift_amount = 0;
};

enum class FloatMathKind { DIV_SS, MUL_SS, ADD_SS, SUB_SS, MIN_SS, MAX_SS, SQRT_SS };

class IR_FloatMath : public IR {
 public:
  IR_FloatMath(FloatMathKind kind, RegVal* dest, RegVal* arg);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
  FloatMathKind get_kind() const { return m_kind; }

 protected:
  FloatMathKind m_kind;
  RegVal* m_dest;
  RegVal* m_arg;
};

enum class ConditionKind { NOT_EQUAL, EQUAL, LEQ, LT, GT, GEQ, INVALID_CONDITION };

struct Condition {
  ConditionKind kind = ConditionKind::INVALID_CONDITION;
  RegVal* a = nullptr;
  RegVal* b = nullptr;
  bool is_signed = false;
  bool is_float = false;
  RegAllocInstr to_rai();
  std::string print() const;
};

class IR_GotoLabel : public IR {
 public:
  IR_GotoLabel();
  void resolve(const Label* dest);
  explicit IR_GotoLabel(const Label* dest);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const Label* m_dest = nullptr;
  bool m_resolved = false;
};

class IR_ConditionalBranch : public IR {
 public:
  IR_ConditionalBranch(const Condition& condition, Label _label);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
  void mark_as_resolved() { m_resolved = true; }

  Condition condition;
  Label label;

 private:
  bool m_resolved = false;
};

class IR_Null : public IR {
 public:
  IR_Null() = default;
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
};

class IR_ValueReset : public IR {
 public:
  IR_ValueReset(std::vector<RegVal*> args);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  std::vector<RegVal*> m_args;
};

class IR_FloatToInt : public IR {
 public:
  IR_FloatToInt(const RegVal* dest, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dest = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_IntToFloat : public IR {
 public:
  IR_IntToFloat(const RegVal* dest, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dest = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_GetStackAddr : public IR {
 public:
  IR_GetStackAddr(const RegVal* dest, int slot);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dest = nullptr;
  int m_slot = -1;
};

class IR_Nop : public IR {
 public:
  IR_Nop();
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
};

class IR_Asm : public IR {
 public:
  explicit IR_Asm(bool use_coloring);
  std::string get_color_suffix_string();

 protected:
  bool m_use_coloring;
};

class IR_LoadConstOffset : public IR_Asm {
 public:
  IR_LoadConstOffset(const RegVal* dest,
                     int offset,
                     const RegVal* base,
                     MemLoadInfo info,
                     bool use_coloring = true);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dest = nullptr;
  int m_offset = 0;
  const RegVal* m_base = nullptr;
  MemLoadInfo m_info;
};

class IR_StoreConstOffset : public IR_Asm {
 public:
  IR_StoreConstOffset(const RegVal* value,
                      int offset,
                      const RegVal* base,
                      int size,
                      bool use_coloring = true);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_value = nullptr;
  int m_offset = 0;
  const RegVal* m_base = nullptr;
  int m_size = 0;
};

class IR_AsmRet : public IR_Asm {
 public:
  IR_AsmRet(bool use_coloring);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
};

class IR_AsmPush : public IR_Asm {
 public:
  IR_AsmPush(bool use_coloring, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_src = nullptr;
};

class IR_AsmPop : public IR_Asm {
 public:
  IR_AsmPop(bool use_coloring, const RegVal* dst);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dst = nullptr;
};

class IR_AsmSub : public IR_Asm {
 public:
  IR_AsmSub(bool use_coloring, const RegVal* dst, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_AsmAdd : public IR_Asm {
 public:
  IR_AsmAdd(bool use_coloring, const RegVal* dst, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 private:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_AsmFNop : public IR_Asm {
 public:
  IR_AsmFNop();
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
};

class IR_AsmFWait : public IR_Asm {
 public:
  IR_AsmFWait();
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;
};

class IR_GetSymbolValueAsm : public IR_Asm {
 public:
  IR_GetSymbolValueAsm(bool use_coloring, const RegVal* dest, std::string sym_name, bool sext);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dest = nullptr;
  std::string m_sym_name;
  bool m_sext = false;
};

class IR_JumpReg : public IR_Asm {
 public:
  IR_JumpReg(bool use_coloring, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_src = nullptr;
};

class IR_RegSetAsm : public IR_Asm {
 public:
  IR_RegSetAsm(bool use_color, const RegVal* dst, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
};

class IR_VFMath3Asm : public IR_Asm {
 public:
  enum class Kind { XOR, SUB, ADD, MUL, MAX, MIN, DIV };
  IR_VFMath3Asm(bool use_color,
                const RegVal* dst,
                const RegVal* src1,
                const RegVal* src2,
                Kind kind);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src1 = nullptr;
  const RegVal* m_src2 = nullptr;
  Kind m_kind;
};

class IR_Int128Math3Asm : public IR_Asm {
 public:
  // these are MIPS names, not x86 names.
  enum class Kind {
    PEXTUB,
    PEXTUH,
    PEXTUW,
    PEXTLB,
    PEXTLH,
    PEXTLW,
    PCPYUD,
    PCPYLD,
    PSUBW,
    PCEQB,
    PCEQH,
    PCEQW,
    PCGTB,
    PCGTH,
    PCGTW,
    POR,
    PXOR,
    PAND
  };
  IR_Int128Math3Asm(bool use_color,
                    const RegVal* dst,
                    const RegVal* src1,
                    const RegVal* src2,
                    Kind kind);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src1 = nullptr;
  const RegVal* m_src2 = nullptr;
  Kind m_kind;
};

class IR_Int128Math2Asm : public IR_Asm {
 public:
  enum class Kind { PW_SLL, PW_SRL, PW_SRA, VPSRLDQ, VPSLLDQ, VPSHUFLW, VPSHUFHW };
  IR_Int128Math2Asm(bool use_color,
                    const RegVal* dst,
                    const RegVal* src,
                    Kind kind,
                    std::optional<int64_t> = std::nullopt);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
  Kind m_kind;
  std::optional<int64_t> m_imm;
};

class IR_VFMath2Asm : public IR_Asm {
 public:
  enum class Kind { ITOF, FTOI };
  IR_VFMath2Asm(bool use_color, const RegVal* dst, const RegVal* src, Kind kind);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
  Kind m_kind;
};

class IR_BlendVF : public IR_Asm {
 public:
  IR_BlendVF(bool use_color, const RegVal* dst, const RegVal* src1, const RegVal* src2, u8 mask);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src1 = nullptr;
  const RegVal* m_src2 = nullptr;
  u8 m_mask = 0xff;
};

class IR_SplatVF : public IR_Asm {
 public:
  IR_SplatVF(bool use_color,
             const RegVal* dst,
             const RegVal* src,
             const emitter::Register::VF_ELEMENT element);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
  const emitter::Register::VF_ELEMENT m_element = emitter::Register::VF_ELEMENT::NONE;
};

class IR_SwizzleVF : public IR_Asm {
 public:
  IR_SwizzleVF(bool use_color, const RegVal* dst, const RegVal* src, const u8 m_controlBytes);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
  const u8 m_controlBytes = 0;
};

class IR_SqrtVF : public IR_Asm {
 public:
  IR_SqrtVF(bool use_color, const RegVal* dst, const RegVal* src);
  std::string print() override;
  RegAllocInstr to_rai() override;
  void do_codegen(emitter::ObjectGenerator* gen,
                  const AllocationResult& allocs,
                  emitter::IR_Record irec) override;

 protected:
  const RegVal* m_dst = nullptr;
  const RegVal* m_src = nullptr;
};
