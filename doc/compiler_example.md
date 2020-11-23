# Compiler Example
This describes how the compiler works on a small piece of code in `example_goal.gc`.
To run this yourself, start the compiler and runtime, then run:

```
(lt)
(asm-file "doc/example_goal.gc" :color :load)
```

And you should see:
```
The value of 10 factorial is 3628800
```

# Overview
The code to read from the GOAL REPL is in `Compiler.cpp`, in `Compiler::execute_repl`. Compiling an `asm-file` form will call `Compiler::compile_asm_file` in `CompilerControl.cpp`, which is where we'll start.

I've divided the process into these steps:
1. __Read__: Convert from text to a representation of Lisp syntax.
2. __IR-Pass__: Convert the S-Expressions to an intermediate representation (IR).
3. __Register Allocation__: Map variables in the IR (`IRegister`s) to real hardware registers
4. __Code Generation__: Generate x86-64 instructions from the IR
5. __Object File Generation__: Put the instructions and static data in an object file and generate linking data
6. __Sending__: Send the code to the runtime
7. __Linking__: The runtime links the code so it can be run, and runs it.
8. __Result__: The code prints the message which is sent back to the REPL.

# Reader
The reader converts GOAL/GOOS source into a `goos::Object`. One of the core ideas of lisp is that "code is data", so GOAL code is represented as GOOS data.  This makes it easy for GOOS macros to operate on GOAL code.  A GOOS object can represent a number, string, pair, etc. This strips out comments/whitespace. 

The reader is run with this code:
```
auto code = m_goos.reader.read_from_file({filename});
```
If you were to `code.print()`, you would get:
```
(top-level (defun factorial-iterative ((x integer)) (let ((result 1)) (while (!= x 1) (set! result (* result x)) (set! x (- x 1))) result)) (define-extern _format function) (define format _format) (let ((x 10)) (format #t "The value of ~D factorial is ~D~%" x (factorial-iterative x))))
```


There are a few details worth mentioning about this process:
- The reader will expand `'my-symbol` to `(quote my-symbol)`
- The reader will throw errors on syntax errors (mismatched parentheses, bad strings/numbers, etc.)
- Using `read_from_file` adds information about where each thing came from to a map stored in the reader.  This map is used to determine the source file/line for compiler errors.

# IR Pass
This pass converts code (represented as a `goos::Object`) into intermediate representation.  This is stored in an `Env*`, a tree structure.  At the top is a `GlobalEnv*`, then an `FileEnv` for each file compiled, then a `FunctionEnv` for each function in the file.  There are environments within `FunctionEnv` that are used for lexical scoping and the other types of GOAL scopes. The Intermediate Representation (IR) is a list per function that's built up in order as the compiler goes through the function. Note that the IR is a list of instructions and doesn't have a tree or more complicated structure.  Here's an example of the IR for the example function:

```
Function: function-factorial-iterative
  mov-ic igpr-2, 1 
  mov igpr-3, igpr-2 
  goto label-10
  mov igpr-4, igpr-3
  imul igpr-4, igpr-0
  mov igpr-3, igpr-4
  mov igpr-5, igpr-0
  mov-ic igpr-6, 1
  subi igpr-5, igpr-6
  mov igpr-0, igpr-5
  mov-ic igpr-7, 1
  j(igpr-0 != igpr-7) label-3
  ret igpr-1 igpr-3

Function: function-top-level
  mov-fa igpr-0, function-factorial-iterative
  mov 'factorial-iterative, igpr-0
  mov igpr-1, '_format
  mov 'format, igpr-1
  mov-ic igpr-2, 10
  mov igpr-3, igpr-2
  mov igpr-4, '#t
  mov-sva igpr-5, static-string "The value of ~D factorial is ~D~%"
  mov igpr-6, 'factorial-iterative
  mov igpr-8, igpr-3
  call igpr-6 (ret igpr-7) (args igpr-8)
  mov igpr-9, igpr-7
  mov igpr-10, 'format
  mov igpr-12, igpr-4
  mov igpr-13, igpr-5
  mov igpr-14, igpr-3
  mov igpr-15, igpr-9
  call igpr-10 (ret igpr-11) (args igpr-12 igpr-13 igpr-14 igpr-15)
  mov igpr-16, igpr-11
  ret igpr-17 igpr-16
```
The `function-top-level` is the "top level" function, which is everything not in a function. In the example, this is just defining the function, defining `format`, and calling `format`. 

