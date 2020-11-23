# Porting to x86
This document will keep track of stuff that needs to be ported or modified significantly for x86. Anything that uses PS2-specific hardware or relies on stuff in the C Kernel will need to be ported.

## Basic Info
Most of the game is written in GOAL. All this source lives in `goal_src`.

The "runtime" is all the support code written in C++. It's located in `game/`. Sometimes the "runtime" + GOAL code together is called the "target".

Most of the code in "runtime" is reverse engineered code from the real game, with small tweaks to make it work on x86 and with OpenGOAL. 

The code in `game/system` is **not** from the game and is an implementation of system functions that are implemented by Sony in the PS2 game. It's stuff like threading, I/O, etc.

The code in `game/sce` is my implementation of the Sony libraries. When possible, I tried to keep exactly the same names/functions as the real Sony libraries. This way our reverse engineered game code can look very similar to the original, which is satisfying and fun.

The PS2's main CPU is called the EE. It runs GOAL code and the C Kernel, which is Naughty Dog's C++ code.  The C Kernel is responsible for bootstrapping GOAL's kernel and exposing Sony library functions to GOAL code.  The C Kernel is in `game/kernel`.  In OpenGOAL the "EE Thread" runs code than ran on the EE on the PS2. This includes the C Kernel and all GOAL code. There is a single EE thread - GOAL implements its own threading, but this all runs in the same Linux/Windows thread.

The PS2 has a separate I/O Processor called the IOP. It runs the OVERLORD driver written by Naughty Dog in C. OpenGOAL uses C++ for its implementation of OVERLORD. Like with the EE, there are Sony libraries for the IOP. These are in `game/sce/iop` to distinguish them from EE code. The IOP can run independently from the EE. Unlike the EE, the IOP itself has multiple threads (7 threads) that each have their own OS thread. But only one IOP thread runs at a time, as the IOP was a single-core CPU.

To give an idea of the size of these (counted by `wc -l`):
- OVERLORD is 3700 lines, but still has a lot to implement,
- C Kernel is 7432 lines, and is mostly done
- SCE is 973 lines, and still has some to implement
- System is 1294 lines, and still has some to implement

## Math Libraries
I think most of the math libraries can be decompiled, but there are a few that will need manual work. These are also a great place to do tests as the math functions have very few dependencies and we know what the right answer should be for many of them.

- `bounding-box` (only some stuff)
- `math` (only `rand-uint31-gen`)
- `matrix` (only some stuff)
- `geometry` (only some stuff)
- `trigonometry` (only some stuff)

At some point we may want to re-implement some of these to be more efficient.

## The IOP (I/O Processor) Framework
This is already implemented.

The IOP was a separate I/O Processor on the PS2. It runs a cooperative multi-tasking kernel developed by Sony. In OpenGOAL it is implemented in `game/system/IOP_Kernel.h`.  The IOP Kernel is managed by the IOP runtime thread (`game/system/iop_thread.h`).

The library in `game/sce/iop.h` wraps the `IOP_Kernel` in an interface that looks like the Sony libraries used by the game so the IOP code can be ported directly. 

There are a few stub functions that are hardcoded to return the correct values for stuff like CD drive initialization.  The main features currently supported are:
- Threads (create, wakeup, start, sleep, delay, get ID)
- Messageboxes to pass data between threads (send, poll)
- SIF RPC, a library to receive remote procedure calls from the EE. See `game/sce/sif_ee.h` for the wrapper of the EE side library, and `game/kernel/kdgo.cpp` and `ksound.cpp` for the wrapper around the EE library that's exposed to GOAL.
- DMA to the EE for sending data from the IOP to GOAL.

All this stuff is currently used for loading DGOs, which is tested and working.

## OVERLORD Framework
This is already implemented.

The OVERLORD is the code written by Naughty Dog that runs on the IOP. It is responsible for sound and loading data.  It's surprisingly complicated and some parts of it are extremely poorly written, especially the thread synchronization stuff.  My implementation of OVERLORD is in `game/overlord`. It's not complete yet, but the basics are there and it does enough to load DGOs.

The framework for OVERLORD is already implemented. The C Kernel calls a Sony library function to load OVERLORD. This library function is `sceSifLoadModule`, implemented in `sif_ee.cpp`, which tells the IOP Kernel code to start.  This starts up the OVERLORD thread which eventually calls `start_overlord` in `game/overlord/overlord.cpp`.  This `start_overlord` function is the entry point to Naughty Dog's OVERLORD library and starts a bunch more threads (see `InitISOFS`), using the Sony IOP library.  In total there are 7 threads.

