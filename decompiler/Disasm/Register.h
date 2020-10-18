#pragma once

/*!
 * @file Register.h
 * Representation of an EE register.
 */

#ifndef NEXT_REGISTER_H
#define NEXT_REGISTER_H

#include <cstdint>
#include <string>

// Namespace for register name constants
namespace Reg {
enum RegisterKind {
  GPR = 0,  // EE General purpose registers, these have nicknames.
  FPR = 1,  // EE Floating point registers, just called f0 - f31
  VF = 2,   // VU0 Floating point vector registers from EE, just called vf0 - vf31
  VI =
      3,  // VU0 Integer registers from EE, the first 16 are vi00 - vi15, the rest are control regs.
  COP0 = 4,  // EE COP0 Control Registers: full of fancy names (there are 32 of them)
  PCR = 5,   // Performance Counter registers (PCR0, PCR1)
  MAX_KIND = 6
};

// nicknames for GPRs
enum Gpr {
  R0 = 0,   // hardcoded to zero
  AT = 1,   // temp, not used by GOAL compiler, but used by GOAL's kernel inline assembly (an other
            // places?)
  V0 = 2,   // return, temp
  V1 = 3,   // temp
  A0 = 4,   // arg0, temp
  A1 = 5,   // arg1, temp
  A2 = 6,   // arg2, temp
  A3 = 7,   // arg3, temp
  T0 = 8,   // arg4, temp
  T1 = 9,   // arg5, temp
  T2 = 10,  // arg6, temp
  T3 = 11,  // arg7, temp
  T4 = 12,  // temp
  T5 = 13,  // temp
  T6 = 14,  // temp
  T7 = 15,  // temp
  S0 = 16,  // saved
  S1 = 17,  // saved
  S2 = 18,  // saved
  S3 = 19,  // saved
  S4 = 20,  // saved
  S5 = 21,  // saved
  S6 = 22,  // process pointer
  S7 = 23,  // symbol table
  T8 = 24,  // temp
  T9 = 25,  // function pointer
  K0 = 26,  // reserved
  K1 = 27,  // reserved
  GP = 28,  // saved (C code uses this a global pointer)
  SP = 29,  // stack pointer
  FP = 30,  // global pointer (address of current function)
  RA = 31,  // return address
  MAX_GPR = 32
};

// nicknames for COP0 registers
enum Cop0 {
  INDEX = 0,
  RANDOM = 1,
  ENTRYLO0 = 2,
  ENTRYLO1 = 3,
  CONTEXT = 4,
  PAGEMASK = 5,
  WIRED = 6,
  INVALID7 = 7,
  BADVADDR = 8,
  COUNT = 9,
  ENTRYHI = 10,
  COMPARE = 11,
  COP0_STATUS = 12,
  CAUSE = 13,
  EPC = 14,
  PRID = 15,
  CONFIG = 16,
  INVALID17 = 17,
  INVALID18 = 18,
  INVALID19 = 19,
  INVALID20 = 20,
  INVALID21 = 21,
  INVALID22 = 22,
  BADPADDR = 23,
  DEBUG = 24,
  PERF = 25,
  INVALID26 = 26,
  INVALID27 = 27,
  TAGLO = 28,
  TAGHI = 29,
  ERROREPC = 30,
  INVALID31 = 31,
  MAX_COP0 = 32
};

// nicknames for COP2 Integer (VI) registers
// the first 16 are vi0 - vi15, so they don't have nicknames
enum Vi {
  COP2_STATUS = 16,
  MAC = 17,
  CLIPPING = 18,
  COP2_INVALID3 = 19,
  R = 20,
  I = 21,
  Q = 22,
  COP2_INVALID7 = 23,
  COP2_INVALID8 = 24,
  COP2_INVALID9 = 25,
  TPC = 26,
  CMSAR0 = 27,
  FBRST = 28,
  VPUSTAT = 29,
  COP2_INVALID14 = 30,
  CMSAR1 = 31,
  MAX_COP2 = 32
};
}  // namespace Reg

// Representation of a register.  Uses a 32-bit integer internally.
class Register {
 public:
  Register() = default;
  Register(Reg::RegisterKind kind, uint32_t num);
  const char* to_charp() const;
  std::string to_string() const;
  Reg::RegisterKind get_kind() const;
  Reg::Gpr get_gpr() const;
  uint32_t get_fpr() const;
  uint32_t get_vf() const;
  uint32_t get_vi() const;
  Reg::Cop0 get_cop0() const;
  uint32_t get_pcr() const;

  bool operator==(const Register& other) const;
  bool operator!=(const Register& other) const;
  bool operator<(const Register& other) const;

  struct hash {
    auto operator()(const Register& x) const { return std::hash<uint16_t>()(x.id); }
  };

 private:
  uint16_t id = -1;
};

#endif  // NEXT_REGISTER_H
