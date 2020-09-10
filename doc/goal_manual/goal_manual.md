# GOAL2 Lanuge Manual

# Introduction

## Basics
GOAL2 is a LISP-based language based on GOAL, a custom langauge for the PlayStation 2 developed by Naughty Dog. Unlike the original GOAL, GOAL2 targets x86-64. Although the language has a syntax and macro system that's similar to Common LISP, the low-level details of the language are much closer to C, with some C++ features.  The programmer has to manually manage memory, and the programming style is very similar to C instead of typical LISP/Scheme.

A GOAL program is made up of the GOAL runtime (called `gk`, written in C++), the GOAL kernel (called `gkernel`, written in GOAL), and the actual code for the program.  This combination of the runtime, kernel and program is called the __target__.  Although GOAL has a runtime, it is __not__ an interpreted language - the runtime is only used to bootstrap the GOAL kernel and provide access to C/C++ libraries, like the I/O driver OVERLORD, which ran on the separate I/O Processor in the PlayStation 2 version.  The C/C++ code is only a small fraction (around 10k out of 500k lines) of the total game. All the interesting parts, like graphics, animation, collision, custom multi-threaded kernel, and memory management are written in GOAL.

An important part of GOAL is that it's interactive! The compiler remains running throughout the entire programming process and connects to the running target through a component called the __Listener__, and allows the programmer to update the code as the game is running. The typical use is to make small tweaks, add debugging print statements, or add new code piece-by-piece while testing it - more complicated changes like modifying types that are already in use will require the programmer to be extremely careful, or just restart the code.  The feature to modify code has little safety checking and should be used with care.

There is a REPL which also allows code fragments to be quickly uploaded and executed, and also accepts commands that tell the GOAL compiler what to do - things like compile a file, connect to the target, or quit the comipler.

## Andy Gavin
Andy Gavin is the Creator of GOAL.  This is what he has to say about it:

Traditional programming languages provide inadequate expression and power for the needs of modern game programming.  G.O.A.L. (Game Object Assembly Lisp) is a heavily modified dialect of R5 scheme which attempts to address these problems.  Specifically, it is a compiled language, with no interpreter, yet it has a fully interactive listener in which any language fragment may be dynamically compiled, linked and executed into a running program.  Inspection and modification of any data structure, as well as dynamic replacement of functions in running code are trivial.  Goal includes support for coprocessors and inline assembly with an unprecedented degree of integration.  There is a single unified syntax for scheme and multiple coprocessor assemblies.  Assembly instructions may be mixed freely with high level code without the use of special inline syntaxes.  Variables, registers, and data structures can be accessed identically by both.  Goal also has a uniform scheme-like syntax, true macros, and a sophisticated type/object system which gives it similar object power to C++, but with a uniform syntax and better control over low level features.  Finally, Goal has a multithreaded runtime kernel which supports separated concepts of “process” as stack and “thread” as execution.  When tied in with the object system this allows the easy creation of  individual state based execution contexts that have control over their own local memory usage.  This is perfect for games which have a multitude of different on screen objects each with their own local contexts.  Programmers of the world unite!  C is the opiate of the masses, the era of real programming languages is on hand!

## Getting Started with Editing GOAL
LISP/Scheme are dead languages at this point, so many modern editors don't have good support for indenting and highlighting LISP.  I use Sublime Text with LispIndent and some modifications to the settings. Once we start adding lots of GOAL code, we should revisit this...

## Projects in GOAL
Because the GOAL compiler stays running throughout the entire build process, there's no need for header files, like in C or C++. The build system is straightforward - it has a list of files and compiles them in order.  A file may only refer to functions/variables/macros/etc in a file defined before it.  A similar rule applies within a source file - you can only reference things that were defined previously in the file.  Of course, you can forward declare things and have circular references, just like in C.

You may see files like `gkernel-h.gc` - these usually contain type definitions like C headers do, but aren't actually handled differently from any other `.gc` file. This is unlike C, where `.h` and `.c` files are handled differently.  I suspect splitting files into interface and implementation was used to make the code cleaner, and possibly remove dependencies - we wouldn't want everything that depends on the kernel interface to have to rebuild each time we change the kernel.

## A `.gc` file
Each `gc` file is compiled into a GOAL code object, which can then be __loaded__.  The order that `.gc` files are loaded in is the same as they are compiled in. All of the code, function defintions, type defintions, and variable defintions in a GOAL source file is in one function, called the `top-level` function. The defintions and code run in the order they appear in the file. This runs exactly once, when the file is __loaded__. Each function definition, type definition, and method definition actually becomes a small piece of code that adds the function/type/method to the appropriate spot in the running game when the file is __loaded__.  You can also put some code in the `top-level` function, and this is sometimes done for initialization.

One tricky thing in GOAL is that defining functions/types/methods/variable is a dynamic thing - the `define` statement actually causes something to be loaded or set up. It's not safe to run code that uses a function/method/variable whose `define` hasn't been executed. The responsibility of getting this right is up to the programmer. The language is designed so that it's hard to accidentally write code which gets this wrong, but it's not impossible when forward declaring things, or doing weird things like putting a `(define)` inside of an `if`.

An example of this trick:
`gcommon.gc` is loaded before `gstring.gc`.  However, functions in `gcommon.gc` call functions defined in `gstring.gc`. This is OK as long as both
1. The functions in `gstring.gc` are forward declared so the compiler knows about them. 
2. The functions in `gcommon.gc` that use `gstring.gc` functions should not be called until `gstring.gc` is loaded.

Part 1. will be checked by the compiler - if you fail to forward declare, or forward declare the wrong type, it will fail to compile. However, 2. isn't checked (it's impossible in the general case, see the halting problem) and is up to the programmer to get right.