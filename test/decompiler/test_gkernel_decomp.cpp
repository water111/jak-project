#include "gtest/gtest.h"
#include "FormRegressionTest.h"

using namespace decompiler;

TEST_F(FormRegressionTest, ExprMethod7Object) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    or v0, a0, r0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0\n";
  std::string type = "(function object int object)";
  std::string expected = "arg0";
  test_with_expr(func, type, expected);
}

TEST_F(FormRegressionTest, ExprLoadPackage) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L278:\n"
      "    daddiu sp, sp, -48\n"
      "    sd ra, 0(sp)\n"
      "    sq s5, 16(sp)\n"
      "    sq gp, 32(sp)\n"

      "    or gp, a0, r0\n"
      "    or s5, a1, r0\n"
      "    lw t9, nmember(s7)\n"
      "    or a0, gp, r0\n"
      "    lw a1, *kernel-packages*(s7)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    bne s7, v0, L279\n"
      "    or v0, s7, r0\n"

      "    lw t9, dgo-load(s7)\n"
      "    or a0, gp, r0\n"
      "    addiu a2, r0, 15\n"
      "    lui a3, 32\n"
      "    or a1, s5, r0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw v1, pair(s7)\n"
      "    lwu t9, 16(v1)\n"
      "    daddiu a0, s7, global\n"
      "    lw a1, pair(s7)\n"
      "    lw a3, *kernel-packages*(s7)\n"
      "    or a2, gp, r0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    sw v0, *kernel-packages*(s7)\n"
      "L279:\n"
      "    ld ra, 0(sp)\n"
      "    lq gp, 32(sp)\n"
      "    lq s5, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 48\n";
  std::string type = "(function string kheap pair)";
  std::string expected =
      "(when\n"
      "  (not (nmember arg0 *kernel-packages*))\n"
      "  (dgo-load arg0 arg1 15 2097152)\n"
      "  (set! v0-1 (cons arg0 *kernel-packages*))\n"
      "  (set! *kernel-packages* v0-1)\n"
      "  v0-1\n"
      "  )";
  test_with_expr(func, type, expected);
}

TEST_F(FormRegressionTest, ExprUnloadPackage) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    daddiu sp, sp, -16\n"
      "    sd ra, 0(sp)\n"
      "    lw t9, nmember(s7)\n"
      "    lw a1, *kernel-packages*(s7)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"
      "    beq s7, v1, L277\n"
      "    or a0, s7, r0\n"

      "    lw t9, delete!(s7)\n"
      "    lw a0, -2(v1)\n"
      "    lw a1, *kernel-packages*(s7)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    sw v0, *kernel-packages*(s7)\n"
      "    or v1, v0, r0\n"
      "L277:\n"
      "    lw v0, *kernel-packages*(s7)\n"
      "    ld ra, 0(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 16\n";
  std::string type = "(function string pair)";
  std::string expected =
      "(begin\n"
      "  (set! v1-0 (nmember arg0 *kernel-packages*))\n"
      "  (if v1-0 (set! *kernel-packages* (delete! (car v1-0) *kernel-packages*)))\n"
      "  *kernel-packages*\n"
      "  )";
  test_with_expr(func, type, expected, true);
}

TEST_F(FormRegressionTest, ExprMethod1Thread) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L274:\n"
      "    lwu v1, 4(a0)\n"
      "    lwu v1, 40(v1)\n"
      "    bne a0, v1, L275\n"
      "    or v1, s7, r0\n"

      "    lw r0, 2(r0)\n"
      "    addiu v1, r0, 0\n"

      "L275:\n"
      "    lwu v0, 8(a0)\n"
      "    lwu v1, 4(a0)\n"
      "    sw v0, 44(v1)\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function thread none)";
  std::string expected =
      "(begin\n"
      "  (when (= arg0 (-> arg0 process main-thread)) (break!) (set! v1-3 0))\n"
      "  (set! (-> arg0 process top-thread) (-> arg0 previous))\n"
      "  )";
  test_with_expr(func, type, expected, false);
}

