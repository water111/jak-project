#include "OpenGoalMapping.h"
#include "common/goos/PrettyPrinter.h"
#include <optional>

namespace decompiler {

typedef OpenGOALAsm::InstructionModifiers MOD;

const std::map<InstructionKind, OpenGOALAsm::Function> MIPS_ASM_TO_OPEN_GOAL_FUNCS = {
    // ----- EE -------
    {InstructionKind::PSLLW, {".pw.sll", {}}},
    {InstructionKind::PSRAW, {".pw.sra", {}}},
    {InstructionKind::PSUBW, {".psubw", {}}},

    // Boolean Arithmetic - or / not or / and
    {InstructionKind::POR, {".por", {}}},
    {InstructionKind::PNOR, {".pnor", {}}},
    {InstructionKind::PAND, {".pand", {}}},

    // Parallel Pack
    {InstructionKind::PPACH, {".ppach", {}}},

    // Parallel Compares
    {InstructionKind::PCEQB, {".pceqb", {}}},
    // {InstructionKind::PCEQH, {".pceqh", {}}},
    {InstructionKind::PCEQW, {".pceqw", {}}},
    // {InstructionKind::PCGTB, {".pcgtb", {}}},
    // {InstructionKind::PCGTH, {".pcgth", {}}},
    {InstructionKind::PCGTW, {".pcgtw", {}}},

    // Parallel Extends
    {InstructionKind::PEXTUB, {".pextub", {}}},
    {InstructionKind::PEXTUH, {".pextuh", {}}},
    {InstructionKind::PEXTUW, {".pextuw", {}}},
    {InstructionKind::PEXTLB, {".pextlb", {}}},
    {InstructionKind::PEXTLH, {".pextlh", {}}},
    {InstructionKind::PEXTLW, {".pextlw", {}}},
    {InstructionKind::PCPYLD, {".pcpyld", {}}},
    {InstructionKind::PCPYUD, {".pcpyud", {}}},

    // NOTE - depending on how this is used, this may case issues! Be Warned!
    // lots of implicit logic in OpenGOAL depending on argument types!
    {InstructionKind::MFC1, {".mov", {}}},

    // ---- COP2 -----
    // TODO - VMOVE supports dest, but OpenGOAL does NOT yet!
    {InstructionKind::VMOVE, {".mov.vf", {MOD::DEST_MASK}}},

    // Load and Store
    {InstructionKind::LQC2, {".lvf", {MOD::OFFSET}}},
    {InstructionKind::QMFC2, {".mov", {}}},
    {InstructionKind::SQC2, {".svf", {MOD::OFFSET, MOD::SWAP_FIRST_TWO_SOURCE_ARGS}}},
    {InstructionKind::QMTC2, {".mov", {}}},

    // Redundant ops, NOP and WAIT
    {InstructionKind::VNOP, {".nop.vf", {}}},
    {InstructionKind::VWAITQ, {".wait.vf", {}}},

    // Max / Min
    {InstructionKind::VMAX, {".max.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMAX_BC, {".max.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},
    {InstructionKind::VMINI, {".min.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMINI_BC, {".min.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},

    // Addition / Addition with ACC
    {InstructionKind::VADD, {".add.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VADD_BC, {".add.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},
    {InstructionKind::VADDA, {".add.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VADDA_BC, {".add.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},

    // Subtraction
    {InstructionKind::VSUB, {".sub.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VSUB_BC, {".sub.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},

    // Multiplication / Multiplication with ACC
    {InstructionKind::VMUL, {".mul.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMUL_BC, {".mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},
    {InstructionKind::VMULA, {".mul.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMULA_BC, {".mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK}}},

    // Add or Subtract with the resulting product / use the ACC
    {InstructionKind::VMADD, {".add.mul.vf", {MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMADD_BC,
     {".add.mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMADDA, {".add.mul.vf", {MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMADDA_BC,
     {".add.mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMSUB, {".sub.mul.vf", {MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMSUB_BC,
     {".sub.mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMSUBA_BC,
     {".sub.mul.{}.vf", {MOD::BROADCAST, MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},
    {InstructionKind::VMSUBQ, {".sub.mul.vf", {MOD::DEST_MASK, MOD::ACC_THIRD_SRC_ARG}}},

    // Absolute value
    {InstructionKind::VABS, {".abs.vf", {MOD::DEST_MASK}}},

    // Outer-product
    // NOTE - currently it's assumed these groups of instructions will be replaced with 1
    {InstructionKind::VOPMULA, {"TODO.VOPMULA.vf", {}}},
    {InstructionKind::VOPMSUB, {".outer.product.vf", {MOD::SWAP_FIRST_TWO_SOURCE_ARGS}}},

    // Division
    {InstructionKind::VDIV, {".div.vf", {MOD::FTF, MOD::FSF}}},

    // Square-root
    {InstructionKind::VSQRT, {".sqrt.vf", {MOD::FTF}}},
    {InstructionKind::VRSQRT, {".isqrt.vf", {MOD::FTF, MOD::FSF}}},

    // Operations using the result of division
    {InstructionKind::VADDQ, {".add.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VSUBQ, {".sub.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMULQ, {".mul.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VMULAQ, {".mul.vf", {MOD::DEST_MASK}}},

    //// Random number generation
    {InstructionKind::VRGET, {"TODO.VRGET", {}}},
    {InstructionKind::VRXOR, {"TODO.VRXOR", {}}},
    {InstructionKind::VRNEXT, {"TODO.VRNEXT", {}}},

    //// VU Integer operations
    {InstructionKind::VMTIR, {"TODO.VMTIR", {}}},
    {InstructionKind::VIAND, {"TODO.VIAND", {}}},
    {InstructionKind::VIADDI, {"TODO.VIADDI", {}}},

    //// Load/store from VU memory
    {InstructionKind::VLQI, {"TODO.VLQI", {}}},
    {InstructionKind::VSQI, {"TODO.VSQI", {}}},

    //// Fixed point conversions
    {InstructionKind::VFTOI0, {".ftoi.vf", {MOD::DEST_MASK}}},
    {InstructionKind::VITOF0, {".itof.vf", {MOD::DEST_MASK}}},
    // NOTE - Only the .xyzw mask is supported via macros!
    {InstructionKind::VFTOI4, {".vftoi4.xyzw", {MOD::DEST_MASK}}},
    {InstructionKind::VITOF12, {".vitof12.xyzw", {MOD::DEST_MASK}}},
    {InstructionKind::VFTOI12, {".vftoi12.xyzw", {MOD::DEST_MASK}}},
    {InstructionKind::VITOF15, {".vitof15.xyzw", {MOD::DEST_MASK}}},

    //// Status Checks
    {InstructionKind::VCLIP, {"TODO.VCLIP", {}}},
};

bool OpenGOALAsm::Function::allows_modifier(InstructionModifiers mod) {
  return std::find(modifiers.begin(), modifiers.end(), mod) != modifiers.end();
}

OpenGOALAsm::OpenGOALAsm(Instruction _instr) : instr(_instr) {
  if (MIPS_ASM_TO_OPEN_GOAL_FUNCS.count(instr.kind) == 0) {
    valid = false;
  } else {
    func = MIPS_ASM_TO_OPEN_GOAL_FUNCS.at(instr.kind);
    if (func.funcTemplate.rfind("TODO", 0) == 0) {
      todo = true;
    }
  }
}

OpenGOALAsm::OpenGOALAsm(Instruction _instr,
                         std::optional<RegisterAccess> _dst,
                         const std::vector<std::optional<RegisterAccess>>& _src)
    : instr(_instr), m_dst(_dst), m_src(_src) {
  if (MIPS_ASM_TO_OPEN_GOAL_FUNCS.count(instr.kind) == 0) {
    valid = false;
  } else {
    func = MIPS_ASM_TO_OPEN_GOAL_FUNCS.at(instr.kind);
    if (func.funcTemplate.rfind("TODO", 0) == 0) {
      todo = true;
    }
  }
}

std::string OpenGOALAsm::full_function_name() {
  std::string func_name = func.funcTemplate;
  // OpenGOAL uses the function name for broadcast specification
  if (func.allows_modifier(MOD::BROADCAST)) {
    if (instr.cop2_bc != 0xff) {
      std::string bc = std::string(1, instr.cop2_bc_to_char());
      func_name = fmt::format(func_name, bc);
    }
  }
  return func_name;
}

std::vector<goos::Object> OpenGOALAsm::get_args(const std::vector<DecompilerLabel>& labels,
                                                const Env& env) {
  std::vector<goos::Object> args;
  std::vector<goos::Object> named_args;

  bool got_fsf = false;
  for (int i = 0; i < instr.n_src; i++) {
    auto v = m_src.at(i);
    InstructionAtom atom = instr.get_src(i);

    if (v.has_value()) {
      // Normal register / constant args
      args.push_back(v.value().to_form(env));
    } else if (atom.kind == InstructionAtom::AtomKind::VF_FIELD) {
      // Handle FTF/FSF operations
      if (func.allows_modifier(MOD::FTF) && func.allows_modifier(MOD::FSF)) {
        if (got_fsf) {
          named_args.push_back(
              pretty_print::to_symbol(fmt::format(":ftf #b{:b}", atom.get_vf_field())));
        } else {
          got_fsf = true;
          named_args.push_back(
              pretty_print::to_symbol(fmt::format(":fsf #b{:b}", atom.get_vf_field())));
        }
      } else if (func.allows_modifier(MOD::FSF)) {
        named_args.push_back(
            pretty_print::to_symbol(fmt::format(":fsf #b{:b}", atom.get_vf_field())));
      } else if (func.allows_modifier(MOD::FTF)) {
        named_args.push_back(
            pretty_print::to_symbol(fmt::format(":ftf #b{:b}", atom.get_vf_field())));
      } else {
        assert(false);
      }
    } else if (func.allows_modifier(MOD::OFFSET) && atom.kind == InstructionAtom::AtomKind::IMM) {
      // Handle offsetting
      if (atom.get_imm() != 0) {
        named_args.push_back(pretty_print::to_symbol(fmt::format(":offset {}", atom.get_imm())));
      }
    } else {
      args.push_back(pretty_print::to_symbol(atom.to_string(labels)));
    }
  }

  // Handle third-argument accumulator
  if (func.allows_modifier(MOD::ACC_THIRD_SRC_ARG)) {
    args.push_back(pretty_print::to_symbol("acc"));
  }

  // Handle destination masks
  if (func.allows_modifier(MOD::DEST_MASK) && instr.cop2_dest != 0xff && instr.cop2_dest != 15) {
    named_args.push_back(
        pretty_print::to_symbol(fmt::format(":mask #b{:b}", instr.cop2_dest_mask_intel())));
  }

  // Some functions are configured, or its easiest to swap the source args
  // NOTE - this currently assumes it is the first two args that must be swapped
  if (func.allows_modifier(MOD::SWAP_FIRST_TWO_SOURCE_ARGS)) {
    std::swap(args.at(0), args.at(1));
  }

  args.insert(args.end(), named_args.begin(), named_args.end());
  return args;
}
}  // namespace decompiler
