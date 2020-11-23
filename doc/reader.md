# Reader
GOOS and GOAL both use the same reader, which converts text files to S-Expressions and allows these s-expressions to be mapped back to a line in a source file for error messages.  This document explains the syntax of the reader.  Note that these rules do not explain the syntax of the language (for instance, GOAL has a much more complicated system of integers and many more restrictions), but rather the rules of how your program source must look.

## Integer Input
Integers handled by the reader are 64-bits. Any overflow is considered an error.  An integer can be specified as a decimal, like `0` or `-12345`; in hex, like `#xbeef`; or in binary, like `#b101001`. All three representations can be used anywhere an integer is used. Hex numbers do not care about the case of the characters. Decimal numbers are signed, and wrapping from a large positive number to a negative number will generate an error.  The valid input range for decimals is `INT64_MIN` to `INT64_MAX`.  Hex and binary are unsigned and do not support negative signs, but allow large positive numbers to wrap to negative.  Their input range is `0` to `UINT64_MAX`.  For example, `-1` can be entered as `-1` or `#xffffffffffffffff`, but not as `UINT64_MAX` in decimal. 

## Floating Point Input
Floating point values handled by the reader are implemented with `double`. Weird numbers (denormals, NaN, infinity) are invalid and not handled by the reader directly.  A number _must_ have a decimal point to be interpreted as floating point. Otherwise, it will be an integer.  Leading/trailing zeros are optional.

## Character Input
Characters are used to represent characters that are part of text.  The character `c` is represented by `#\c`.  This representation is used for all ASCII characters between `!` and `~`.  There are three special characters which have a non-standard representation:
- Space : `#\\s`
- New Line: `#\\n`
- Tab: `#\\t`

All other characters are invalid.

## String
A string is a sequence of characters, surrounding by double quotes.  The ASCII characters from ` ` to `~` excluding `"` can be entered directly.  Strings have the following escape codes:
- `\\` : insert a backslash
- `\n` : insert a new line
- `\t` : insert a tab
- `\"` : insert a double quote


## Comments
The reader supports line comments with `;` and multi-line comments with `#| |#`. For example

```
(print "hi") ; prints hi

#|
this is a multi-line comment!
(print "hi") <- this is commented out.
|#
```

## Array
The reader supports arrays with the following syntax:
```
; array of 1, 2, 3, 4
#(1 2 3 4)
```

Arrays can be nested with lists, pairs, and other arrays.

## Pair
The reader supports pairs with the following syntax:
```
; pair of a, b
(a . b)
```
Pairs can be nested with lists, pairs, and arrays.

## List
The reader supports lists. Lists are just an easier way of constructing a linked list of pairs, terminated with the empty list.  The empty list is a special list written like `()`.

```
; list of 1, 2, 3
(1 2 3)
; actually the same as
(1 . (2 . (3 . ())))
```

## Symbol
A symbol is a sequence of characters containing no whitespace, and not matching any other data type. (Note: this is not a very good definition). Typically symbols are lower case, and words are separated by a `-`. Examples:
```
this-is-a-symbol
; you can have weird symbols too:
#f
#t
-
*
+
__WEIRDLY-NamedSymbol ; this is weird, but OK.
```

## Reader Macros
The reader has some default macros which are common in Scheme/LISP:
- `'x` will be replaced with `(quote x)`
- `` `x`` will be replaced with `(quasiquote x)`
- `,x` will be replaced with `(unquote x)`
- `,@` will be replaced with `(unquote-splicing x)`