TEST_F(FormRegressionTest, ExprMethod2Thread) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L273:\n"
      "    daddiu sp, sp, -32\n"
      "    sd ra, 0(sp)\n"
      "    sd fp, 8(sp)\n"
      "    or fp, t9, r0\n"
      "    sq gp, 16(sp)\n"

      "    or gp, a0, r0\n"
      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L343\n"
      "    lwu a2, -4(gp)\n"
      "    lwu a3, 0(gp)\n"
      "    lwu v1, 4(gp)\n"
      "    lwu t0, 0(v1)\n"
      "    lwu t1, 20(gp)\n"
      "    or t2, gp, r0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v0, gp, r0\n"
      "    ld ra, 0(sp)\n"
      "    ld fp, 8(sp)\n"
      "    lq gp, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 32";
  std::string type = "(function thread thread)";
  std::string expected =
      "(begin\n"
      "  (format\n"
      "   (quote #t)\n"
      "   \"#<~A ~S of ~S pc: #x~X @ #x~X>\"\n"
      "   (-> arg0 type)\n"
      "   (-> arg0 name)\n"
      "   (-> arg0 process name)\n"
      "   (-> arg0 pc)\n"
      "   arg0\n"
      "   )\n"
      "  arg0\n"
      "  )";
  test_with_expr(func, type, expected, false, "", {{"L343", "#<~A ~S of ~S pc: #x~X @ #x~X>"}});
}

TEST_F(FormRegressionTest, ExprMethod9Thread) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L268:\n"
      "    daddiu sp, sp, -16\n"
      "    sd ra, 0(sp)\n"
      "    sd fp, 8(sp)\n"
      "    or fp, t9, r0\n"

      "    lwu a2, 4(a0)\n"
      "    lwu v1, 40(a2)\n"
      "    beq a0, v1, L269\n"
      "    sll r0, r0, 0\n"

      "    lw t9, format(s7)\n"
      "    addiu a0, r0, 0\n"
      "    daddiu a1, fp, L342\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"
      "    beq r0, r0, L272\n"
      "    sll r0, r0, 0\n"

      "L269:\n"
      "    lw v1, 32(a0)\n"
      "    bne v1, a1, L270\n"
      "    sll r0, r0, 0\n"

      "    or v1, s7, r0\n"
      "    beq r0, r0, L272\n"
      "    sll r0, r0, 0\n"

      "L270:\n"
      "    lw v1, 32(a0)\n"
      "    daddiu v1, v1, -4\n"
      "    lwu a3, -4(a0)\n"
      "    lhu a3, 8(a3)\n"
      "    daddu v1, v1, a3\n"
      "    daddu v1, v1, a0\n"
      "    lwu a3, 84(a2)\n"
      "    bne a3, v1, L271\n"
      "    sll r0, r0, 0\n"

      "    daddiu v1, a1, -4\n"
      "    lwu a3, -4(a0)\n"
      "    lhu a3, 8(a3)\n"
      "    daddu v1, v1, a3\n"
      "    daddu v1, v1, a0\n"
      "    sw v1, 84(a2)\n"
      "    sw a1, 32(a0)\n"
      "    or v1, a1, r0\n"
      "    beq r0, r0, L272\n"
      "    sll r0, r0, 0\n"

      "L271:\n"
      "    lw t9, format(s7)\n"
      "    addiu a0, r0, 0\n"
      "    daddiu a1, fp, L341\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"

      "L272:\n"
      "    or v0, r0, r0\n"
      "    ld ra, 0(sp)\n"
      "    ld fp, 8(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 16";
  std::string type = "(function thread int none)";
  std::string expected =
      "(begin\n"
      "  (set! a2-0 (-> arg0 process))\n"
      "  (cond\n"
      "   ((!= arg0 (-> a2-0 main-thread)) (format 0 \"1 ~A ~%\" a2-0))\n"
      "   ((= (-> arg0 stack-size) arg1))\n"
      "   ((=\n"
      "     (-> a2-0 heap-cur)\n"
      "     (+\n"
      "      (+ (+ (-> arg0 stack-size) -4) (the-as int (-> arg0 type size)))\n"
      "      (the-as int arg0)\n"
      "      )\n"
      "     )\n"
      "    (set!\n"
      "     (-> a2-0 heap-cur)\n"
      "     (+ (+ (+ arg1 -4) (the-as int (-> arg0 type size))) (the-as int arg0))\n"
      "     )\n"
      "    (set! (-> arg0 stack-size) arg1)\n"
      "    )\n"
      "   (else (format 0 \"2 ~A ~%\" a2-0))\n"
      "   )\n"
      "  (set! v0-2 0)\n"
      "  )";
  test_with_expr(func, type, expected, false, "", {{"L342", "1 ~A ~%"}, {"L341", "2 ~A ~%"}});
}

