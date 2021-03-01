#include "OpenGoalMapping.h"
#include "common/goos/PrettyPrinter.h"
#include <optional>

// TODO - Known Issues:
// - init / use ACC for instructions that implicitly use it
namespace decompiler {
const std::map<InstructionKind, OpenGOALAsm::Function> MIPS_ASM_TO_OPEN_GOAL_FUNCS = {
    // ----- EE -------
    {InstructionKind::PSLLW, {".pw.sll", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::PSRAW, {".pw.sra", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::PSUBW, {"TODO.psubw", {}, {}}},

    // TODO - this is waiting on proper 128-bit int support in OpenGOAL
    {InstructionKind::LQ, {"TODO.lq", {}, {OpenGOALAsm::InstructionModifiers::OFFSET}}},
    {InstructionKind::SQ, {"TODO.sq", {}, {OpenGOALAsm::InstructionModifiers::OFFSET}}},

    // NOTE - depending on how this is used, this may case issues! Be Warned!
    {InstructionKind::MFC1, {".mov", {}, {}}},

    // ---- COP2 -----
    // TODO - VMOVE supports dest, but OpenGOAL does NOT yet!
    {InstructionKind::VMOVE, {"TODO.mov", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Load and Store
    {InstructionKind::LQC2, {".lvf", {}, {OpenGOALAsm::InstructionModifiers::OFFSET}}},
    {InstructionKind::QMFC2, {".mov", {}, {}}},
    {InstructionKind::SQC2,
     {".svf",
      {},
      {OpenGOALAsm::InstructionModifiers::OFFSET,
       OpenGOALAsm::InstructionModifiers::SWAP_SOURCE_ARGS}}},
    {InstructionKind::QMTC2, {".mov", {}, {}}},

    // Redundant ops, NOP and WAIT
    {InstructionKind::VNOP, {".nop.vf", {}, {}}},
    {InstructionKind::VWAITQ, {".wait.vf", {}, {}}},

    // Max / Min
    {InstructionKind::VMAX, {".max.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMAX_BC,
     {".max.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMINI, {".min.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMINI_BC,
     {".min.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Addition / Addition with ACC
    {InstructionKind::VADD, {".add.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VADD_BC,
     {".add.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VADDA,
     {".add.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VADDA_BC,
     {".add.{}.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Subtraction
    {InstructionKind::VSUB, {".sub.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VSUB_BC,
     {".sub.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    // TODO - missing VSUBA?

    // Multiplication / Multiplication with ACC
    {InstructionKind::VMUL, {".mul.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMUL_BC,
     {".mul.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMULA,
     {".mul.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMULA_BC,
     {".mul.{}.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Add or Subtract with the resulting product / use the ACC
    {InstructionKind::VMADD, {".add.mul.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMADD_BC,
     {".add.mul.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMADDA,
     {".add.mul.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMADDA_BC,
     {".add.mul.{}.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMSUB, {".sub.mul.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMSUB_BC,
     {".sub.mul.{}.vf",
      {},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    // TODO - missing a VMSUBA?
    {InstructionKind::VMSUBA_BC,
     {".sub.mul.{}.vf",
      {OpenGOALAsm::AdditionalVURegisters::ACC},
      {OpenGOALAsm::InstructionModifiers::BROADCAST,
       OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Absolute value
    {InstructionKind::VABS, {".abs.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    // Outer-product
    // NOTE - currently it's assumed these groups of instructions will be replaced with 1
    {InstructionKind::VOPMULA, {".outer.product.vf", {}, {}}},
    {InstructionKind::VOPMSUB,
     {".outer.product.vf", {}, {OpenGOALAsm::InstructionModifiers::SWAP_SOURCE_ARGS}}},

    // Division
    {InstructionKind::VDIV,
     {".div.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_DST},
      {OpenGOALAsm::InstructionModifiers::FTF, OpenGOALAsm::InstructionModifiers::FSF}}},

    // Square-root
    {InstructionKind::VSQRT,
     {".sqrt.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_DST},
      {OpenGOALAsm::InstructionModifiers::FTF}}},
    {InstructionKind::VRSQRT, {"TODO.vrsqrt", {}, {}}},  // TODO - implement in compiler!

    // Operations using the result of division
    {InstructionKind::VADDQ,
     {".add.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_SRC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VSUBQ,
     {".sub.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_SRC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMULQ,
     {".mul.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_SRC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VMULAQ,
     {".mul.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_SRC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    // TODO - missing a vmaddq?
    {InstructionKind::VMSUBQ,
     {".sub.mul.vf",
      {OpenGOALAsm::AdditionalVURegisters::Q_SRC},
      {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},

    //// Random number generation
    //{InstructionKind::VRGET, "not-implemented"},
    //{InstructionKind::VRXOR, "not-implemented"},
    //{InstructionKind::VRNEXT, "not-implemented"},

    //// VU Integer operations
    //{InstructionKind::VMTIR, "not-implemented"},
    //{InstructionKind::VIAND, "not-implemented"},
    //{InstructionKind::VIADDI, "not-implemented"},

    //// Load/store from VU memory
    //{InstructionKind::VLQI, "not-implemented"},
    //{InstructionKind::VSQI, "not-implemented"},

    //// Fixed point conversions
    {InstructionKind::VFTOI0, {".ftoi.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VFTOI4, {"TODO.VFTOI4", {}, {}}},
    {InstructionKind::VFTOI12, {"TODO.VFTOI12", {}, {}}},
    {InstructionKind::VITOF0, {".itof.vf", {}, {OpenGOALAsm::InstructionModifiers::DEST_MASK}}},
    {InstructionKind::VITOF12, {"TODO.VITOF12", {}, {}}},
    {InstructionKind::VITOF15, {"TODO.VITOF15", {}, {}}},

    //// Status Checks
    //{InstructionKind::VCLIP, "not-implemented"},
};

bool OpenGOALAsm::Function::allows_modifier(InstructionModifiers mod) {
  return std::find(modifiers.begin(), modifiers.end(), mod) != modifiers.end();
}

OpenGOALAsm::OpenGOALAsm(Instruction _instr) {
  instr = _instr;
  if (MIPS_ASM_TO_OPEN_GOAL_FUNCS.count(instr.kind) == 0) {
    valid = false;
  } else {
    func = MIPS_ASM_TO_OPEN_GOAL_FUNCS.at(instr.kind);
  }
}

OpenGOALAsm::OpenGOALAsm(Instruction _instr,
                         std::optional<Variable> _dst,
                         std::vector<std::optional<Variable>> _src) {
  instr = _instr;
  m_dst = _dst;
  m_src = _src;
  if (MIPS_ASM_TO_OPEN_GOAL_FUNCS.count(instr.kind) == 0) {
    valid = false;
  } else {
    func = MIPS_ASM_TO_OPEN_GOAL_FUNCS.at(instr.kind);
  }
}

std::string OpenGOALAsm::full_function_name() {
  std::string func_name = func.funcTemplate;
  // OpenGOAL uses the function name for broadcast specification
  if (func.allows_modifier(InstructionModifiers::BROADCAST)) {
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

  for (int i = 0; i < instr.n_src; i++) {
    auto v = m_src.at(i);
    InstructionAtom atom = instr.get_src(i);

    if (v.has_value()) {
      // Normal register / constant args
      args.push_back(v.value().to_form(env));
    } else if (atom.kind == InstructionAtom::AtomKind::VF_FIELD) {
      // Handle FTF/FSF operations
      if (func.allows_modifier(InstructionModifiers::FTF) && args.size() == 0) {
        named_args.push_back(pretty_print::to_symbol(fmt::format(":ftf #b{:b}", atom.get_imm())));
      } else if (func.allows_modifier(InstructionModifiers::FSF)) {
        named_args.push_back(pretty_print::to_symbol(fmt::format(":fsf #b{:b}", atom.get_imm())));
      }
    } else if (func.allows_modifier(InstructionModifiers::OFFSET) &&
               atom.kind == InstructionAtom::AtomKind::IMM) {
      // Handle offsetting
      if (atom.get_imm() != 0) {
        named_args.push_back(pretty_print::to_symbol(fmt::format(":offset {}", atom.get_imm())));
      }
    } else {
      args.push_back(pretty_print::to_symbol(atom.to_string(labels)));
    }
  }

  if (instr.kind == InstructionKind::VFTOI0) {
    int x = 0;
  }

  // Handle destination masks
  if (func.allows_modifier(InstructionModifiers::DEST_MASK) && instr.cop2_dest != 0xff &&
      instr.cop2_dest != 15) {
    named_args.push_back(pretty_print::to_symbol(fmt::format(":mask #b{:b}", instr.cop2_dest)));
  }

  // Some functions are configured, or its easiest to swap the source args
  // NOTE - this currently assumes it is the first two args that must be swapped
  if (func.allows_modifier(InstructionModifiers::SWAP_SOURCE_ARGS)) {
    std::swap(args.at(0), args.at(1));
  }

  args.insert(args.end(), named_args.begin(), named_args.end());
  return args;
}
}  // namespace decompiler