You'll notice that there are a ton of `mov`s between `igpr`. The compiler inserts tons of moves. Because this is all done in a single pass, there's a lot of cases where the compiler can't know if a move is needed or not. But the register allocator can figure it out and will remove most unneeded moves.  Adding moves can also prevent stack spills. For example, consider the case where you want to get the return value of function `a`, then call function `b`, then call function `c` with the return value of function `a`. If there are a lot of moves, the register allocator can figure out a way to temporarily stash the value in a saved register instead of spilling to the stack.

Another thing to notice is that GOAL nested function calls suck. Example:
```
(format #t "The value of ~D factorial is ~D~%" x (factorial-iterative x))
```
requires loading `format`, `#t`, the string, and `x` into registers, then calling `(factorial-iterative x)`, then calling `format`. This has to be done this way, just in case the `factorial-iterative` call modifies the value of `format`, `#t`, or `x`.

## IR Pass Implementation
An important type in the compiler is `Val`, which is a specification on how to get a value.  A `Val` has an associated GOAL type (`TypeSpec`) and the IR Pass should take care of all type checking.  A `Val` can represent a constant, a memory location (relative to pointer, or static data, etc), a spot in an array, a register etc.  A `Val` representing a register is a `RegVal`, which contains an `IRegister`. An `IRegister` is a register that's not yet mapped to the hardware, and instead has a unique integer to identify itself. The IR assumes there are infinitely many `IRegister`s, and a later stage maps `IRegister`s to real hardware registers.

The general process starts with a `compile` function, which dispatches other `compile_<thing>` functions as needed.  These generally take in `goos::Object` as a code input, emit IR into an `Env`/or modify things in the `Env`, and return a `Val*` describing the result.  

In general, GOAL is very greedy and `compile` functions emit IR to do things, then put the result in a register, and return a `RegVal`. 

However, there is an exception for memory related things.  Consider
```
(-> my-object my-field) ;  my_object->my_field in C
```	
This shouldn't return a `Val` for a register containing the value of `my_object->my_field`, but should instead return something that represents "the memory location of my_field in my_object". This way you can do 
```
(set! (-> my-object my-field) val)
;; or
(& (-> my-object my-field)) ;; &my_object->my_field
```
and the compiler will have enough information to figure out the memory address. 

If the compiler actually needs the value of something, and wants to be sure its a value in a register, it will use the `to_reg` method of `Val`. This will emit IR into the current function to get the value in a register, then return a `Val` that represents this register.  Example `to_reg` implementation for integer constants:
```
RegVal* IntegerConstantVal::to_reg(Env* fe) {
  auto rv = fe->make_gpr(m_ts);
  fe->emit(std::make_unique<IR_LoadConstant64>(rv, m_value));
  return rv;
}
```


Note that `to_reg` can emit code like reading from memory, where the order of operations really matters, so you have to be very careful.

It's extremely dangerous to let a memory reference `Val` propagate too far. Consider this example:

```
(let ((x (-> my-object my-field)))
  (set! (-> my-object my-field) 12)
  x
  )
```
Where `x` should be the old value of `my-field`. The `Val` for `x` needs to be `to_reg`ed _before_ getting inside the `let`. There's also some potential confusion around the order that you compile and `to_gpr` things.  In a case where you need a bunch of values in gprs, you should do the `to_gpr` immediately after compiling to match the exact behavior of the original GOAL. For example

```
(+ (-> my-array (* x y)) (some-function))
;; like c++ my_array[x*y] + some_function()
```
When we `compile` the `(-> my-array (* x y))`, it will emit code to calculate the `(*x y)`, but won't actually do the memory access until we call `to_reg` on the result.  This memory access should happen __before__ `some-function` is called.


In general, each time you `compile` something, you should immediately `to_gpr` it, _before_ `compile`ing the next thing.  Many places will only accept a `RegVal` as an input to help with this.  Also, the result for almost all compilation functions should be `to_reg`ed. The only exceptions are forms which deal with memory references (address of operator, dereference operator) or math.

