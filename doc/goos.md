Goos
-----
GOOS is a macro language for GOAL. It is a separate language.  Files written in GOAL end in `.gc`  and files written in GOOS end in `.gs`.  The REPL will display a `goos>` prompt for GOOS and `goal>` for GOAL.

There is a special namespace shared between GOOS and GOAL containing the names of the macros (written in GOOS) which can be used in GOAL code.

To access a GOOS REPL, run `(goos)` from the `goal>` prompt (note, currently this happens by default as GOAL is not implemented).  

This document assumes some familiarity with the Scheme programming language.  It's recommended to read a bit about Scheme first.

Note that most Scheme things will work in GOOS, with the following exceptions:
- Scheme supports fractions, GOOS does not (it has separate integer/floating point types)
- The short form for defining functions is `(desfun function-name (arguments) body...)`
- GOOS does not have tail call optimization and prefers looping to recursion (there is a `while` form)


Special Forms
---------------
Most forms in Scheme have a name, and list of arguments. Like:
```
(my-operation first-argument second-argument ...)
```
Usually, each argument is evaluated, then passed to the operation, and the resulting value is returned.  However, there are cases where not all arguments are evaluated.  For example:
```
(if (want-x?)
    (do-x)
    (do-y)
 )
```
In this case, only one of `(do-x)` and `(do-y)` are executed.  This doesn't follow the pattern of "evaluate all arguments...", so it is a *SPECIAL FORM*.  It's not possible for a function call to be a special form - GOOS will automatically evaluate all arguments.  It is possible to build macros which act like special forms.  There are some special forms built-in to the GOOS interpreter, which are documented in this section.

### define
This is used to define a value in the current lexical environment.
For example:
```
(define x 10)
```
will define `x` as a variable equal to `10` in the inner-most lexical environment. (Note, I'm not sure this is actually how Scheme works)

There is an optional keyword argument to pick the environment for definition, but this is used rarely.  The only named environments are:
- `*goal-env*`
- `*global-env*`

Example:
```
(define :env *global-env* x 10)
```
will define `x` in the global (outer-most) environment, regardless of where the `define` is written.


### quote
This form simply returns its argument without evaluating it.  There's a reader shortcut for this:
```
(quote x)

;; reader shortcut
'x ;; same as (quote x)
```

It's often used to get a symbol, but you can quote complex things like lists, pairs, and arrays.
```
goos> (cdr '(1 . 2))
2
goos> (cdr '(1 2))
(2)
goos> '#(1 2 3)
#(1 2 3)
```

### set!
Set is used to search for a variable in the enclosing environments, then set it to a value.
```
(set! x (+ 1 2))
```
will set the lexically closest `x` to 3.  It's an error if there's no variable named `x` in an enclosing scope.

### lambda
See any Lisp/Scheme tutorial for a good explanation of `lambda`.

Note that GOOS has some extensions for arguments.  You can have a "rest" argument at the end, like this:
```
(lambda (a b &rest c) ...) ;; c is the rest arg
(lambda (&rest a) ...) ;; a is the rest
```

The rest argument will contain a list of all extra arguments passed to the function.  If there are no extra arguments, the rest argument will be the empty list.

There are also keyword arguments, which contain a `&key` before the argument name.
```
(lambda (a b &key c) ...) ;; b is a keyword argument, a and c are not.
(lambda (&key a &key b) ...) ;; a and b are keyword arguments
```

These keyword arguments _must_ be specified by name. So to call the two functions above:
```
(f 1 2 :c 3) ;; a = 1, b = 2, c = 3
(f :a 1 : b 2) ;; a = 1, b = 2
```

Note that it is not required to put keyword arguments last, but it is a good idea to do it for clarity.

There are also keyword default arguments, which are like keyword arguments, but can be omitted from the call.  In this case a default value is used instead.
```
(lambda (&key (c 12)) ...)
(f :c 2) ;; c = 2
(f) ;; c = 12
```

The order of argument evaluation is:
- All "normal" arguments, in the order they appear
- All keyword/keyword default arguments, in alphabetical order
  - It is not recommended to rely on this
- All rest arguments, in the order they appear

### cond
Normal Scheme `cond`. If no cases matches and there is no `else`, returns `#f`.
Currently `else` isn't implement, just use `#t` instead for now.

### or
Short circuiting `or`. If nothing is truthy, `#f`. Otherwise returns first truthy.

### and
Short circuiting `or`. If not all truthy, `#f`. Otherwise returns last truthy.

### macro
Kind of like `lambda`, but for a macro.  

A lambda:
- Evaluate the arguments
- Evaluate the body

A macro:
- Don't evaluate the arguments
- Evaluate the body
- Evaluate that again

You can think about a `lambda` like a "normal" function, and a `macro` as a function that receive code as arguments (as opposed to values), and produces code as an output, which is then evaluated.

### quasiquote
See any Lisp/Scheme tutorial.  GOOS supports:

- `(quasiquote x)` or ``` `x ```
- `(unquote x)` or `,x`
- `(unquote-splicing x)` or `,@x`

### while
Special while loop form for GOOS.

`(while condition body...)`

To add together `[0, 100)`:
```
(define count 0)
(define sum 0)

(while (< count 100)
  (set! sum (+ sum count))
  (set! count (+ count 1))
  )

sum
```

Not Special Built-In Forms
---------------------------


Namespace Details
------------------
The GOOS `define` form accepts an environment for definition.  For example:
```
(define :env *goal-env* x 10)
```
will define `x` in the `*goal-env*`.  Any macros defined in the `*goal-env*` can be used as macros from within GOAL code.
Things that aren't macros in the `*goal-env*` cannot be accessed.