Once `start_overlord` returns, the initial call to `sceSifLoadModule` returns and the runtime keeps initializing.

## OVERLORD ISO Thread
This is partially implemented.

This thread is responsible for controlling the DVD drive and the small DVD data buffers used in the IOP. It has a big loop in `ISOThread()` in `iso.cpp` that looks for pending reads, executes them, waits for data to be read, then calls a callback.  This code is unbelievably confusing.

It receives commands from other OVERLORD threads (using a MessageBox) and uses the priority queue implemented in `iso_queue.cpp` to decide which read gets to go first.

To interact with the DVD drive, it uses an `IsoFS` abstraction, which is a struct containing function pointers to control the drive. The version of OVERLORD in the retail game has only one implemented, called `iso_cd` which uses the actual drive in the PS2. There's also a reference to `fakeiso`, but this is empty in the game. Instead of "emulating" the CD drive functions, I implemented my own version of `fakeiso` mode in `fake_iso.cpp`.  This just reads files from your hard drive and uses the `fakeiso.txt` file to map files in the `jak-project` folder to OVERLORD file names (it has it's own system for naming files).

It also has some sound stuff in it for managing VAG audio streams, but this isn't implemented yet.

The other threads in OVERLORD are "RPC" threads. They sit in a loop waiting for the main runtime thread (EE thread) to send a remote procedure call (RPC). Then they do something (like maybe sending a message to the ISO thread), maybe wait for something to happen, and then return. 

From the GOAL/EE side of things, RPC calls can be blocking or non-blocking. They can be issued from GOAL (with `rpc-call`) or from the C Kernel (`RpcCall`). Per "channel" (corresponds to an IOP thread), there can only be one RPC call happening at a time. The `rpc-busy?` command can be used to check if an RPC is complete. 

## IOP PLAY (6)
This is unimplemented.

The `PLAY` RPC appears to be relatively simple and plays/stops/pauses/queues a VAG audio stream. It can either use the "AnimationName" system or another system to get the name of the audio stream.  I don't know what sound effects in the game are streamed, but I believe there are some.

I suspect the GOAL side code for this is in `gsound` and `gsound-h`.

## IOP STR (5)
This is unimplemented.

This is an RPC for streaming data back to the EE. I think this is used to control animation streaming.

## IOP DGO (4)
This is implemented.

This is the RPC for loading DGO files.  The DGO loading is super complicated, but the basic idea is that loading / linking are double buffered. In order to allow linking files to allocate memory, the currently loading file goes in a temporary buffer on the top of the heap. (There are actually two temp buffers that rotate, one for loading out of and one for linking, as the "copy to heap" step is done as part of linking, not loading)

The final chunk is not double buffered. This is so it can be loaded directly into its final location in the heap. This has three advantages: you don't need to copy it out of a temporary buffer, you can have a file larger than the temp buffer and you can also entirely fill the heap this way (the temp buffers are freed so you don't have to worry about that).

The IOP side state machine for this is in `iso.cpp`, implemented inside of the DGO load buffer complete callback and is somewhat complicated because DGO info may be split between multiple buffers, and you have to deal with getting partial info.  The EE side is in `kdgo.cpp`. 

The DGO synchronization is pretty confusing but I believe I have it working. It may be worth documenting it more (I thought I did already, but where did I put it?).

## IOP Server/Ramdisk (3)
This is implemented, but so far unused and untested.

This RPC is used to store files in RAM on the IOP. There's a buffer of around 800 kB. I believe it's used for a few different things, in particular the level visibility data. The EE requests data to be loaded from a file on DVD into the "ramdisk" (just a buffer on the IOP), then can request chunks of this file. Of course it is not as fast as storing the file in the EE RAM, but it is much faster than reading from the DVD again.

This is what Andy Gavin refers to when they said they did "things they weren't supposed to" with the "one megabyte of memory that wasn't being used".


## IOP Loader (2)
This is unimplemented.

This is used to control the loading of music and soundbanks. I haven't touched it yet.  Music and soundbanks are loaded into IOP memory when you switch levels.

## IOP Player (1)
This is unimplemented.

This is used to control the playing of sound, and goes with Loader. Like PLAY it can play VAG audio streams. I'm not sure which one is actually used for streaming audio, maybe both?

## IOP VBlank Handler
This is unimplemented.