Another important thing is that compilation functions should _never_ modify any existing `IRegister`s or `Val`s, unless that function is `set!`, which handles the situation correctly. Instead, create new `IRegister`s and move into those. I am planning to implement a `settable` flag to help reduce errors. 

For example:
- `RegVal` storing a local variable: is `settable`, you can modify local variables by writing to the register they use.
- `RegVal` storing the result of a memory dereference: not `settable`, you should set the memory instead.
- `RegVal` containing the result of converting a `xmm` to `gpr`: not settable, you need to set the original `xmm` instead

The only settable `RegVal` is one corresponding to a local variable.

## Following the Code

This pass runs from here:
```
auto obj_file = compile_object_file(obj_file_name, code, !no_code);
```
That function sets up a `FileEnv*`, then runs
```
  file_env->add_top_level_function(
      compile_top_level_function("top-level", std::move(code), compilation_env));
```      
which compiles the body of the function with:
```
auto result = compile_error_guard(code, fe.get());
```

The `compile_error_guard` function takes in code (as a `goos::Object`) and a `Env*`, and returns a `Val` representing the return value of the code. It calls the `compile` function, but wraps it in a `try catch` block to catch any compilation errors and print an error message.  In the case where there's no error, it just does:
```
return compile(code, env);
```

The `compile` function is pretty simple:
```
/*!
 * Highest level compile function
 */
Val* Compiler::compile(const goos::Object& code, Env* env) {
  switch (code.type) {
    case goos::ObjectType::PAIR:
      return compile_pair(code, env);
    case goos::ObjectType::INTEGER:
      return compile_integer(code, env);
    case goos::ObjectType::SYMBOL:
      return compile_symbol(code, env);
    case goos::ObjectType::STRING:
      return compile_string(code, env);
    case goos::ObjectType::FLOAT:
      return compile_float(code, env);
    default:
      ice("Don't know how to compile " + code.print());
  }
  return get_none();
}
```
In our case, the code starts with `(defun..`, which is actually a GOOS macro. It throws away the docstring, creates a lambda, then stores the function in a symbol:
```
;; Define a new function
(defmacro defun (name bindings &rest body)
  (if (and
        (> (length body) 1)      ;; more than one thing in function
        (string? (first body))   ;; first thing is a string
        )
    ;; then it's a docstring and we ignore it.
    `(define ,name (lambda :name ,name ,bindings ,@(cdr body)))
    ;; otherwise don't ignore it.
    `(define ,name (lambda :name ,name ,bindings ,@body))
    )
  )
```

The compiler notices this is a macro in `compile_pair`:
```
  if (head.is_symbol()) {
  	// ...

    goos::Object macro_obj;
    if (try_getting_macro_from_goos(head, &macro_obj)) {
      return compile_goos_macro(code, macro_obj, rest, env);
    }

    // ...
  }

```
The `compile_goos_macro` function sets up a GOOS environment and interprets the GOOS macro to generate more GOAL code:
```
Val* Compiler::compile_goos_macro(const goos::Object& o,
                                  const goos::Object& macro_obj,
                                  const goos::Object& rest,
                                  Env* env) {
  auto macro = macro_obj.as_macro();
  Arguments args = m_goos.get_args(o, rest, macro->args);
  auto mac_env_obj = EnvironmentObject::make_new(); // GOOS environment
  auto mac_env = mac_env_obj.as_env();
  mac_env->parent_env = m_goos.global_environment.as_env();
  m_goos.set_args_in_env(o, args, macro->args, mac_env);
  auto goos_result = m_goos.eval_list_return_last(macro->body, macro->body, mac_env); // evaluate GOOS macro
  return compile_error_guard(goos_result, env); // compile resulting GOAL code
}
```
and the last line of that function compiles the result of macro expansion in GOAL. 

As an example, I'm going to look at `compile_add`, which handles the `+` form, and is representative of typical compiler code for this part.  We start by checking that the arguments look valid:
```
Val* Compiler::compile_add(const goos::Object& form, const goos::Object& rest, Env* env) {
  auto args = get_va(form, rest); // get arguments to + in a list
  if (!args.named.empty() || args.unnamed.empty()) {
    throw_compile_error(form, "Invalid + form");
  }
```