TEST_F(FormRegressionTest, ExprMethod0Thread) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    lwu v1, 44(a2)\n"
      "    beq s7, v1, L266\n"
      "    sll r0, r0, 0\n"

      "    daddiu v0, t1, -7164\n"
      "    beq r0, r0, L267\n"
      "    sll r0, r0, 0\n"

      "L266:\n"
      "    addiu v1, r0, -16\n"
      "    lwu a0, 84(a2)\n"
      "    daddiu a0, a0, 15\n"
      "    and v1, v1, a0\n"
      "    lhu a0, 8(a1)\n"
      "    daddu a0, v1, a0\n"
      "    daddu a0, a0, t0\n"
      "    sw a0, 84(a2)\n"
      "    daddiu v0, v1, 4\n"

      "L267:\n"
      "    sw a1, -4(v0)\n"
      "    sw a3, 0(v0)\n"
      "    sw a2, 4(v0)\n"
      "    sw t1, 24(v0)\n"
      "    sw t1, 28(v0)\n"
      "    lwu v1, 44(a2)\n"
      "    sw v1, 8(v0)\n"
      "    sw v0, 44(a2)\n"
      "    lwu v1, -4(v0)\n"
      "    lwu v1, 56(v1)\n"
      "    sw v1, 12(v0)\n"
      "    lwu v1, -4(v0)\n"
      "    lwu v1, 60(v1)\n"
      "    sw v1, 16(v0)\n"
      "    sw t0, 32(v0)\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function symbol type process symbol int pointer thread)";
  std::string expected =
      "(begin\n"
      "  (set!\n"
      "   v0-0\n"
      "   (cond\n"
      "    ((-> arg2 top-thread) (+ arg5 (the-as uint -7164)))\n"
      "    (else\n"
      "     (set!\n"
      "      v1-2\n"
      "      (logand -16 (the-as int (+ (-> arg2 heap-cur) (the-as uint 15))))\n"
      "      )\n"
      "     (set! (-> arg2 heap-cur) (+ (+ v1-2 (the-as int (-> arg1 size))) arg4))\n"
      "     (+ v1-2 4)\n"
      "     )\n"
      "    )\n"
      "   )\n"
      "  (set! (-> (the-as cpu-thread v0-0) type) arg1)\n"
      "  (set! (-> v0-0 name) arg3)\n"
      "  (set! (-> v0-0 process) arg2)\n"
      "  (set! (-> v0-0 sp) arg5)\n"
      "  (set! (-> v0-0 stack-top) arg5)\n"
      "  (set! (-> v0-0 previous) (-> arg2 top-thread))\n"
      "  (set! (-> arg2 top-thread) v0-0)\n"
      // TODO - we could make this method access nicer.
      "  (set! (-> v0-0 suspend-hook) (method-of-object v0-0 thread-suspend))\n"
      "  (set! (-> v0-0 resume-hook) (method-of-object v0-0 thread-resume))\n"
      "  (set! (-> v0-0 stack-size) arg4)\n"
      "  v0-0\n"
      "  )";
  test_with_expr(func, type, expected, false, "cpu-thread", {},
                 parse_hint_json("[[13, [\"v0\", \"cpu-thread\"]]]"));
}

TEST_F(FormRegressionTest, ExprMethod5CpuThread) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L264:\n"
      "    lwu v1, -4(a0)\n"
      "    lhu v1, 8(v1)\n"
      "    lw a0, 32(a0)\n"
      "    daddu v0, v1, a0\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function cpu-thread int)";
  std::string expected = "(the-as int (+ (-> arg0 type size) (the-as uint (-> arg0 stack-size))))";
  test_with_expr(func, type, expected, false);
}

TEST_F(FormRegressionTest, RemoveExit) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L262:\n"
      "    lwu v1, 88(s6)\n"
      "    beq s7, v1, L263\n"
      "    or v0, s7, r0\n"

      "    lwu v1, 88(s6)\n"
      "    lwu v0, 4(v1)\n"
      "    sw v0, 88(s6)\n"

      "L263:\n"
      "    jr ra\n"
      "    daddu sp, sp, r0";
  std::string type = "(function stack-frame)";
  std::string expected =
      "(when\n"
      "  (-> pp stack-frame-top)\n"
      "  (set! v0-0 (-> pp stack-frame-top next))\n"
      "  (set! (-> pp stack-frame-top) v0-0)\n"
      "  v0-0\n"
      "  )";
  test_with_expr(func, type, expected, false);
}

