# Emitter
x86-64 has a lot of instructions.  They are described in Volume 2 of the 5 Volume "Intel® 64 and IA-32 Architectures Software Developer’s Manual". Just this volume alone is over 2000 pages, which would take forever to fully implement.  As a result, we will use only a subset of these instructions.  This the rough plan:

- Most instructions like `add` will only be implemented with `r64 r64` versions.
- To accomplish something like `add rax, 1`, we will use a temporary register `X`
  - `mov X, 1`
  - `add rax, X`
  - The constant propagation system will be able to provide enough information that we could eventually use `add r64 immX` and similar if needed.
  - Register allocation should handle the case `(set! x (+ 3 y))` as:
     - `mov x, 3`
     - `add x, y`
  - but `(set! x (+ y 3))`, in cases where `y` is needed after and `x` can't take its place, will become the inefficient
     - `mov x, y`
     - `mov rtemp, 3`
     - `add x, rtemp`
- Loading constants into registers will be done efficiently, using the same strategy used by modern versions of `gcc` and `clang`.
- Memory access will be done in the form `mov rdest, [roff + raddr]` where `roff` is the offset register. Doing memory access in this form was found to be much faster in simple benchmark test.
- Memory access to the stack will have an extra `sub` and more complicated dereference.  GOAL code seems to avoid using the stack in most places, and I suspect the programmers attempted to avoid stack spills.
  - `mov rdest, rsp` : coloring move for upcoming subtract
  - `sub rdest, roff` : convert real pointer to GOAL pointer
  - `mov rdest, [rdest + roff + variable_offset]` : access memory through normal GOAL deref.
  - Note - we should check that the register allocator gets this right always, and eliminates moves and avoid using a temporary register.
  - Again, the constant propagation should give use enough information, if we ever want/need to implement a more efficient `mov rdest, [rsp + variable_offset]` type instructions.
- Memory access to static data should use `rip` addressing, like `mov rdest, [rip + offset]`. And creating pointers to static data could be `lea rdest, [rip - roff + offset]`