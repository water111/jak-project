# KERNEL

## `gcommon`: **Done**
- `vec4s` print/inpsect unimplemented (believed not working in GOAL either)
- `array`'s `print`/`inspect` will not work on `uint128` or `int128`. These are believed to be unused, and also don't work in GOAL.
- `quad-copy!` is an optimized assembly memory copy that was rewritten. It should have identical behavior, but may be slow. In the future, we could improve the performance if it's used a lot.
- `valid?` uses some inline assembly to check if a pointer is inside the symbol table, rewritten.
- Lots of important memory constants should be defined here.

## `gstring-h`: **Done**
- This file generates no code.

## `gkernel-h`: **Done**
- The types `cpu-thread` and `catch-frame` have a slightly different layout in OpenGOAL to back up x86-64 registers

## `gkernel`:
- Many changes for x86-64/OpenGOAL
- A few bugs in the game related to `process-tree` vs `(pointer process-tree)`.
- `change-brother` is totally wrong, unused, and left out.

## `pskernel`: **Done**
- Unimplemented in OpenGOAL. Seems to be debugging hooks into the PS2's kernel. Error strings indicate that there should have been a second related file included that actually contained the debugging handlers, but this file is not present.

## `gstring`: **Done**
- `string->int` doesn't handle negative numbers correctly. This appears to be a bug in the original version.

## `dgo-h`: **Done**
- Just type definitions. These don't seem to match the version of DGO files found in the game, so maybe this is outdated? Also GOAL never sees DGOs, they are always processed on the IOP.

## `gstate`: **Done**
- Doing a `go` from a non-main thread of the process that is changing state is implemented a tiny bit differently. I don't think it should matter.

# ENGINE

## `types-h`: **Done**
- Just some bitfield types.

## `vu1-macros`: **Done**
- Empty

## `math`: **Done**
- The VU random generator has been rewritten, it used the PS2's (very bad) random hardware
- The "31 bit" integer random generator was rewritten, it used very strange inline assembly.

## `vector-h`: **Done**
- Has some very simple, manually rewritten VU functions

## `gravity-h`: **Done**
- Empty

## `bounding-box-h`: **Done**

## `matrix-h`: **Done**
- `matrix-copy!` is a good example of where the OpenGOAL compiler's register allocator does poorly.

## `quaternion-h`: **Done**
- No comments

## `euler-h`: **Done**
- Uses boxed arrays

## `transform-h`: **Done**
- No comments

## `geometry-h`: **Done**
- No comments

## `trigonometry-h`: **Done**
- Empty

## `transformq-h`: waiting on stack stuff
- Needs stack stuff

## `bounding-box`: asm

## `matrix`: asm

## `transform`: asm

## `quaternion`: asm

## `euler`: asm

## `geometry`: asm

## `trigonometry`: **Done**
- `sincos!` and `sincos-rad!` have a bug where cosine is slightly off

## `gsound-h`: **Done**

## `timer-h`: **Done**
- `timer-init` removed for PC port.

## `timer`: Decompiled, probably needs porting
- Has functions for accessing PS2 timers, needs to be ported to PC. All the logic for stopwatches etc is decopmiled, just the timer reads/writes need to be modified.

## `vif-h`: **Done**

## `dma-h`: **Done**
- DMA sync disabled by default.

## `video-h`: **Done**

## `vu1-user-h`: **Done**

## `dma`: **Done**
- `dma-initialize` disabled on PC.
- Plan is to modify things at a higher level than this for the PC graphics system, most of these functions are non-functional on the PC.
- `ultimate-memcpy` may need to be modified, or we can just swap out code to use the non-`ultimate` version of `memcpy`.

## `dma-buffer`: **Done**
- The sends won't work on PC, but it should let you build the buffer...

## `dma-bucket`: **Done**
- Could clean up some bitfield access, should probably add some features to the compiler to help here.

## `dma-disasm`: In progress
- Unused, but possibly useful debugging utilities for printing DMA chains.
- Needs stack stuff to do the last two functions
- Needs static data disassembler for a lookup table.

## `pad`: **Done**

## `gs`: **Done**
- Missing bitfields

## `display-h`: **Done**

## `vector`: asm

## `fileio`: **Done**

## `loader-h`: **Done**
- Good one for playing with inlined basics.

## `anim-tester`: In progress
- Various functions need decompiler or compiler support on multiple fronts:
 - Stack variables.
 - GOAL behaviors.
 - GOAL defstate.
 - better subclass support
- Need to find where some functions are called to verify the argument types.