TEST_F(FormRegressionTest, RemoveMethod0ProcessTree) {
  std::string func =
      "    sll r0, r0, 0\n"
      "    daddiu sp, sp, -32\n"
      "    sd ra, 0(sp)\n"
      "    sq gp, 16(sp)\n"

      "    or gp, a2, r0\n"
      "    lw v1, object(s7)\n"
      "    lwu t9, 16(v1)\n"
      "    or v1, a1, r0\n"
      "    lhu a2, 8(a1)\n"
      "    or a1, v1, r0\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    sw gp, 0(v0)\n"
      "    addiu v1, r0, 256\n"
      "    sw v1, 4(v0)\n"
      "    sw s7, 8(v0)\n"
      "    sw s7, 12(v0)\n"
      "    sw s7, 16(v0)\n"
      "    sw v0, 24(v0)\n"
      "    daddiu v1, v0, 24\n"
      "    sw v1, 20(v0)\n"
      "    ld ra, 0(sp)\n"
      "    lq gp, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 32";
  std::string type = "(function symbol type basic process-tree)";
  std::string expected =
      "(begin\n"
      "  (set! v0-0 (object-new arg0 arg1 (the-as int (-> arg1 size))))\n"
      "  (set! (-> v0-0 name) arg2)\n"
      "  (set! (-> v0-0 mask) 256)\n"
      "  (set! (-> v0-0 parent) (quote #f))\n"
      "  (set! (-> v0-0 brother) (quote #f))\n"
      "  (set! (-> v0-0 child) (quote #f))\n"
      "  (set! (-> v0-0 self) v0-0)\n"
      "  (set! (-> v0-0 ppointer) (&-> v0-0 self))\n"
      "  v0-0\n"
      "  )";
  test_with_expr(func, type, expected, false, "process-tree");
}

TEST_F(FormRegressionTest, RemoveMethod3ProcessTree) {
  std::string func =
      "    sll r0, r0, 0\n"

      "    daddiu sp, sp, -32\n"
      "    sd ra, 0(sp)\n"
      "    sd fp, 8(sp)\n"
      "    or fp, t9, r0\n"
      "    sq gp, 16(sp)\n"

      "    or gp, a0, r0\n"
      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L340\n"
      "    or a2, gp, r0\n"
      "    lwu a3, -4(gp)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L339\n"
      "    lwu a2, 0(gp)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L338\n"
      "    lwu a2, 4(gp)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L337\n"
      "    lwu v1, 8(gp)\n"
      "    beq s7, v1, L258\n"
      "    or a2, s7, r0\n"

      "    lwu v1, 0(v1)\n"
      "    lwu a2, 24(v1)\n"

      "L258:\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L336\n"
      "    lwu v1, 12(gp)\n"
      "    beq s7, v1, L259\n"
      "    or a2, s7, r0\n"

      "    lwu v1, 0(v1)\n"
      "    lwu a2, 24(v1)\n"

      "L259:\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    lw t9, format(s7)\n"
      "    daddiu a0, s7, #t\n"
      "    daddiu a1, fp, L335\n"
      "    lwu v1, 16(gp)\n"
      "    beq s7, v1, L260\n"
      "    or a2, s7, r0\n"

      "    lwu v1, 0(v1)\n"
      "    lwu a2, 24(v1)\n"

      "L260:\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v0, gp, r0\n"
      "    ld ra, 0(sp)\n"
      "    ld fp, 8(sp)\n"
      "    lq gp, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 32";
  std::string type = "(function process-tree process-tree)";
  // gross, but expected. see https://github.com/water111/jak-project/issues/254
  std::string expected =
      "(begin\n"
      "  (format (quote #t) \"[~8x] ~A~%\" arg0 (-> arg0 type))\n"
      "  (format (quote #t) \"~Tname: ~S~%\" (-> arg0 name))\n"
      "  (format (quote #t) \"~Tmask: #x~X~%\" (-> arg0 mask))\n"
      "  (set! t9-3 format)\n"
      "  (set! a0-4 (quote #t))\n"
      "  (set! a1-3 \"~Tparent: ~A~%\")\n"
      "  (set! v1-0 (-> arg0 parent))\n"
      "  (t9-3 a0-4 a1-3 (if v1-0 (-> v1-0 0 self)))\n"
      "  (set! t9-4 format)\n"
      "  (set! a0-5 (quote #t))\n"
      "  (set! a1-4 \"~Tbrother: ~A~%\")\n"
      "  (set! v1-2 (-> arg0 brother))\n"
      "  (set! a2-4 (if v1-2 (-> v1-2 0 self)))\n"
      "  (t9-4 a0-5 a1-4 a2-4)\n"
      "  (set! t9-5 format)\n"
      "  (set! a0-6 (quote #t))\n"
      "  (set! a1-5 \"~Tchild: ~A~%\")\n"
      "  (set! v1-4 (-> arg0 child))\n"
      "  (t9-5 a0-6 a1-5 (if v1-4 (-> v1-4 0 self)))\n"
      "  arg0\n"
      "  )";
  test_with_expr(func, type, expected, false, "process-tree",
                 {{"L340", "[~8x] ~A~%"},
                  {"L339", "~Tname: ~S~%"},
                  {"L338", "~Tmask: #x~X~%"},
                  {"L337", "~Tparent: ~A~%"},
                  {"L336", "~Tbrother: ~A~%"},
                  {"L335", "~Tchild: ~A~%"}});
}