Then we compile the first thing in the `(+ ...` form, get its type, and pick a math mode (int, float):
```  
auto first_val = compile_error_guard(args.unnamed.at(0), env);
auto first_type = first_val->type();
auto math_type = get_math_mode(first_type);
```
In the integer case, we first create a new variable in the IR called an `IRegister` that must be in a GPR (as opposed to an XMM floating point register), and then emit an IR instruction that sets this result register to the first argument.
```
auto result = env->make_gpr(first_type);
env->emit(std::make_unique<IR_RegSet>(result, first_val->to_gpr(env)));
```

Then, for each of the remaining arguments, we do:
```
      for (size_t i = 1; i < args.unnamed.size(); i++) {
        env->emit(std::make_unique<IR_IntegerMath>(
            IntegerMathKind::ADD_64, result,
            to_math_type(compile_error_guard(args.unnamed.at(i), env), math_type, env)
                ->to_gpr(env)));
      }
```
which emits an IR to add the value to the sum.  The `to_math_type` will emit any code needed to convert this to the correct numeric type (returns either a numeric constant or a `RegVal` containing the value).

An important detail is that we create a new register which will hold the result. This may seem inefficient in cases, but a later compile pass will try to make this new register be the same register as `first_val` if possible, and will eliminate the `IR_RegSet`.

# Register Allocation

This step figures out how to match up `IRegister`s to real hardware registers. In the case where there aren't enough hardware registers, it figures out how to "spill" variables onto the stack. The current implementation is a very greedy one, so it doesn't always succeed at doing things perfectly. The stack spilling is also not handled very efficiently, but the hope is that most functions won't require stack spilling.

This step is run from `compile_asm_function` on the line:
```
color_object_file(obj_file);

void Compiler::color_object_file(FileEnv* env) {
  for (auto& f : env->functions()) {
    AllocationInput input;
    for (auto& i : f->code()) {
      input.instructions.push_back(i->to_rai());
      input.debug_instruction_names.push_back(i->print());
    }
    input.max_vars = f->max_vars();
    input.constraints = f->constraints();

    // debug prints removed

    f->set_allocations(allocate_registers(input));
  }
}
```
The actual algorithm is too complicated to describe here, but it figures out a mapping from `IRegister`s to hardware registers. It also figures out how much space on the stack is needed for any stack spills, which saved registers will be used, and deals with aligning the stack.


# Code Generation
This part actually generates the static data and x86 instructions and stores them in an `ObjectGenerator`. See `CodeGenerator::do_function`. It emits the function prologue and epilogue, as well as any extra loads/stores from the stack that the register allocator added.  Each `IR` gets to emit instructions with:
```
ir->do_codegen(&m_gen, allocs, i_rec);
```

Each IR has its own `do_codegen` that emits the right instruction, and also any linking data that's needed. For example, instructions that access the symbol table are patched by the runtime to directly access the correct slot of the hash table, so the `do_codegen` also lets the `ObjectGenerator` know about this link:
```
// IR_GetSymbolValue::do_codegen

// look at register allocation result to determine hw register
auto dst_reg = get_reg(m_dest, allocs, irec);

// add an instruction
auto instr = gen->add_instr(IGen::load32u_gpr64_gpr64_plus_gpr64_plus_s32(
                             dst_reg, gRegInfo.get_st_reg(), gRegInfo.get_offset_reg(), 0x0badbeef), irec);

// add link info
gen->link_instruction_symbol_mem(instr, m_src->name());
```
here `0xbadbeef` is used as a placeholder offset - the runtime should patch this to the actual offset of the symbol.

There's a ton of book-keeping to figure out the correct offsets for `rip`-relative addressing, or how to deal with jumps to/from IR which become multiple (or zero!) x86-64 instructions.  It should all be handled by `ObjectFileGenerator`, and not in `do_codegen` or `CodeGenerator`.

