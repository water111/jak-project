# GOAL Operations

## `div.s`
Suspected source
```
(/ 1.0 x)
```
where `x` is in a GPR:
```
    lwc1 f0, L345(fp) ;; first argument prepared first?
    mtc1 f1, a0       ;; second argument prepared second?
    div.s f0, f0, f1
```
Sequence
- Compile first
- First to FPR
- Compile second
- Second to FPR

## `daddu`
Used for `int` and `uint` addition.

Two element form:
```
daddu v0, a0, a1
```
is `(+ a0 a1)` - the order in the opcode matches the order in the expression.

## `daddiu` to get a symbol
```
daddiu v0, s7, #t
```
Note for `#t`: `#t` is linked when the code literally has a `#t` in it. Other cases are currently unknown.

## `dsubu`
Used for `int` and `uint` subtraction. 

## `mult3` (EE `mult`)
Used for `int` multiplication.

Like `daddu` for opcode ordering:
```
mult3 v0, a0, a1
```
is `(* a0 a1)`.

## `div`
Used for `int` division.

```
    div a0, a1
    mflo v0
```
is `(/ a0 a1)`.

and also for `int` mod
```
    div a0, a1
    mfhi v0
```
## `or` used to get the value of false
```
or v0, s7, r0
```

## `or` used as a bitwise or
```
or v0, a0, a1
```
is `(logior a0 a1)`

## `and` used as a bitwise and
```
and v0, a0, a1
```
is `(logand a0 a1)`.

```
(logand #xfffffff0 (+ (ash (-> thing field) 2) 43))
```
is
```
    ld v1, L346(fp)     ;; first arg to the and
    lhu a0, 14(a0)      ;; second arg evaluation...
    dsll a0, a0, 2
    daddiu a0, a0, 43
    and v0, v1, a0      ;; and result, first, second
```

## `nor` used as a bitwise nor
```
nor v0, a0, a1
```
is `(lognor a0 a1)`

## `xor` used as a bitwise xor
```
xor v0, a0, a1
```
is `(logxor a0 a1)`

## `nor` used as a logical not
```
nor v0, a0, r0
```
is `(lognot a0)`



# Common "Idioms"

## `ash`
Variable shift (`ash`) is an inline function
```
    or v1, a0, r0
    bgezl a1, L306
    dsllv v0, v1, a1

    dsubu a0, r0, a1
    dsrav v0, v1, a0
L306:
```

## `abs` of integer
```
    or v0, a0, r0
    bltzl v0, L302
    dsubu v0, r0, v0
L302:
```

## `min` of integers
```
    or v0, a0, r0
    or v1, a1, r0
    slt a0, v0, v1
    movz v0, v1, a0
```

## `max` of integers
```
    or v0, a0, r0
    or v1, a1, r0
    slt a0, v0, v1
    movn v0, v1, a0
```

# Others

## Integer constants that are large
A constant of `0xfffffff0` is loaded with `ld`

## Access value of symbol
Seems to always use `lw`?

# Control Flow Info


## Begin-like forms flush everything always, immediately after compiling
Example in `vector` with flushing:
```
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; .function vector3s+!
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
L8:
    daddiu sp, sp, -16
    sd fp, 8(sp)
    or fp, t9, r0
    daddiu v1, fp, L109 ;; string: "Add 2 vectors3."
    lwc1 f0, 0(a1)
    lwc1 f1, 0(a2)
    add.s f0, f0, f1
    swc1 f0, 0(a0)
    lwc1 f0, 4(a1)
    lwc1 f1, 4(a2)
    add.s f0, f0, f1
    swc1 f0, 4(a0)
    lwc1 f0, 8(a1)
    lwc1 f1, 8(a2)
    add.s f0, f0, f1
    swc1 f0, 8(a0)
    or v0, a0, r0
    ld fp, 8(sp)
    jr ra
    daddiu sp, sp, 16
```
The `daddiu v1, fp, L109` loads a `string` into the `v1` register which is never used, immediately after the prologue. This will only happen if the value is flushed. This is very likely a documentation comment that accidentally got included as a string constant. It's unused, so there was likely no consumer of the string that did the `flush` - it was done by the top level evaluation.
```
(defun vector3s+! (stuff)
  "Add 2 vectors3." ;; oops, a string constant instead of a comment.
  ... ; rest of the function
)
```
## Return-From evaluates to 0 bug
We would expect the value of `(return-from #f x)` to be nothing, as there's no possible way to use it. However, GOAL seems to have a small bug where `(return-from #f x)` always attempts to evaluate to 0.  This would be like implementing it as:
```lisp
(set! return-reg return-value)
(goto end-of-function)
0 ;; oops
```
by accident.

Example in GOAL:

```
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; .function basic-type? (in gcommon)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
L285:
    lwu v1, -4(a0)
    lw a0, object(s7)
L286:
    bne v1, a1, L287
    or a2, s7, r0
                       ; (return-from #f #t) starts here
    daddiu v1, s7, #t  ; compile/flush the #t
    or v0, v1, r0      ; move to return register
    beq r0, r0, L288   ; branch to end
    sll r0, r0, 0      ; branch delay slot (usual filler)

    or v1, r0, r0      ; unreachable loading of 0 into a register.
L287:
    lwu v1, 4(v1)
    bne v1, a0, L286
    sll r0, r0, 0

    or v0, s7, r0
L288:
    jr ra
    daddu sp, sp, r0

    sll r0, r0, 0
    sll r0, r0, 0
```    

## Unused else case returning false in cond
From `delete!` in gcommon.
```
    beq a2, a0, L222  ; (if (= a2 a0) only-one-case)
    or a0, s7, r0     ; a0 is unused return value of if

    lw a0, 2(a2)      ; (set! (cdr v1) (cdr a2)), will evaluate to (cdr a2) which is stored in a0
    sw a0, 2(v1)
L222:                 ; a0 = #f or a0 = (cdr a2) depending on branch taken, but it's totally unused!
    or v0, a1, r0     ; return a1
    jr ra
    daddu sp, sp, r0
```
Also note that all cases stored their result in `a0`, even though nothing uses the result.

## Function Calls evaluate arguments in order:
```
    lw t9, format(s7)    ;; head of function
    daddiu a0, s7, #t    ;; first arg
    daddiu a1, fp, L344  ;; second arg
    sllv a2, gp, r0      ;; third arg
    dsra32 a3, gp, 0     ;; fourth arg
    pcpyud v1, gp, r0 
    sllv t0, v1, r0      ;; fifth arg
    pcpyud v1, gp, r0
    dsra32 t1, v1, 0     ;; sixth arg
    por t2, gp, r0       ;; seventh arg
    jalr ra, t9
    sll v0, ra, 0
```
also an example of lack of common subexpression elimination on the `pcpyud v1, gp, r0`s.

### A second example with register type conversions:
```
    lw t9, format(s7)    ;; function
    daddiu a0, s7, #t    ;; first arg
    daddiu a1, fp, L343  ;; second arg
    lwc1 f0, 0(gp)       ;; compile and flush third arg
    mfc1 a2, f0          ;; move to correct reg type
    jalr ra, t9
    sll v0, ra, 0
```