TEST_F(FormRegressionTest, ExprMethod0Process) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L254:\n"
      "    daddiu sp, sp, -48\n"
      "    sd ra, 0(sp)\n"
      "    sq s5, 16(sp)\n"
      "    sq gp, 32(sp)\n"

      "    or s5, a2, r0\n"
      "    or gp, a3, r0\n"
      "    lw v1, symbol(s7)\n"
      "    lwu a2, -4(a0)\n"
      "    bne a2, v1, L255\n"
      "    sll r0, r0, 0\n"

      "    lw v1, object(s7)\n"
      "    lwu t9, 16(v1)\n"
      "    lw v1, process(s7)\n"
      "    lhu v1, 8(v1)\n"
      "    daddu a2, v1, gp\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    beq r0, r0, L256\n"
      "    sll r0, r0, 0\n"

      "L255:\n"
      "    daddiu v0, a0, 4\n"

      "L256:\n"
      "    sw s5, 0(v0)\n"
      "    daddiu v1, s7, dead\n"
      "    sw v1, 32(v0)\n"
      "    sw r0, 36(v0)\n"
      "    sw s7, 28(v0)\n"
      "    sw gp, 68(v0)\n"
      "    sw s7, 44(v0)\n"
      "    sw s7, 40(v0)\n"
      "    daddiu v1, v0, 108\n"
      "    sw v1, 84(v0)\n"
      "    sw v1, 76(v0)\n"
      "    lw v1, 68(v0)\n"
      "    daddiu v1, v1, 108\n"
      "    daddu v1, v1, v0\n"
      "    sw v1, 80(v0)\n"
      "    lwu v1, 80(v0)\n"
      "    sw v1, 88(v0)\n"
      "    sw s7, 88(v0)\n"
      "    sw s7, 52(v0)\n"
      "    sw s7, 72(v0)\n"
      "    sw s7, 48(v0)\n"
      "    sw s7, 56(v0)\n"
      "    sw s7, 60(v0)\n"
      "    sw s7, 64(v0)\n"
      "    sw s7, 8(v0)\n"
      "    sw s7, 12(v0)\n"
      "    sw s7, 16(v0)\n"
      "    sw v0, 24(v0)\n"
      "    daddiu v1, v0, 24\n"
      "    sw v1, 20(v0)\n"
      "    ld ra, 0(sp)\n"
      "    lq gp, 32(sp)\n"
      "    lq s5, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 48";
  std::string type = "(function symbol type basic int process)";
  std::string expected =
      "(begin\n"
      "  (set!\n"
      "   v0-0\n"
      "   (if\n"
      "    (= (-> arg0 type) symbol)\n"
      "    (object-new arg0 arg1 (the-as int (+ (-> process size) (the-as uint arg3))))\n"
      "    (+ (the-as int arg0) 4)\n"
      "    )\n"
      "   )\n"
      "  (set! (-> (the-as process v0-0) name) arg2)\n"
      "  (set! (-> v0-0 status) (quote dead))\n"
      "  (set! (-> v0-0 pid) 0)\n"
      "  (set! (-> v0-0 pool) (quote #f))\n"
      "  (set! (-> v0-0 allocated-length) arg3)\n"
      "  (set! (-> v0-0 top-thread) (quote #f))\n"
      "  (set! (-> v0-0 main-thread) (quote #f))\n"
      "  (set! v1-5 (-> v0-0 stack))\n"
      "  (set! (-> v0-0 heap-cur) v1-5)\n"
      "  (set! (-> v0-0 heap-base) v1-5)\n"
      "  (set! (-> v0-0 heap-top) (&-> v0-0 stack (-> v0-0 allocated-length)))\n"
      "  (set! (-> v0-0 stack-frame-top) (-> v0-0 heap-top))\n"
      "  (set! (-> v0-0 stack-frame-top) (quote #f))\n"
      "  (set! (-> v0-0 state) (quote #f))\n"
      "  (set! (-> v0-0 next-state) (quote #f))\n"
      "  (set! (-> v0-0 entity) (quote #f))\n"
      "  (set! (-> v0-0 trans-hook) (quote #f))\n"
      "  (set! (-> v0-0 post-hook) (quote #f))\n"
      "  (set! (-> v0-0 event-hook) (quote #f))\n"
      "  (set! (-> v0-0 parent) (quote #f))\n"
      "  (set! (-> v0-0 brother) (quote #f))\n"
      "  (set! (-> v0-0 child) (quote #f))\n"
      "  (set! (-> v0-0 self) v0-0)\n"
      "  (set! (-> v0-0 ppointer) (&-> v0-0 self))\n"
      "  v0-0\n"
      "  )";
  test_with_expr(func, type, expected, false, "process", {},
                 parse_hint_json("[\t\t[12, [\"a0\", \"int\"]],\n"
                                 "\t\t[13, [\"v0\", \"process\"]]]"));
}

