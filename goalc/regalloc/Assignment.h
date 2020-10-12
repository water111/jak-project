#pragma once

#ifndef JAK_ASSIGNMENT_H
#define JAK_ASSIGNMENT_H

#include "goalc/emitter/Register.h"

/*!
 * The assignment of an IRegister to a real Register.
 * For a single IR Instruction.
 */
struct Assignment {
  enum class Kind { STACK, REGISTER, UNASSIGNED } kind = Kind::UNASSIGNED;
  emitter::Register reg = -1;  //! where the IRegister is now
  int stack_slot = -1;         //! index of the slot, if we are ever spilled
  bool spilled = false;        //! are we ever spilled

  std::string to_string() const;
  bool occupies_same_reg(const Assignment& other) const { return other.reg == reg && (reg != -1); }

  bool occupies_reg(emitter::Register other_reg) const { return reg == other_reg && (reg != -1); }

  bool is_assigned() const { return kind != Kind::UNASSIGNED; }
};

#endif  // JAK_ASSIGNMENT_H