# Object File Generation
Once the `CodeGenerator` is done going through all functions and static data, it runs:
```
return m_gen.generate_data_v3().to_vector();
```
This actually lays out everything in memory. It takes a few passes because x86 instructions are variable length (may even change based on which registers are used!), so it's a little bit tricky to figure out offsets between different instructions or instructions and data. Finally it generates link data tables, which efficiently pack together links to the same symbols into a single entry, to avoid duplicated symbol names.  The link table also contains information about linking references in between different segments, as different parts of the object file may be loaded into different spots in memory, and will need to reference each other.

This is the final result for top-level function (stored in top-level segment)
```
;; prologue
push   rbx
push   rbp
push   r10
push   r11
push   rbx

;; load address of factorial-iterative function
lea    rax,[rip+0x0]
;; convert to GOAL pointer
sub    rax,r15
;; store in symbol table
mov    DWORD PTR [r15+r14*1+0xbadbeef],eax

;; load _format value
mov    eax,DWORD PTR [r15+r14*1+0xbadbeef]
;; store in format
mov    DWORD PTR [r15+r14*1+0xbadbeef],eax

;; load constant 10 (the greedy regalloc does poorly here)
mov    eax,0xa
mov    rbx,rax

;; load format from symbol table
mov    ebp,DWORD PTR [r15+r14*1+0xbadbeef]

;; load #t from symbol table
mov    r10d,DWORD PTR [r15+r14*1+0xbadbeef]

;; get address of string
lea    r11,[rip+0x0]
;; convert to GOAL pointer
sub    r11,r15

;; get factorial-iterative from symbol table
mov    ecx,DWORD PTR [r15+r14*1+0xbadbeef]
;; move 10 into argument register
mov    rdi,rbx
;; convert factorial-iterative to a real pointer
add    rcx,r15
;; call factorial
call   rcx

;; move args into argument registers
mov    rdi,r10
mov    rsi,r11
mov    rdx,rbx
mov    rcx,rax

;; call format
add    rbp,r15
call   rbp

;; epilogue
pop    rbx
pop    r11
pop    r10
pop    rbp
pop    rbx
ret
```
and the factorial function (stored in main segment)
```
mov    eax,0x1
jmp    0x18
mul   eax,edi
movsxd rax,eax
mov    ecx,0x1
sub    rdi,rcx
mov    ecx,0x1
cmp    rdi,rcx
jne    0xa
ret
```

# Sending and Receiving
The result of `codegen_object_file` is sent with:
```
m_listener.send_code(data);
```
which adds a message header then just sends the code over the socket.

The receive process is complicated, so this is just a quick summary
- `Deci2Server` receives it
- calls `GoalProtoHandler` in chunks, which stores it in a buffer (`MessBuffArea`)
- Once fully received, `WaitForMessageAndAck` will return a pointer to the message. The name of this function is totally wrong, it doesn't wait for a message, and it doesn't ack the message.
- The main loop in `KernelCheckAndDispatch` will see this message and run `ProcessListenerMessage`
- `ProcessListenerMessage` sees that it has code, copies the message to the debug heap, and links it.
```
auto buffer = kmalloc(kdebugheap, MessCount, 0, "listener-link-block");
memcpy(buffer.c(), msg.c(), MessCount);
ListenerLinkBlock->value = buffer.offset + 4;
ListenerFunction->value = link_and_exec(buffer, "*listener*", 0, kdebugheap, LINK_FLAG_FORCE_DEBUG).offset;
```
- The `link_and_exec` function doesn't actually execute anything because it doesn't have the `LINK_FLAG_EXECUTE` set, it just links things. It moves the top level function and linking data to the top of the heap (temporary storage for the kernel) and keep both the main segment and debug segment of the code on the debug heap.  It'll move them together and eliminate gaps before linking.  After linking, the `ListenerFunction->value` will contain a pointer to the top level function, which is stored in the top temp area of the heap.  This `ListenerFunction` is the GOAL `*listener-function*` symbol.
- The next time the GOAL kernel runs, it will notice that `*listener-function*` is set, then call this function, then set it to `#f` to indicate it called the function.
- This 
- After this, `ClearPending()` is called, which sends all of the `print` messages with the `Deci2Server` back to the compiler.
- Because the GOAL kernel changed `ListenerFunction` to `#f`, it does a `SendAck()` to send a special `ACK` message to the compiler, saying "I got the function, ran it, and didn't crash. Now I'm ready for more messages."