TEST_F(FormRegressionTest, ExprInspectProcessHeap) {
  std::string func =
      "    sll r0, r0, 0\n"
      "L251:\n"
      "    daddiu sp, sp, -64\n"
      "    sd ra, 0(sp)\n"
      "    sq s4, 16(sp)\n"
      "    sq s5, 32(sp)\n"
      "    sq gp, 48(sp)\n"

      "    or gp, a0, r0\n"
      "    lwu v1, 76(gp)\n"
      "    daddiu s5, v1, 4\n"
      "    beq r0, r0, L253\n"
      "    sll r0, r0, 0\n"

      "L252:\n"
      "    or a0, s5, r0\n"
      "    lwu v1, -4(a0)\n"
      "    lwu t9, 28(v1)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"
      "    addiu s4, r0, -16\n"
      "    or a0, s5, r0\n"
      "    lwu v1, -4(a0)\n"
      "    lwu t9, 36(v1)\n"
      "    jalr ra, t9\n"
      "    sll v0, ra, 0\n"

      "    or v1, v0, r0\n"
      "    daddiu v1, v1, 15\n"
      "    and v1, s4, v1\n"
      "    daddu s5, s5, v1\n"

      "L253:\n"
      "    lwu v1, 84(gp)\n"
      "    slt v1, s5, v1\n"
      "    bne v1, r0, L252\n"
      "    sll r0, r0, 0\n"

      "    or v0, s7, r0\n"
      "    ld ra, 0(sp)\n"
      "    lq gp, 48(sp)\n"
      "    lq s5, 32(sp)\n"
      "    lq s4, 16(sp)\n"
      "    jr ra\n"
      "    daddiu sp, sp, 64";
  std::string type = "(function process symbol)";
  std::string expected =
      "(begin\n"
      "  (set! s5-0 (+ (-> arg0 heap-base) (the-as uint 4)))\n"
      "  (while\n"
      "   (< (the-as int s5-0) (the-as int (-> arg0 heap-cur)))\n"
      "   (inspect (the-as basic s5-0))\n"
      "   (set!\n"
      "    (the-as int s5-0)\n"
      "    (+ (the-as int s5-0) (logand -16 (+ (asize-of s5-0) 15)))\n"
      "    )\n"
      "   )\n"
      "  (quote #f)\n"
      "  )\n"
      "";
  test_with_expr(func, type, expected, false, "", {},
                 parse_hint_json("[\t\t[4, [\"s5\", \"basic\"]],\n"
                                 "\t\t[17, [\"s5\", \"int\"]]]"));
}