The IOP's kernel will call `VBlank_Handler` on each vblank. This is once per frame, and I don't know where it is, or if its tied to the actual HW vblank or framebuffer swap, if it happens at 30/60 fps (or even/odd frames if 30 fps). I suspect it's the real vblank at 60 fps but I don't know.

This does some music fade calculations and sends some data to the EE.  In GOAL this is called the `sound-iop-info`.

The EE first has to do some set up to tell the IOP where to copy the data, which I believe is done in another sound RPC from GOAL.

We'll also need to add some stuff to `system` and `sce/iop` to set this up, which will have to work with frame timing stuff so it happens at the right part of the frame.


## Sound Library
This is a pretty big one. To actually make sounds, OVERLORD code uses a third-party sound library called 989SND. Internally 989SND uses the SPU2 (Sound Processor) to actually do the "sound math" to decode ADPCM, do ADSR for the sampled sounds, and do reverb/mixing.

I think the lowest effort sound implementation is to try to reimplement 989SND + the SPU as a single library.  This could be tested and developed in isolation from everything else.

We'll also need to pick a library for doing audio on PC and a design for how to keep the audio in sync. My gut feeling is to let the IOP side audio stuff just run totally independent from everything else, like the real game does. Let the audio sampling be driven by the sound device so you never have any crackling/interpolation artifacts. This is why the audio keeps going even after the game crashes on PS2.

## GOAL Kernel
The GOAL kernel needs some modification to work on x86. It implements userspace threading and needs to know the details of how to back up the current CPU state and restore it.  It also needs to work with the compiler to make sure that the kernel and compiler agree on what registers may not be preserved across a thread suspend  There are also some CPU specific details on how to do dynamic throw/catches, unwinding stack frames, and passing initial arguments to a thread.

In OpenGOAL, the `rsp` is a "real" pointer and all other pointers are "GOAL pointer"s (offset from base of GOAL memory), so there are some details needed to correctly save/restore stacks.

A final detail is we will probably want/need the ability to increase the default size of stack that can be backed up on suspend. The default is 256 bytes so if our compiler does worse than the original and we use more stack space, we could run out. There's a check for this so it shouldn't be hard to detect.

## Jak Graphics Basics
The PS2 has some special hardware that's used for graphics. These are the DMAC, the VU1, and the GS. 

The DMAC is a sophisticated DMA controller. It runs separately from the EE and can copy data from one place to another at pretty high speed. If it is not stalled for any reason it can reach 2.4 GB/sec.  The main RAM is only good for around 1.2 GB/sec so in practice "big" things don't move around any faster than 1.2 GB/sec on average. It's used to send graphics data from main memory to the other components.  It can be configured, but it's not programmable. It can do simple transfers, like "copy this block of data from here to there", and more complicated things, like following linked lists. 

The VU1 takes the role of vertex shaders. It can be programmed, but only in assembly, and it is extremely challenging and confusing. It has an extremely small memory (16 kB), but this memory is extremely fast. It's role is usually to do vertex transformations and lighting, then generate a list of commands to send to the GS.  The `XGKICK` instruction on VU1 is used to send data from the VU1 memory to the GS.

The GS is the actual GPU. It has VRAM and receives commands from a few different places, including:
- VU1 `XGICK`s stuff to it directly, bypassing the main bus used by DMAC/CPU memory access. This is called PATH 1 and is most commonly used in Jak 1.
- When DMAing stuff to VU1, it first goes through a thing called VIF1 which can "unpack" data.  There is a special command that you can give to VIF1 which tells it to "send data directly to the GS".
- DMA sends data directly from EE main memory to GS (Path 3), unused by Jak 1

The GS is like pixel shaders but it's very simple - it's not programmable and only can do a few fixed things. The GS also has the VRAM, which can contain frame buffers, z buffers, textures, and scratch area for effects.

My understanding is that during a frame, the EE generates a long list of things to draw. These are a DMA "chain" - basically a complicated linked-list like data structure that the PS2's DMA knows how to handle.  I believe some graphics calculations are done on the EE - particularly the environment mapping. 

## DMA

## Display

## Texture

## Collision System

## Joint

## BSP

## Merc Blend Shape

## Ripple

## Bones

## Generic Merc

## Generic TIE

## Shadow

## Font

## Decompression

## Background

## Draw Node Culling

## Shrubbery

## TFRAG

## TIE

## Particle

## Time of Day

## Sky

## Load boundary

## Sound

## Controllers

## IOP Streaming

## Ocean

## Navigate