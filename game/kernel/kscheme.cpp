/*!
 * @file kscheme.cpp
 * Implementation of GOAL runtime.
 */

#include <cstring>
#include <cassert>
#include "kscheme.h"
#include "common/common_types.h"
#include "kmachine.h"
#include "klisten.h"
#include "kmalloc.h"
#include "kprint.h"
#include "fileio.h"
#include "kboot.h"
#include "kdsnetm.h"
#include "kdgo.h"
#include "klink.h"
#include "common/symbols.h"
#include "common/versions.h"
#include "common/goal_constants.h"

//! Controls link mode when EnableMethodSet = 0, MasterDebug = 1, DiskBoot = 0. Will enable a
//! warning message if EnableMethodSet = 1
u32 FastLink;

// where to put a new symbol for the most recently searched for symbol that wasn't found
u32 symbol_slot;

// pointer to the "second" symbol table
Ptr<u32> SymbolTable2;

// pointer to the last symbol
Ptr<u32> LastSymbol;

// total number of symbols in the table
s32 NumSymbols;

// set to true to enable propagating method overrides to child types
// this is an O(N_max_symbols) operation, so it is avoided when loading DGOs for levels.
// but is enabled when loading the engine.
Ptr<u32> EnableMethodSet;

// used for crc32 calculation
u32 crc_table[0x100];

// value of the GOAL s7 register, pointing to the middle of the symbol table
Ptr<u32> s7;

void kscheme_init_globals() {
  for (auto& x : crc_table) {
    x = 0;
  }
  NumSymbols = 0;
  s7.offset = 0;
  SymbolTable2.offset = 0;
  LastSymbol.offset = 0;
  EnableMethodSet.offset = 0;
  FastLink = 0;
}

/*!
 * Initialize CRC Table.
 */
void init_crc() {
  for (u32 i = 0; i < 0x100; i++) {
    u32 n = i << 24;
    for (u32 j = 0; j < 8; j++) {
      n = n & 0x80000000 ? (n << 1) ^ CRC_POLY : (n << 1);
    }
    crc_table[i] = n;
  }
}

/*!
 * Take the CRC32 hash of some data
 */
u32 crc32(const u8* data, s32 size) {
  uint32_t crc = 0;
  for (int i = size; i != 0; i--, data++) {
    crc = crc_table[crc >> 24] ^ ((crc << 8) | *data);
  }

  if ((~crc) == 0) {
    // if this happens, I think the hash table implementation breaks.
    assert(false);
  }
  return ~crc;
}

/*!
 * New method for types which cannot have "new" used on them.
 * Prints an error to stdout and returns false.
 */
u64 new_illegal(u32 allocation, u32 type) {
  (void)allocation;
  MsgErr("dkernel: illegal attempt to call new method of static object type %s\n",
         info(Ptr<Type>(type)->symbol)->str->data());
  return s7.offset;
}

/*!
 * Delete method for types which cannot have "delete" used on them.
 * Prints an error to stdout and returns false.
 */
u64 delete_illegal(u32 obj) {
  MsgErr("dkernel: illegal attempt to call delete of static object @ #x%x\n", obj);
  return s7.offset;  // todo, maybe don't return anything?
}

/*!
 * Wrapper around kmalloc to allow GOAL programs to allocate on kernel heaps.
 */
u64 goal_malloc(u32 heap, u32 size, u32 flags, u32 name) {
  return kmalloc(Ptr<kheapinfo>(heap), size, flags, Ptr<String>(name)->data()).offset;
}

/*!
 * Allocate memory from the specified heap. If symbol is 'process, does a process allocation.
 * If symbol is 'scratch, does a scratch allocation (this is not used).
 *
 * If it's not a heap, treat it as a stack allocation and just memset it to zero.
 * Type is only used to print a debug message if the allocation fails, so it can be null or not
 * completely defined.
 */
u64 alloc_from_heap(u32 heapSymbol, u32 type, s32 size) {
  if (size <= 0) {
    throw std::runtime_error("got <= 0 size allocation in alloc_from_heap!");
  }

  // align to 16 bytes (part one)
  s32 alignedSize = size + 0xf;

  // huh?
  if (alignedSize < 0)
    alignedSize += 0xf;

  // finish aligning.
  alignedSize = (alignedSize >> 4) << 4;

  u32 heapOffset = heapSymbol - s7.offset;

  if (heapOffset == FIX_SYM_GLOBAL_HEAP || heapOffset == FIX_SYM_DEBUG_HEAP ||
      heapOffset == FIX_SYM_PROCESS_LEVEL_HEAP || heapOffset == FIX_SYM_LOADING_LEVEL) {
    // it's a kheap, so just kmalloc.

    if (!type) {  // no type given, just call it a global-object
      return kmalloc(*Ptr<Ptr<kheapinfo>>(heapSymbol), size, KMALLOC_MEMSET, "global-object")
          .offset;
    }

    Ptr<Type> typ(type);
    if (!typ->symbol.offset) {  // type doesn't have a symbol, just call it a global-object
      return kmalloc(*Ptr<Ptr<kheapinfo>>(heapSymbol), size, KMALLOC_MEMSET, "global-object")
          .offset;
    }

    Ptr<String> gstr = info(typ->symbol)->str;
    if (!gstr->len) {  // string has nothing in it.
      return kmalloc(*Ptr<Ptr<kheapinfo>>(heapSymbol), size, KMALLOC_MEMSET, "global-object")
          .offset;
    }

    return kmalloc(*Ptr<Ptr<kheapinfo>>(heapSymbol), size, KMALLOC_MEMSET, gstr->data()).offset;
  } else if (heapOffset == FIX_SYM_PROCESS_TYPE) {
    throw std::runtime_error("this type of process allocation is not supported yet!\n");
    // allocate on current process heap
    //    Ptr start = *ptr<Ptr>(getS6() + 0x4c + 8);
    //    Ptr heapEnd = *ptr<Ptr>(getS6() + 0x4c + 4);
    //    Ptr allocEnd = start + alignedSize;
    //
    //    // there's room, bump allocate
    //    if(allocEnd < heapEnd) {
    //      *ptr<Ptr>(getS6() + 0x4c + 8) = allocEnd;
    //      memset(vptr(start), 0, (size_t)alignedSize);
    //      return start;
    //    } else {
    //      MsgErr("kmalloc: !alloc mem in heap for #<process @ #x%x> (%d bytes)\n", getS6(),
    //      alignedSize); return 0;
    //    }
  } else if (heapOffset == FIX_SYM_SCRATCH) {
    throw std::runtime_error("this type of scratchpad allocation is not used!\n");
  } else {
    memset(Ptr<u8>(heapSymbol).c(), 0, (size_t)alignedSize);  // treat it as a stack address
    return heapSymbol;
  }
}

/*!
 * Allocate untyped memory.
 */
u64 alloc_heap_memory(u32 heap, u32 size) {
  return alloc_from_heap(heap, 0, size);
}

/*!
 * Allocate memory and add type tag for an object.
 * For allocating basics.
 */
u64 alloc_heap_object(u32 heap, u32 type, u32 size) {
  auto mem = alloc_from_heap(heap, type, size);
  if (!mem) {
    return 0;
  }

  *Ptr<u32>(mem) = type;
  return mem + BASIC_OFFSET;
}

/*!
 * Allocate a structure and get the structure size from the type.
 */
u64 new_structure(u32 heap, u32 type) {
  return alloc_from_heap(heap, type, Ptr<Type>(type)->allocated_size);
}

/*!
 * Allocate a structure with a dynamic size
 */
u64 new_dynamic_structure(u32 heap, u32 type, u32 size) {
  return alloc_from_heap(heap, type, size);
}

/*!
 * Delete a structure.  Not supported, as it uses kfree, which doesn't do anything.
 */
void delete_structure(u32 s) {
  kfree(Ptr<u8>(s));
}

/*!
 * Allocate a basic of fixed size.
 */
u64 new_basic(u32 heap, u32 type) {
  return alloc_heap_object(heap, type, Ptr<Type>(type)->allocated_size);
}

/*!
 * Delete a basic.  Not supported, as it uses kfree.
 */
void delete_basic(u32 s) {
  // note that the game has a bug here and has s as a uint* and does -4 which is actually a
  // 16-byte offset. Luckily kfree does nothing so there's no harm done.  But it's a good indication
  // that the "freeing memory" feature never made it very far in development. This bug exists in
  // Jak 3 as well.
  kfree(Ptr<u8>(s - BASIC_OFFSET * 4));  // replicate the bug
}

/*!
 * Allocate a new pair and set its car and cdr.
 */
u64 new_pair(u32 heap, u32 type, u32 car, u32 cdr) {
  auto mem = alloc_from_heap(heap, type, Ptr<Type>(type)->allocated_size);
  if (!mem) {
    return 0;
  }

  u32* m = Ptr<u32>(mem).c();
  m[0] = car;
  m[1] = cdr;
  return mem + PAIR_OFFSET;
}

/*!
 * Delete a pair.  BUG
 */
void delete_pair(u32 s) {
  // the -8 should be a -2, but s is likely a u32* in the code.
  kfree(Ptr<u8>(s - 8));
}

/*!
 * Make an empty string of given size.
 * Allocates from the global heap.
 */
u64 make_string(u32 size) {
  auto mem_size = size + 1;  // null
  if (mem_size < 8) {
    mem_size = 8;  // min size of string
  }

  // total size is mem_size (chars + null term), plus basic_offset (type tag) + 4 (string size)
  auto mem = alloc_heap_object((s7 + FIX_SYM_GLOBAL_HEAP).offset, *(s7 + FIX_SYM_STRING_TYPE),
                               mem_size + BASIC_OFFSET + sizeof(uint32_t));

  // set the string size field.
  if (mem) {
    *Ptr<u32>(mem) = size;
  }
  return mem;
}

/*!
 * Convert a C string to a GOAL string.
 * Allocates from the global heap and copies the string data.
 */
u64 make_string_from_c(const char* c_str) {
  auto str_size = strlen(c_str);
  auto mem_size = str_size + 1;
  if (mem_size < 8) {
    mem_size = 8;
  }

  auto mem = alloc_heap_object((s7 + FIX_SYM_GLOBAL_HEAP).offset, *(s7 + FIX_SYM_STRING_TYPE),
                               mem_size + BASIC_OFFSET + 4);
  // there's no check for failed allocation here!

  // string size field
  *Ptr<u32>(mem) = str_size;

  // rest is chars
  kstrcpy(Ptr<char>(mem + 4).c(), c_str);
  return mem;
}

/*!
 * Create a GOAL function from a C function. This doesn't export it as a global function, it just
 * creates a function object on the global heap.
 *
 * The implementation is to create a simple trampoline function which jumps to the C function.
 *
 *
 */
Ptr<Function> make_function_from_c(void* func) {
  // allocate a function object on the global heap
  auto mem = Ptr<u8>(
      alloc_heap_object(s7.offset + FIX_SYM_GLOBAL_HEAP, *(s7 + FIX_SYM_FUNCTION_TYPE), 0x80));
  auto f = (uint64_t)func;
  auto fp = (u8*)&f;

  int i = 0;
  // we will put the function address in RAX with a movabs rax, imm8
  mem.c()[i++] = 0x48;
  mem.c()[i++] = 0xb8;
  for (int j = 0; j < 8; j++) {
    mem.c()[i++] = fp[j];
  }

  /*
   * push rdi
   * push rsi
   * push rdx
   * push rcx
   * pop r9
   * pop r8
   * pop rdx
   * pop rcx
   *
   * sub rsp, 40
   * call rax
   * add rsp, 40
   * ret
   */
  for (auto x : {0x57, 0x56, 0x52, 0x51, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x48,
                 0x83, 0xEC, 0x28, 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0xC3}) {
    mem.c()[i++] = x;
  }

  // jmp rax
  //  mem.c()[10] = 0xff;
  //  mem.c()[11] = 0xe0;

  // the C function's ret will return to the caller of this trampoline.

  // CacheFlush(mem, 0x34);

  return mem.cast<Function>();
}

/*!
 * Create a GOAL function which does nothing and immediately returns.
 */
Ptr<Function> make_nothing_func() {
  auto mem = Ptr<u8>(
      alloc_heap_object(s7.offset + FIX_SYM_GLOBAL_HEAP, *(s7 + FIX_SYM_FUNCTION_TYPE), 0x14));

  // a single x86-64 ret.
  mem.c()[0] = 0xc3;
  // CacheFlush(mem, 8);
  return mem.cast<Function>();
}

/*!
 * Create a GOAL function which returns 0.
 */
Ptr<Function> make_zero_func() {
  auto mem = Ptr<u8>(
      alloc_heap_object(s7.offset + FIX_SYM_GLOBAL_HEAP, *(s7 + FIX_SYM_FUNCTION_TYPE), 0x14));
  // xor eax, eax
  mem.c()[0] = 0x31;
  mem.c()[1] = 0xc0;
  // ret
  mem.c()[2] = 0xc3;
  // CacheFlush(mem, 8);
  return mem.cast<Function>();
}

/*!
 * Given a C function and a name, create a GOAL function and store it in the symbol with the given
 * name. This effectively creates a global GOAL function with the given name which calls the given C
 * function.
 */
Ptr<Function> make_function_symbol_from_c(const char* name, void* f) {
  auto sym = intern_from_c(name);
  auto func = make_function_from_c(f);
  sym->value = func.offset;
  return func;
}

/*!
 * Set the named symbol to the value. This isn't specific to functions.
 */
u32 make_raw_function_symbol_from_c(const char* name, u32 value) {
  intern_from_c(name)->value = value;
  return value;
}

/*!
 * Configure a "fixed" symbol to have a given name and value.  The "fixed" symbols are symbols
 * which have their location in the symbol table determined ahead of time and not looked up by the
 * hash function.
 */
Ptr<Symbol> set_fixed_symbol(u32 offset, const char* name, u32 value) {
  // grab the symbol directly
  Ptr<Symbol> sym = (s7 + offset).cast<Symbol>();

  // grab the symbol type directly (it might not be set up yet, but that's ok)
  Ptr<Type> typ = *Ptr<Ptr<Type>>(s7.offset + FIX_SYM_SYMBOL_TYPE);

  // set type tag of the symbol.
  sym.cast<u32>().c()[-1] = typ.offset;

  // set name of the symbol
  info(sym)->str = Ptr<String>(make_string_from_c(name));

  // set hash of the symbol
  info(sym)->hash = crc32((const u8*)name, (int)strlen(name));

  // set value of the symbol
  sym->value = value;

  NumSymbols++;
  return sym;
}

/*!
 * Search the hash table's fixed area for a symbol.
 * Returns null if we didn't find it.
 */
Ptr<Symbol> find_symbol_in_fixed_area(u32 hash, const char* name) {
  for (u32 i = s7.offset; i < s7.offset + FIX_FIXED_SYM_END_OFFSET; i += 8) {
    auto sym = Ptr<Symbol>(i);
    if (info(sym)->hash == hash) {
      if (!strcmp(info(sym)->str->data(), name)) {
        return sym;
      }
    }
  }
  return Ptr<Symbol>(0);
}

/*!
 * Do a linear probe from start to end to find a symbol (or a slot for a new symbol).
 * If we run into the end without finding anything, returns 1 to indicate the linear probe needs to
 * wrap around. If we run into a blank space, mark that as the slot. Also search the fixed area for
 * the symbol. If we fail to find it without wrapping, and it's not in the fixed area, return 0.
 */
Ptr<Symbol> find_symbol_in_area(u32 hash, const char* name, u32 start, u32 end) {
  for (u32 i = start; i < end; i += 8) {
    auto sym = Ptr<Symbol>(i);

    // note - this may break if any symbols hash to zero!
    if (info(sym)->hash == hash) {
      if (!strcmp(info(sym)->str->data(), name)) {
        return sym;
      }
    }

    if (!info(sym)->hash) {
      // open slot!
      // means we don't need to wrap.
      symbol_slot = i;

      // check the fixed area, in case it's not dynamically placed.
      return find_symbol_in_fixed_area(hash, name);
    }
  }

  // we got to the end without finding the symbol or an empty slot.  Return 1 to indicate this.
  return Ptr<Symbol>(1);
}

/*!
 * Searches the table for a symbol.  If the symbol is found, returns it.
 * If not, returns 0, but symbol_slot will contain the slot for the symbol.
 * If both are 0, the symbol table is full and you are sad.
 * Also allows you to find the empty pair by searching for _empty_
 */
Ptr<Symbol> find_symbol_from_c(const char* name) {
  symbol_slot = 0;  // nowhere to put the symbol yet, clear any old symbol_slot result.
  u32 hash = crc32((const u8*)name, (int)strlen(name));

  // check if we've got the empty pair.
  if (hash == EMPTY_HASH) {
    if (!strcmp(name, "_empty_")) {
      return (s7 + FIX_SYM_EMPTY_PAIR).cast<Symbol>();
    }
  }

  s32 sh1 = hash << 0x13;
  s32 sh2 = sh1 >> 0x10;
  // will be signed, bottom 3 bits 0 (for alignment, symbol are every 8 bytes)
  // upper 16 bits are the same, so we will reach +/- 8 kb around 0.

  if (sh2 > 0) {
    // upper table first.
    auto probe = find_symbol_in_area(hash, name, s7.offset + sh2, LastSymbol.offset);
    if (probe.offset != 1) {
      return probe;
    }

    // overflow!
    probe = find_symbol_in_area(hash, name, SymbolTable2.offset, s7.offset - 0x10);
    if (probe.offset == 1) {
      // uh oh, both overflowed!
      printf("[BIG WARNING] symbol table probe double overflow!\n");
      return find_symbol_in_fixed_area(hash, name);
    } else {
      return probe;
    }

  } else {
    // lower table first
    auto probe = find_symbol_in_area(hash, name, s7.offset + sh2, s7.offset - 0x10);
    if (probe.offset != 1) {
      return probe;
    }

    // overflow!
    probe =
        find_symbol_in_area(hash, name, s7.offset + FIX_FIXED_SYM_END_OFFSET, LastSymbol.offset);
    if (probe.offset == 1) {
      printf("[BIG WARNING] symbol table probe double overflow!\n");
      return find_symbol_in_fixed_area(hash, name);
    } else {
      return probe;
    }
  }
}

/*!
 * Returns a symbol with the given name.  If this is the first time, make a new symbol, otherwise it
 * returns the old one. Basically a LISP symbol intern
 */
Ptr<Symbol> intern_from_c(const char* name) {
  auto symbol = find_symbol_from_c(name);
  if (symbol.offset) {
    // already exists, return it!
    return symbol;
  }

  // otherwise, a new symbol!
  symbol = Ptr<Symbol>(symbol_slot);
  // set type tag
  symbol.cast<u32>().c()[-1] = *(s7 + FIX_SYM_SYMBOL_TYPE);

  u32 hash = crc32((const u8*)name, (int)strlen(name));
  auto str = make_string_from_c(name);
  info(symbol)->str = Ptr<String>(str);
  info(symbol)->hash = hash;

  NumSymbols++;
  return symbol;
}

/*!
 * GOAL intern function.
 */
u64 intern(u32 name) {
  return intern_from_c(Ptr<String>(name)->data()).offset;
}

namespace {
u32 size_of_type(u32 method_count) {
  return (4 * method_count + 0x23) & 0xfffffff0;
}
}  // namespace

/*!
 * Given a symbol for the type name, allocate memory for a type and add it to the symbol table.
 */
Ptr<Type> alloc_and_init_type(Ptr<Symbol> sym, u32 method_count) {
  // allocate from the global heap
  u32 new_type = alloc_heap_object((s7 + FIX_SYM_GLOBAL_HEAP).offset, *(s7 + FIX_SYM_TYPE_TYPE),
                                   size_of_type(method_count));

  // add to symbol table.
  sym->value = new_type;
  return Ptr<Type>(new_type);
}

/*!
 * Like intern, but returns a type instead of a symbol. If the type doesn't exist, a new one is
 * allocated.
 */
Ptr<Type> intern_type_from_c(const char* name, u64 methods) {
  // there's a weird flag system used here.
  // if methods is a number that's not 0 or 1, its used as the desired number of methods.
  // If method is 0, and a new type needs to be created, it uses 12 methods
  // If method is 1, and a new type needs to be created, it uses 44 methods
  // If method is 0 or 1 and no new type needs to be created, there is no error.
  // Requesting a type to have fewer methods than the existing type has is ok.
  // Requesting a type to have more methods than the existing type is not ok and prints an error.

  auto symbol = intern_from_c(name);
  u32 sym_value = symbol->value;

  if (!sym_value) {
    // new type
    int n_methods = methods;

    if (methods == 0) {
      // some stupid types like user-defined children of integers have "0" as the method count
      n_methods = DEFAULT_METHOD_COUNT;
    } else if (methods == 1) {
      // whatever builds the v2/v4 object files (level data) doesn't actually know method counts.
      // so it just puts a 1.  In this case, we should put lots of methods, just in case.
      // I guess 44 was the number they picked.
      n_methods = FALLBACK_UNKNOWN_METHOD_COUNT;
    }

    // create the type.
    auto type = alloc_and_init_type(symbol, n_methods);
    type->symbol = symbol;
    type->num_methods = n_methods;
    return type;
  } else {
    // the type exists.
    auto type = Ptr<Type>(sym_value);
    // note - flags of 0 or 1 will pass through here without triggering the error.
    if (size_of_type(type->num_methods) < size_of_type(methods)) {
      MsgErr(
          "dkernel: trying to redefine a type '%s' with %d methods when it had %d, try "
          "restarting\n",
          name, (u32)methods, type->num_methods);
      assert(false);
    }
    return type;
  }
}

/*!
 * Wrapper of intern_type_from_c to use with GOAL. It accepts a gstring as a name.
 */
u64 intern_type(u32 name, u64 methods) {
  return intern_type_from_c(Ptr<String>(name)->data(), methods).offset;
}

/*!
 * Setup a type which is located in a fixed spot of the symbol table.
 */
Ptr<Type> set_fixed_type(u32 offset,
                         const char* name,
                         Ptr<Symbol> parent_symbol,
                         u64 flags,
                         u32 print,
                         u32 inspect) {
  Ptr<Symbol> type_symbol = (s7 + offset).cast<Symbol>();
  u32 symbol_value = type_symbol->value;

  // set type tag, name, hash of symbol
  type_symbol.cast<u32>().c()[-1] = *(s7 + FIX_SYM_SYMBOL_TYPE);
  info(type_symbol)->str = Ptr<String>(make_string_from_c(name));
  info(type_symbol)->hash = crc32((const u8*)name, (int)strlen(name));

  // increment
  NumSymbols++;

  // construct type if needed
  Ptr<Type> new_type;
  if (!symbol_value) {
    new_type = alloc_and_init_type(type_symbol, (u32)((flags >> 32) & 0xffff));
  } else {
    new_type.offset = symbol_value;
  }

  // set the type of the type
  new_type.cast<u32>().c()[-1] = *(s7 + FIX_SYM_TYPE_TYPE);

  // set type fields
  new_type->symbol = type_symbol;
  Ptr<Type> parent_type(parent_symbol->value);

  set_type_values(new_type, parent_type, flags);

  // inherit methods
  new_type->new_method = parent_type->new_method;
  new_type->delete_method = parent_type->delete_method;

  if (!print) {
    new_type->print_method = parent_type->print_method;
  } else {
    new_type->print_method.offset = print;
  }

  if (!inspect) {
    new_type->inspect_method = parent_type->inspect_method;
  } else {
    new_type->inspect_method.offset = inspect;
  }

  new_type->length_method.offset = *(s7 + FIX_SYM_ZERO_FUNC);
  new_type->asize_of_method = parent_type->asize_of_method;
  new_type->copy_method = parent_type->copy_method;

  return new_type;
}

/*!
 * New method of type. A GOAL (deftype) will end up calling this method.
 * Internally does an intern.
 */
u64 new_type(u32 symbol, u32 parent, u64 flags) {
  //  printf("flags 0x%lx\n", flags);
  u32 n_methods = (flags >> 32) & 0xffff;
  if (n_methods == 0) {
    // 12 methods used as default, if the user has not provided us with a number
    n_methods = DEFAULT_METHOD_COUNT;
  }

  assert(n_methods < 127);  // will cause issues.

  auto new_type = Ptr<Type>(intern_type(info(Ptr<Symbol>(symbol))->str.offset, n_methods));

  Ptr<Function>* child_slots = &(new_type->new_method);
  Ptr<Function>* parent_slots = &(Ptr<Type>(parent)->new_method);

  //  if (Ptr<Type>(parent)->num_methods < n_methods) {
  //    printf("%s %d %d\n", info(Ptr<Symbol>(symbol))->str.c()->data(),
  //    Ptr<Type>(parent)->num_methods,
  //           n_methods);
  //    assert(false);
  //  }

  // BUG! This uses the child method count, but should probably use the parent method count.
  for (u32 i = 0; i < n_methods; i++) {
    child_slots[i] = parent_slots[i];
  }

  return set_type_values(new_type, Ptr<Type>(parent), flags).offset;
}

/*!
 * Configure a type.
 */
Ptr<Type> set_type_values(Ptr<Type> type, Ptr<Type> parent, u64 flags) {
  type->parent = parent;
  type->allocated_size = (flags & 0xffff);
  type->heap_base = (flags >> 16) & 0xffff;
  type->padded_size = ((type->allocated_size + 0xf) & 0xfff0);

  u16 new_methods = (flags >> 32) & 0xffff;
  if (type->num_methods < new_methods) {
    type->num_methods = new_methods;
  }

  return type;
}

/*!
 * Is t1 a t2?
 */
u64 type_typep(Ptr<Type> t1, Ptr<Type> t2) {
  if (t1 == t2) {
    return (s7 + FIX_SYM_TRUE).offset;
  }

  do {
    t1 = t1->parent;
    if (t1 == t2) {
      return (s7 + FIX_SYM_TRUE).offset;
    }
  } while (t1.offset && t1.offset != *(s7 + FIX_SYM_OBJECT_TYPE));
  return s7.offset;
}

/*!
 * Set method of type.
 * Looks at the EnableMethodSet symbol to determine if it should loop through all types looking for
 * children and updating those.  Only updates children who haven't overridden the method previously.
 *
 * Even if EnableMethodSet is disabled, it will still do this loop if FastLink is disabled,
 * MasterDebug is enabled, or DiskBoot is false.  This is likely for debugging reasons?
 *
 * If method is 0, does nothing.
 * If method is 1, sets method of type to zero.
 * If method is 2, sets method of type to parent's method
 * GOAL args:
 * arg0 : type
 * arg1 : methodID
 * arg2 : method
 * Return is method
 */
u64 method_set(u32 type_, u32 method_id, u32 method) {
  printf("Call to method set\n");
  Ptr<Type> type(type_);
  if (method_id > 127)
    printf("[METHOD SET ERROR] tried to set method %d\n", method_id);

  //  printf("METHOD SET id %d to 0x%x type 0x%x!\n", method_id, method, type_);

  auto existing_method = type->get_method(method_id).offset;

  if (method == 1) {
    method = 0;
    printf("[Method Set] got 1, setting null\n");
  } else if (method == 0) {
    // no print, this happens a lot in non-debug mode.
    return 0;
  } else if (method == 2) {
    method = type->parent->get_method(method_id).offset;
    printf("[Method Set] got 2, inheriting\n");
  }

  // do the set
  type->get_method(method_id).offset = method;

  // this is kind of a strange combination...
  if (*EnableMethodSet || (!FastLink && MasterDebug && !DiskBoot)) {
    // upper table
    auto sym = s7.offset;
    for (; sym < LastSymbol.offset; sym += 8) {
      auto symValue = *Ptr<u32>(sym);
      if ((symValue < SymbolTable2.offset || 0x7ffffff < symValue) &&  // not in normal memory
          (symValue < 0x84000 || 0x100000 <= symValue)) {              // not in kernel memory
        continue;
      }

      if ((symValue & OFFSET_MASK) != BASIC_OFFSET) {
        continue;
      }

      auto objType = *Ptr<Ptr<Type>>(symValue - 4);
      if (objType.offset != *(s7 + FIX_SYM_TYPE_TYPE)) {
        continue;
      }

      auto symAsType = Ptr<Type>(symValue);
      if (method_id >= symAsType->num_methods) {
        continue;
      }

      if (symAsType->get_method(method_id).offset != existing_method) {
        continue;
      }

      if (type_typep(symAsType, type) == s7.offset) {
        continue;
      }

      if (FastLink) {
        // you were saved by EnableMethodSet.  I guess we warn.
        printf("************ WARNING **************\n");
        printf("method %d of %s redefined - you must define class heirarchies in order now\n",
               method_id, info(symAsType->symbol)->str->data());
        printf("***********************************\n");
      }

      symAsType->get_method(method_id).offset = method;
    }

    sym = SymbolTable2.offset;
    for (; sym < s7.offset; sym += 8) {
      auto symValue = *Ptr<u32>(sym);
      if ((symValue < SymbolTable2.offset || 0x7ffffff < symValue) &&  // not in normal memory
          (symValue < 0x84000 || 0x100000 <= symValue)) {              // not in kernel memory
        continue;
      }

      if ((symValue & OFFSET_MASK) != BASIC_OFFSET) {
        continue;
      }

      auto objType = *Ptr<Ptr<Type>>(symValue - 4);
      if (objType.offset != *(s7 + FIX_SYM_TYPE_TYPE)) {
        continue;
      }

      auto symAsType = Ptr<Type>(symValue);
      if (method_id >= symAsType->num_methods) {
        continue;
      }

      if (symAsType->get_method(method_id).offset != existing_method) {
        continue;
      }

      if (type_typep(symAsType, type) == s7.offset) {
        continue;
      }

      if (FastLink) {
        // you were saved by EnableMethodSet.  I guess we warn.
        printf("************ WARNING **************\n");
        printf("method %d of %s redefined - you must define class heirarchies in order now\n",
               method_id, info(symAsType->symbol)->str->data());
        printf("***********************************\n");
      }

      symAsType->get_method(method_id).offset = method;
    }
  }
  return method;
}

extern "C" {
// defined in asm_funcs.nasm
uint64_t _call_goal_asm(u64 a0, u64 a1, u64 a2, void* fptr, void* st_ptr, void* offset);
}

/*!
 * Wrapper around _call_goal_asm for calling a GOAL function from C.
 */
u64 call_goal(Ptr<Function> f, u64 a, u64 b, u64 c, u64 st, void* offset) {
  auto st_ptr = (void*)((uint8_t*)(offset) + st);
  void* fptr = f.c();
  return _call_goal_asm(a, b, c, fptr, st_ptr, offset);
}

/*!
 * Call a GOAL method of a given type.
 */
u64 call_method_of_type(u32 arg, Ptr<Type> type, u32 method_id) {
  if (((type.offset < SymbolTable2.offset || 0x7ffffff < type.offset) &&  // not in normal memory
       (type.offset < 0x84000 || 0x100000 <= type.offset))                // not in kernel memory
      || ((type.offset & OFFSET_MASK) != BASIC_OFFSET)) {                 // invalid type
    cprintf("#<#%x has invalid type ptr #x%x>\n", arg, type.offset);
  } else {
    auto type_tag = Ptr<Ptr<Type>>(type.offset - 4);
    if ((*type_tag).offset == *(s7 + FIX_SYM_TYPE_TYPE)) {
      auto f = type->get_method(method_id);
      return call_goal(f, arg, 0, 0, s7.offset, g_ee_main_mem);
    } else {
      cprintf("#<#x%x has invalid type ptr #x%x, bad type #x%x>\n", arg, type.offset,
              (*type_tag).offset);
    }
  }
  // throw std::runtime_error("call_method_of_type failed!\n");
  printf("[ERROR] call_method_of_type failed!\n");
  printf("type is %s\n", info(type->symbol)->str->data());
  return arg;
}

/*!
 * Call a GOAL function with no arguments.
 */
u64 call_goal_function(Ptr<Function> func) {
  return call_goal(func, 0, 0, 0, s7.offset, g_ee_main_mem);
}

/*!
 * Call a global GOAL function by name.
 */
u64 call_goal_function_by_name(const char* name) {
  return call_goal_function(Ptr<Function>(*(intern_from_c(name)).cast<u32>()));
}

/*!
 * Like call_method_of_type, but has two arguments. Used to "relocate" v2/s4 loads.
 */
u64 call_method_of_type_arg2(u32 arg, Ptr<Type> type, u32 method_id, u32 a1, u32 a2) {
  if (((type.offset < SymbolTable2.offset || 0x7ffffff < type.offset) &&  // not in normal memory
       (type.offset < 0x84000 || 0x100000 <= type.offset))                // not in kernel memory
      || ((type.offset & OFFSET_MASK) != BASIC_OFFSET)) {                 // invalid type
    cprintf("#<#%x has invalid type ptr #x%x>\n", arg, type.offset);
  } else {
    auto type_tag = Ptr<Ptr<Type>>(type.offset - 4);
    if ((*type_tag).offset == *(s7 + FIX_SYM_TYPE_TYPE)) {
      // return type->get_method(method_id).cast<u64 (u32,u32,u32)>().c()(arg,a1,a2);
      return call_goal(type->get_method(method_id), arg, a1, a2, s7.offset, g_ee_main_mem);
    } else {
      cprintf("#<#x%x has invalid type ptr #x%x, bad type #x%x>\n", arg, type.offset,
              (*type_tag).offset);
    }
  }
  throw std::runtime_error("call_method_of_type failed!\n");
  return arg;
}

/*!
 * Print an object with a newline after it to the GOAL PrintBuffer (not stdout)
 */
u64 sprint(u32 obj) {
  auto rv = print_object(obj);
  cprintf("\n");
  return rv;
}

/*!
 * Most generic printing method.
 * Does not correctly handle 64 bit boxed integers or object64's correctly.
 * It is important that no objects of type object actually exist or this will loop!
 */
u64 print_object(u32 obj) {
  if ((obj & OFFSET_MASK) == BINTEGER_OFFSET) {
    return print_binteger(obj);
  } else {
    if ((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
        (obj < 0x84000 || 0x100000 <= obj)) {              // not in kernel memory
      cprintf("#<invalid object #x%x>", obj);
    } else if ((obj & OFFSET_MASK) == PAIR_OFFSET) {
      return print_pair(obj);
    } else if ((obj & OFFSET_MASK) == BASIC_OFFSET) {
      return call_method_of_type(obj, Ptr<Type>(*Ptr<u32>(obj - 4)), GOAL_PRINT_METHOD);
    } else {
      cprintf("#<unknown type %d @ #x%x>", obj & OFFSET_MASK, obj);
    }
  }
  return obj;
}

/*!
 * Default print method for structures.
 * Structures have no runtime type info, so there's not much we can do here.
 */
u64 print_structure(u32 s) {
  cprintf("#<structure @ #x%x>", s);
  return s;
}

/*!
 * Default print method a basic.
 * Confirms basic is valid and prints the type name.
 */
u64 print_basic(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET)) {
    cprintf("#<invalid basic #x%x>", obj);
  } else {
    cprintf("#<%s @ #x%x>", info(Ptr<Type>(*Ptr<u32>(obj - 4))->symbol)->str->data(), obj);
  }
  return obj;
}

/*!
 * Print a pair as a LISP list.  Don't try to print circular lists or it will get stuck
 * Can print improper lists
 */
u64 print_pair(u32 obj) {
  if (obj == s7.offset + FIX_SYM_EMPTY_PAIR) {
    cprintf("()");
  } else {
    cprintf("(");
    auto toPrint = obj;
    for (;;) {
      if ((toPrint & OFFSET_MASK) == PAIR_OFFSET) {
        // print CAR
        print_object(*Ptr<u32>(toPrint - 2));

        // load up CDR
        auto cdr = *Ptr<u32>(toPrint + 2);
        toPrint = cdr;
        if (cdr == s7.offset + FIX_SYM_EMPTY_PAIR) {  // end of proper list
          cprintf(")");
          return obj;
        } else {  // continue list
          cprintf(" ");
        }
      } else {  // improper list
        cprintf(". ");
        print_object(toPrint);
        cprintf(")");
        return obj;
      }
    }
  }
  return obj;
}

/*!
 * Print an integer. Works correctly for 64-bit integers.
 */
u64 print_integer(u64 obj) {
  // not sure why this is any better than cprintf("%ld") or similar. Maybe a tiny bit faster?
  char* str = PrintPending.cast<char>().c();
  if (!str) {
    str = (PrintBufArea + 0x18).cast<char>().c();
  }

  PrintPending = make_ptr(strend(str)).cast<u8>();
  kitoa((char*)PrintPending.c(), obj, 10, 0xffffffff, '0', 0);
  return obj;
}

/*!
 * Print a boxed integer. Works correctly for 64-bit integers. Assumes signed.
 */
u64 print_binteger(u64 obj) {
  char* str = PrintPending.cast<char>().c();
  if (!PrintPending.offset) {
    str = (PrintBufArea + 0x18).cast<char>().c();
  }

  PrintPending = make_ptr(strend(str)).cast<u8>();
  kitoa((char*)PrintPending.c(), ((s64)obj) >> 3, 10, 0xffffffff, '0', 0);
  return obj;
}

/*!
 * Print floating point number.
 */
u64 print_float(u32 f) {
  // again not sure why this is any better than cprintf("%f") or similar. Maybe a tiny bit faster?
  float ff;
  *(u32*)&ff = f;
  char* str = PrintPending.cast<char>().c();
  if (!PrintPending.offset) {
    str = (PrintBufArea + 0x18).cast<char>().c();
  }

  PrintPending = make_ptr(strend(str)).cast<u8>();

  ftoa((char*)PrintPending.c(), ff, 0xffffffff, ' ', 4, 0);
  return f;
}

/*!
 * Print method for symbol.  Just prints the name without quotes or anything fancy.
 */
u64 print_symbol(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) ||
      *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_SYMBOL_TYPE)) {
    cprintf("#<invalid symbol #x%x>", obj);
  } else {
    char* str = info(Ptr<Symbol>(obj))->str->data();
    cprintf("%s", str);
  }
  return obj;
}

/*!
 * Print method for type.  Just prints the name without quotes
 */
u64 print_type(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) || *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_TYPE_TYPE)) {
    cprintf("#<invalid type #x%x>", obj);
  } else {
    cprintf("%s", info(Ptr<Type>(obj)->symbol)->str->data());
  }
  return obj;
}

/*!
 * Print method for string.  Prints the string in quotes.
 */
u64 print_string(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) ||
      *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_STRING_TYPE)) {
    cprintf("#<invalid string #x%x>", obj);
  } else {
    cprintf("\"%s\"", Ptr<String>(obj)->data());
  }
  return obj;
}

/*!
 * Print method for function. Just prints the address because functions can't identify themselves.
 */
u64 print_function(u32 obj) {
  cprintf("#<compiled %s @ #x%x>", info(Ptr<Type>(*Ptr<u32>(obj - 4))->symbol)->str->data(), obj);
  return obj;
}

/*!
 * Print method for VU functions.  Again, just prints address.
 */
u64 print_vu_function(u32 obj) {
  cprintf("#<compiled vu-function @ #x%x>", obj);
  return obj;
}

/*!
 * Get the allocated size field of a basic.  By default we grab this from the type struct.
 * Dynamically sized basics should override this method.
 */
u64 asize_of_basic(u32 it) {
  return Ptr<Type>(it - 4)->allocated_size;
}

/*!
 * Copy method that does no copying.
 */
u64 copy_fixed(u32 it) {
  return it;
}

/*!
 * Default copy for a structure. Since this has no idea of the actual type, it doesn't know what
 * size to copy.  So we do no copy and return a reference to the original data.
 */
u64 copy_structure(u32 it, u32 unknown) {
  (void)unknown;
  return it;
}

/*!
 * Create a copy of a basic.  If the destination isn't identified as a symbol, treat it as an
 * address. This seems a little bit unsafe to me, as it reads the 4-bytes before the given address
 * and checks it against the symbol type pointer to see if its a symbol. It seems possible to have a
 * false positive for this check.
 */
u64 copy_basic(u32 obj, u32 heap) {
  // determine size of basic. We call a method instead of using asize_of_basic in case the type has
  // overridden the default asize_of method.
  u32 size = call_method_of_type(obj, Ptr<Type>(*Ptr<u32>(obj - BASIC_OFFSET)), GOAL_ASIZE_METHOD);
  u32 result;

  if (*Ptr<u32>(heap - 4) == *(s7 + FIX_SYM_SYMBOL_TYPE)) {
    // we think we're creating a new copy on a heap.  First allocate memory...
    result = alloc_heap_object(heap, *Ptr<u32>(obj - BASIC_OFFSET), size);
    // then copy! (minus the type tag, alloc_heap_object already did it for us)
    memcpy(Ptr<u32>(result).c(), Ptr<u32>(obj).c(), size - BASIC_OFFSET);
  } else {
    printf("DANGER COPY BASIC!\n");
    // copy directly (including type tag)
    memcpy(Ptr<u32>(heap - BASIC_OFFSET).c(), Ptr<u32>(obj - BASIC_OFFSET).c(), size);
    result = heap;
  }
  return result;
}

/*!
 * Highest level inspect method. Won't inspect 64-bit bintegers correctly.
 */
u64 inspect_object(u32 obj) {
  if ((obj & OFFSET_MASK) == BINTEGER_OFFSET) {
    return inspect_binteger(obj);
  } else {
    if ((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
        (obj < 0x84000 || 0x100000 <= obj)) {              // not in kernel memory
      cprintf("#<invalid object #x%x>", obj);
    } else if ((obj & OFFSET_MASK) == PAIR_OFFSET) {
      return inspect_pair(obj);
    } else if ((obj & OFFSET_MASK) == BASIC_OFFSET) {
      return call_method_of_type(obj, Ptr<Type>(*Ptr<u32>(obj - BASIC_OFFSET)),
                                 GOAL_INSPECT_METHOD);
    } else {
      cprintf("#<unknown type %d @ #x%x>", obj & OFFSET_MASK, obj);
    }
  }
  return obj;
}

/*!
 * Inspect a pair.
 */
u64 inspect_pair(u32 obj) {
  cprintf("[%8x] pair ", obj);
  print_pair(obj);
  cprintf("\n");
  return obj;
}

/*!
 * Inspect an integer (works correctly on 64-bit integers)
 */
u64 inspect_integer(u64 obj) {
  // and now we're using cprintf. Why doesn't print do this?
  cprintf("[%16lx] fixnum %ld\n", obj, obj);
  return obj;
}

/*!
 * Inspect a boxed integer (works correctly on 64-integers)
 */
u64 inspect_binteger(u64 obj) {
  cprintf("[%16lx] boxed-fixnum %ld\n", obj, s64(obj) >> 3);
  return obj;
}

/*!
 * Inspect a floating point number
 */
u64 inspect_float(u32 f) {
  float ff;
  ff = *(float*)(&f);
  cprintf("[%8x] float ", f);

  // likely copy-pasta - no need for this check because of the cprintf immediately before.
  char* str = PrintPending.cast<char>().c();
  if (!str) {
    str = (PrintBufArea + 0x18).cast<char>().c();
  }

  PrintPending = make_ptr(strend(str)).cast<u8>();

  ftoa(PrintPending.cast<char>().c(), ff, -1, ' ', 4, 0);
  cprintf("\n");
  return f;
}

/*!
 * Inspect a string. There's a typo in allocated_length (has underscore instead of dash).
 * This typo is fixed in later games.
 */
u64 inspect_string(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) ||
      *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_STRING_TYPE)) {
    cprintf("#<invalid string #x%x>\n", obj);
  } else {
    auto str = Ptr<String>(obj);
    cprintf("[%8x] string\n\tallocated_length: %d\n\tdata: \"%s\"\n", obj, str->len, str->data());
  }
  return obj;
}

/*!
 * Inspect a symbol.
 */
u64 inspect_symbol(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) ||
      *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_SYMBOL_TYPE)) {
    cprintf("#<invalid symbol #x%x>", obj);
  } else {
    auto sym = Ptr<Symbol>(obj);
    auto inf = info(sym);
    cprintf("[%8x] symbol\n\tname: %s\n\thash: #x%x\n\tvalue: ", obj, inf->str->data(), inf->hash);
    print_object(sym->value);
    cprintf("\n");
  }
  return obj;
}

/*!
 * Inspect a type.
 */
u64 inspect_type(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET) || *Ptr<u32>(obj - 4) != *(s7 + FIX_SYM_TYPE_TYPE)) {
    cprintf("#<invalid type #x%x>\n", obj);
  } else {
    auto typ = Ptr<Type>(obj);
    auto sym = typ->symbol;
    auto inf = info(sym);

    cprintf("[%8x] type\n\tname: %s\n\tparent: ", obj, inf->str->data());
    print_object(typ->parent.offset);
    cprintf("\n\tsize: %d/%d\n\theap-base: %d\n\tallocated_length: %d\n\tprint: ",
            typ->allocated_size, typ->padded_size, typ->heap_base, typ->num_methods);
    print_object(typ->print_method.offset);
    cprintf("\n\tinspect: ");
    print_object(typ->inspect_method.offset);
    cprintf("\n");
  }
  return obj;
}

/*!
 * Inspect a structure.
 */
u64 inspect_structure(u32 obj) {
  cprintf("[%8x] structure\n", obj);
  return obj;
}

/*!
 * Inspect a basic. This is just a fallback for basics which don't know how to inspect themselves.
 * We just use print_object.
 */
u64 inspect_basic(u32 obj) {
  if (((obj < SymbolTable2.offset || 0x7ffffff < obj) &&  // not in normal memory
       (obj < 0x84000 || 0x100000 <= obj))                // not in kernel memory
      || ((obj & OFFSET_MASK) != BASIC_OFFSET)) {
    cprintf("#<invalid basic #x%x>\n", obj);
  } else {
    cprintf("[%8x] ", obj);
    print_object(*Ptr<u32>(obj - 4));
    cprintf("\n");
  }
  return obj;
}

/*!
 * Inspect a link block. This link block doesn't seem to be used at all.
 */
u64 inspect_link_block(u32 ob) {
  struct LinkBlock {
    u32 length;
    u32 version;
  };

  auto lb = Ptr<LinkBlock>(ob);
  cprintf("[%8x] link-block\n\tallocated_length: %d\n\tversion: %d\n\tfunction: ", ob, lb->length,
          lb->version);
  print_object(ob + lb->length);
  cprintf("\n");
  return ob;
}

/*!
 * Inspect a VU Function. Doesn't seem to be used. Also the concept of "vu-function"
 * isn't really used. VU0 macro mode stuff goes in normal functions, and micro-mode stuff goes
 * in a giant dump of many functions thats loaded and unloaded all at the same time.
 */
u64 inspect_vu_function(u32 obj) {
  struct VuFunction {
    u32 length;
    u32 origin;
    u32 qlength;
  };

  auto vf = Ptr<VuFunction>(obj);
  cprintf("[%8x] vu-function\n\tlength: %d\n\torigin: #x%x\n\tqlength: %d\n", obj, vf->length,
          vf->origin, vf->qlength);
  return obj;
}

/*!
 * This doesn't exist in the game, but we add it as a wrapper around kheapstatus.
 * Note that this isn't a great inspect as it prints to stdout instead of the printbuffer.
 */
u64 inspect_kheap(u32 obj) {
  kheapstatus(Ptr<kheapinfo>(obj));
  return obj;
}

/*!
 * Doesn't exist in the game. Maybe it was a macro?
 */
u64 pack_type_flag(u64 methods, u64 heap_base, u64 size) {
  return (methods << 32) + (heap_base << 16) + (size);
}

// void iterate_symbol_table(std::function<void(u32)> f) {
//  auto sym = s7.offset;
//  for (; sym < LastSymbol.offset; sym += 8) {
//    f(sym);
//  }
//
//  sym = SymbolTable2.offset;
//  for (; sym < s7.offset; sym += 8) {
//    f(sym);
//  }
//}
//
// void print_symbol_table() {
//  u32 sym_cnt = 0;
//  iterate_symbol_table([&](u32 sym) {
//    if (info(Ptr<Symbol>(sym))->hash) {
//      sym_cnt++;
//      inspect_object(sym);
//      //      inspect_object(Ptr<Symbol>(sym)->value);
//      //      printf("\n");
//    }
//  });
//  cprintf("Total %d symbols\n", sym_cnt);
//}

/*!
 * TODO remove me!
 */
s32 test_function(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  return arg0 + 2 * arg1 + 3 * arg2 + 4 * arg3;
}

extern "C" {
// defined in asm_funcs. It calls format_impl and sets up arguments correctly.
void _format();
}

/*!
 * Initializes the GOAL Heap, GOAL Symbol Table, GOAL Funcdamental Types, loads the GOAL kernel,
 * exports Machine functions, loads the game engine, and calls "play" to initialize the engine.
 *
 * This takes care of all initialization that isn't for the hardware itself.
 */
s32 InitHeapAndSymbol() {
  // allocate memory for the symbol table
  auto symbol_table = kmalloc(kglobalheap, 0x20000, KMALLOC_MEMSET, "symbol-table").cast<u32>();

  // pointer to the middle symbol is stored in the s7 register.
  s7 = symbol_table + (GOAL_MAX_SYMBOLS / 2) * 8 + BASIC_OFFSET;
  // pointer to the first symbol (SymbolTable2 is the "lower" symbol table)
  SymbolTable2 = symbol_table + BASIC_OFFSET;
  // the last symbol we will ever access.
  LastSymbol = symbol_table + 0xff00;
  NumSymbols = 0;
  // inform compiler the symbol table is reset, and where it is.
  reset_output();

  // set up the empty pair:
  *(s7 + FIX_SYM_EMPTY_CAR) = (s7 + FIX_SYM_EMPTY_PAIR).offset;
  *(s7 + FIX_SYM_EMPTY_CDR) = (s7 + FIX_SYM_EMPTY_PAIR).offset;

  // need to set up 'global fixed symbol so allocating memory works.
  *(s7 + FIX_SYM_GLOBAL_HEAP) = kglobalheap.offset;

  // allocate fundamental types
  alloc_and_init_type((s7 + FIX_SYM_TYPE_TYPE).cast<Symbol>(), 9);
  alloc_and_init_type((s7 + FIX_SYM_SYMBOL_TYPE).cast<Symbol>(), 9);
  alloc_and_init_type((s7 + FIX_SYM_STRING_TYPE).cast<Symbol>(), 9);
  alloc_and_init_type((s7 + FIX_SYM_FUNCTION_TYPE).cast<Symbol>(), 9);

  // booleans
  set_fixed_symbol(FIX_SYM_FALSE, "#f", s7.offset + FIX_SYM_FALSE);
  set_fixed_symbol(FIX_SYM_TRUE, "#t", s7.offset + FIX_SYM_TRUE);

  // functions
  set_fixed_symbol(FIX_SYM_NOTHING_FUNC, "nothing", make_nothing_func().offset);
  set_fixed_symbol(FIX_SYM_ZERO_FUNC, "zero-func", make_zero_func().offset);
  set_fixed_symbol(FIX_SYM_ASIZE_OF_BASIC_FUNC, "asize-of-basic-func",
                   make_function_from_c((void*)asize_of_basic).offset);
  // NOTE: this is a typo in the game too.
  set_fixed_symbol(FIX_SYM_COPY_BASIC_FUNC, "asize-of-basic-func",
                   make_function_from_c((void*)copy_basic).offset);
  set_fixed_symbol(FIX_SYM_DEL_BASIC_FUNC, "delete-basic",
                   make_function_from_c((void*)delete_basic).offset);

  // heap symbols
  set_fixed_symbol(FIX_SYM_GLOBAL_HEAP, "global", kglobalheap.offset);
  set_fixed_symbol(FIX_SYM_DEBUG_HEAP, "debug", kdebugheap.offset);
  set_fixed_symbol(FIX_SYM_STATIC, "static", (s7 + FIX_SYM_STATIC).offset);
  set_fixed_symbol(FIX_SYM_LOADING_LEVEL, "loading-level", (s7 + FIX_SYM_LOADING_LEVEL).offset);
  set_fixed_symbol(FIX_SYM_LOADING_PACKAGE, "loading-package",
                   (s7 + FIX_SYM_LOADING_PACKAGE).offset);
  set_fixed_symbol(FIX_SYM_PROCESS_LEVEL_HEAP, "process-level-heap",
                   (s7 + FIX_SYM_PROCESS_LEVEL_HEAP).offset);
  set_fixed_symbol(FIX_SYM_STACK, "stack", (s7 + FIX_SYM_STACK).offset);
  set_fixed_symbol(FIX_SYM_SCRATCH, "scratch", (s7 + FIX_SYM_SCRATCH).offset);
  set_fixed_symbol(FIX_SYM_SCRATCH_TOP, "*scratch-top*", 0x70000000);

  // level stuff
  set_fixed_symbol(FIX_SYM_LEVEL, "level", 0);
  set_fixed_symbol(FIX_SYM_ART_GROUP, "art-group", 0);
  set_fixed_symbol(FIX_SYM_TX_PAGE_DIR, "texture-page-dir", 0);
  set_fixed_symbol(FIX_SYM_TX_PAGE, "texture-page", 0);
  set_fixed_symbol(FIX_SYM_SOUND, "sound", 0);
  set_fixed_symbol(FIX_SYM_DGO, "dgo", 0);
  set_fixed_symbol(FIX_SYM_TOP_LEVEL, "top-level", *(s7 + FIX_SYM_NOTHING_FUNC));

  // OBJECT type
  auto new_illegal_func = make_function_from_c((void*)new_illegal);
  auto delete_illegal_func = make_function_from_c((void*)delete_illegal);
  auto print_object_func = make_function_from_c((void*)print_object);
  auto inspect_object_func = make_function_from_c((void*)inspect_object);
  set_fixed_type(FIX_SYM_OBJECT_TYPE, "object", (s7 + FIX_SYM_OBJECT_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), print_object_func.offset, inspect_object_func.offset);
  auto object_type = Ptr<Type>(*(s7 + FIX_SYM_OBJECT_TYPE));
  object_type->new_method = new_illegal_func;
  object_type->delete_method = delete_illegal_func;
  object_type->asize_of_method.offset = *(s7 + FIX_SYM_ZERO_FUNC);
  auto copy_fixed_function = make_function_from_c((void*)copy_fixed);
  object_type->copy_method = copy_fixed_function;

  // STRUCTURE type
  auto print_structure_func = make_function_from_c((void*)print_structure);
  auto inspect_structure_func = make_function_from_c((void*)inspect_structure);
  set_fixed_type(FIX_SYM_STRUCTURE_TYPE, "structure", (s7 + FIX_SYM_OBJECT_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), print_structure_func.offset,
                 inspect_structure_func.offset);
  auto new_structure_func = make_function_from_c((void*)new_structure);
  auto delete_structure_func = make_function_from_c((void*)delete_structure);
  auto structureType = Ptr<Type>(*(s7 + FIX_SYM_STRUCTURE_TYPE));
  structureType->new_method = new_structure_func;
  structureType->delete_method = delete_structure_func;

  // BASIC type
  auto print_basic_func = make_function_from_c((void*)print_basic);
  auto inspect_basic_function = make_function_from_c((void*)inspect_basic);
  set_fixed_type(FIX_SYM_BASIC_TYPE, "basic", (s7 + FIX_SYM_STRUCTURE_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), print_basic_func.offset, inspect_basic_function.offset);
  auto new_basic_func = make_function_from_c((void*)new_basic);
  auto basicType = Ptr<Type>(*(s7 + FIX_SYM_BASIC_TYPE));
  basicType->new_method = new_basic_func;
  basicType->delete_method.offset = *(s7 + FIX_SYM_DEL_BASIC_FUNC);
  basicType->asize_of_method.offset = *(s7 + FIX_SYM_ASIZE_OF_BASIC_FUNC);
  basicType->copy_method.offset = *(s7 + FIX_SYM_COPY_BASIC_FUNC);

  // SYMBOL type
  auto print_symbol_func = make_function_from_c((void*)print_symbol);
  auto inspect_symbol_func = make_function_from_c((void*)inspect_symbol);
  set_fixed_type(FIX_SYM_SYMBOL_TYPE, "symbol", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), print_symbol_func.offset, inspect_symbol_func.offset);
  auto symbolType = Ptr<Type>(*(s7 + FIX_SYM_SYMBOL_TYPE));
  symbolType->new_method = new_illegal_func;
  symbolType->delete_method = delete_illegal_func;

  // TYPE type
  auto print_type_func = make_function_from_c((void*)print_type);
  auto inspect_type_func = make_function_from_c((void*)inspect_type);
  set_fixed_type(FIX_SYM_TYPE_TYPE, "type", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x38), print_type_func.offset, inspect_type_func.offset);
  auto typeType = Ptr<Type>(*(s7 + FIX_SYM_TYPE_TYPE));
  auto new_type_func = make_function_from_c((void*)new_type);
  typeType->new_method = new_type_func;
  typeType->delete_method = delete_illegal_func;

  // STRING type
  auto print_string_func = make_function_from_c((void*)print_string);
  auto inspect_string_func = make_function_from_c((void*)inspect_string);
  set_fixed_type(FIX_SYM_STRING_TYPE, "string", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), print_string_func.offset, inspect_string_func.offset);

  // FUNCTION type
  auto print_function_func = make_function_from_c((void*)print_function);
  set_fixed_type(FIX_SYM_FUNCTION_TYPE, "function", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), print_function_func.offset, 0);

  // VU FUNCTION type
  auto print_vu_function_func = make_function_from_c((void*)print_vu_function);
  auto inspect_vu_function_func = make_function_from_c((void*)inspect_vu_function);
  set_fixed_type(FIX_SYM_VU_FUNCTION_TYPE, "vu-function",
                 (s7 + FIX_SYM_STRUCTURE_TYPE).cast<Symbol>(), pack_type_flag(9, 0, 0x10),
                 print_vu_function_func.offset, inspect_vu_function_func.offset);

  // LINK BLOCK type
  auto inspect_link_block_func = make_function_from_c((void*)inspect_link_block);
  set_fixed_type(FIX_SYM_LINK_BLOCK, "link-block", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0xc), 0, inspect_link_block_func.offset);
  auto linkBlockType = Ptr<Type>(*(s7 + FIX_SYM_LINK_BLOCK));
  linkBlockType->new_method = new_illegal_func;
  linkBlockType->delete_method = delete_illegal_func;

  // KHEAP
  auto inspect_kheap_func = make_function_from_c((void*)inspect_kheap);
  set_fixed_type(FIX_SYM_KHEAP, "kheap", (s7 + FIX_SYM_STRUCTURE_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x10), 0, inspect_kheap_func.offset);

  // ARRAY
  set_fixed_type(FIX_SYM_ARRAY_TYPE, "array", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x10), 0, 0);

  // PAIR
  auto print_pair_func = make_function_from_c((void*)print_pair);
  auto inspect_pair_func = make_function_from_c((void*)inspect_pair);
  set_fixed_type(FIX_SYM_PAIR_TYPE, "pair", (s7 + FIX_SYM_OBJECT_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), print_pair_func.offset, inspect_pair_func.offset);
  auto pairType = Ptr<Type>(*(s7 + FIX_SYM_PAIR_TYPE));
  auto new_pair_func = make_function_from_c((void*)new_pair);
  auto delete_pair_func = make_function_from_c((void*)delete_pair);
  pairType->new_method = new_pair_func;
  pairType->delete_method = delete_pair_func;

  // KERNEL TYPES
  set_fixed_type(FIX_SYM_PROCESS_TREE_TYPE, "process-tree",
                 (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(), pack_type_flag(0xe, 0, 0x20), 0, 0);
  set_fixed_type(FIX_SYM_PROCESS_TYPE, "process", (s7 + FIX_SYM_PROCESS_TREE_TYPE).cast<Symbol>(),
                 pack_type_flag(0xe, 0, 0x70), 0, 0);
  set_fixed_type(FIX_SYM_THREAD_TYPE, "thread", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(0xc, 0, 0x28), 0, 0);
  set_fixed_type(FIX_SYM_CONNECTABLE_TYPE, "connectable",
                 (s7 + FIX_SYM_STRUCTURE_TYPE).cast<Symbol>(), pack_type_flag(9, 0, 0x10), 0, 0);
  set_fixed_type(FIX_SYM_STACK_FRAME_TYPE, "stack-frame", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0xc), 0, 0);
  set_fixed_type(FIX_SYM_FILE_STREAM_TYPE, "file-stream", (s7 + FIX_SYM_BASIC_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x14), 0, 0);

  // NUMERIC TYPES
  set_fixed_type(FIX_SYM_POINTER_TYPE, "pointer", (s7 + FIX_SYM_OBJECT_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), 0, 0);

  // NUMERIC TYPES
  auto print_integer_func = make_function_from_c((void*)print_integer);
  auto inspect_integer_func = make_function_from_c((void*)inspect_integer);
  set_fixed_type(FIX_SYM_NUMBER_TYPE, "number", (s7 + FIX_SYM_OBJECT_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), print_integer_func.offset, inspect_integer_func.offset);

  auto print_float_func = make_function_from_c((void*)print_float);
  auto inspect_float_func = make_function_from_c((void*)inspect_float);
  set_fixed_type(FIX_SYM_FLOAT_TYPE, "float", (s7 + FIX_SYM_NUMBER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), print_float_func.offset, inspect_float_func.offset);

  set_fixed_type(FIX_SYM_INTEGER_TYPE, "integer", (s7 + FIX_SYM_NUMBER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), 0, 0);

  auto print_binteger_func = make_function_from_c((void*)print_binteger);
  auto inspect_binteger_func = make_function_from_c((void*)inspect_binteger);
  set_fixed_type(FIX_SYM_BINTEGER_TYPE, "binteger", (s7 + FIX_SYM_INTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), print_binteger_func.offset, inspect_binteger_func.offset);

  set_fixed_type(FIX_SYM_SINTEGER_TYPE, "sinteger", (s7 + FIX_SYM_INTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), 0, 0);
  set_fixed_type(FIX_SYM_INT8_TYPE, "int8", (s7 + FIX_SYM_SINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 1), 0, 0);
  set_fixed_type(FIX_SYM_INT16_TYPE, "int16", (s7 + FIX_SYM_SINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 2), 0, 0);
  set_fixed_type(FIX_SYM_INT32_TYPE, "int32", (s7 + FIX_SYM_SINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), 0, 0);
  set_fixed_type(FIX_SYM_INT64_TYPE, "int64", (s7 + FIX_SYM_SINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), 0, 0);
  set_fixed_type(FIX_SYM_INT128_TYPE, "int128", (s7 + FIX_SYM_SINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x10), 0, 0);

  set_fixed_type(FIX_SYM_UINTEGER_TYPE, "uintger", (s7 + FIX_SYM_INTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), 0, 0);
  set_fixed_type(FIX_SYM_UINT8_TYPE, "uint8", (s7 + FIX_SYM_UINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 1), 0, 0);
  set_fixed_type(FIX_SYM_UINT16_TYPE, "uint16", (s7 + FIX_SYM_UINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 2), 0, 0);
  set_fixed_type(FIX_SYM_UINT32_TYPE, "uint32", (s7 + FIX_SYM_UINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 4), 0, 0);
  set_fixed_type(FIX_SYM_UINT64_TYPE, "uint64", (s7 + FIX_SYM_UINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 8), 0, 0);
  set_fixed_type(FIX_SYM_UINT128_TYPE, "uint128", (s7 + FIX_SYM_UINTEGER_TYPE).cast<Symbol>(),
                 pack_type_flag(9, 0, 0x10), 0, 0);

  // Object new macro
  auto goal_new_object_func = make_function_from_c((void*)alloc_heap_object);
  object_type->new_method = goal_new_object_func;

  // Stuff that isn't in a fixed spot:
  make_function_symbol_from_c("string->symbol", (void*)intern);
  make_function_symbol_from_c("print", (void*)sprint);
  make_function_symbol_from_c("inspect", (void*)inspect_object);

  // loads
  make_function_symbol_from_c("load", (void*)load);
  make_function_symbol_from_c("loado", (void*)loado);
  make_function_symbol_from_c("unload", (void*)unload);

  make_function_symbol_from_c("_format", (void*)_format);

  // allocations
  make_function_symbol_from_c("malloc", (void*)alloc_heap_memory);
  make_function_symbol_from_c("kmalloc", (void*)goal_malloc);
  make_function_symbol_from_c("new-dynamic-structure", (void*)new_dynamic_structure);

  // type system
  make_function_symbol_from_c("method-set!", (void*)method_set);

  // dgo
  make_function_symbol_from_c("link", (void*)link_and_exec_wrapper);
  make_function_symbol_from_c("dgo-load", (void*)load_and_link_dgo);

  // forward declare
  make_raw_function_symbol_from_c("ultimate-memcpy", 0);
  make_raw_function_symbol_from_c("memcpy-and-rellink", 0);  // never used
  make_raw_function_symbol_from_c("symlink2", 0);
  make_raw_function_symbol_from_c("symlink3", 0);

  // game stuff
  make_function_symbol_from_c("link-begin", (void*)link_begin);
  make_function_symbol_from_c("link-resume", (void*)link_resume);
  //  make_function_symbol_from_c("mc-run", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-format", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-unformat", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-create-file", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-save", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-load", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-check-result", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-get-slot-info", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("mc-makefile", &CKernel::not_yet_implemented);
  //  make_function_symbol_from_c("kset-language", &CKernel::not_yet_implemented);

  // set *debug-segment*
  auto ds_symbol = intern_from_c("*debug-segment*");
  if (DebugSegment) {
    ds_symbol->value = (s7 + FIX_SYM_TRUE).offset;
  } else {
    ds_symbol->value = (s7 + FIX_SYM_FALSE).offset;
  }

  // setup *enable-method-set*
  auto method_set_symbol = intern_from_c("*enable-method-set*");
  EnableMethodSet = method_set_symbol.cast<u32>();
  method_set_symbol->value = 0;

  // set *boot-video-mode*
  intern_from_c("*boot-video-mode*")->value = 0;

  // load the kernel!
  method_set_symbol->value++;
  load_and_link_dgo_from_c("kernel", kglobalheap,
                           LINK_FLAG_OUTPUT_LOAD | LINK_FLAG_EXECUTE | LINK_FLAG_PRINT_LOGIN,
                           0x400000);
  method_set_symbol->value--;

  // check the kernel version!
  auto kernel_version = intern_from_c("*kernel-version*")->value;
  if (!kernel_version || ((kernel_version >> 0x13) != KERNEL_VERSION_MAJOR)) {
    MsgErr("\n");
    MsgErr(
        "dkernel: compiled C kernel version is %d.%d but the goal kernel is %d.%d\n\tfrom the "
        "goal> prompt (:mch) then mkee your kernel in linux.\n",
        KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR, kernel_version >> 0x13,
        (kernel_version >> 3) & 0xffff);
    return -1;
  } else {
    printf("Got correct kernel version %d.%d\n", kernel_version >> 0x13,
           (kernel_version >> 3) & 0xffff);
  }

  // setup deci2count for message counter.
  protoBlock.deci2count = intern_from_c("*deci-count*").cast<s32>();

  // load stuff for the listener interface
  InitListener();

  // Do final initialization, including loading and initializing the engine.
  InitMachineScheme();

  // testing stuff:
  make_function_symbol_from_c("test-function", (void*)test_function);

  return 0;
}

/*!
 * GOAL "load" function for debug loads. Doesn't load off the CD.
 */
u64 load(u32 file_name_in, u32 heap_in) {
  printf("LOAD!\n");  // added by me
  Ptr<String> file_name(file_name_in);
  Ptr<kheapinfo> heap(heap_in);
  char decodedName[260];  // could be 256 or 260?

  auto loading_pack_sym = Ptr<Symbol>(s7.offset + FIX_SYM_LOADING_PACKAGE);
  auto last_loading_pack = loading_pack_sym->value;
  loading_pack_sym->value = heap_in;

  kstrcpy(decodedName, DecodeFileName(file_name->data()));
  s32 returnValue = load_and_link(
      file_name->data(), decodedName, heap.c(),
      LINK_FLAG_OUTPUT_TRUE | LINK_FLAG_OUTPUT_LOAD | LINK_FLAG_EXECUTE | LINK_FLAG_PRINT_LOGIN);

  loading_pack_sym->value = last_loading_pack;

  if (returnValue < 0) {
    return s7.offset;
  } else {
    return returnValue;
  }
}

/*!
 * Unused. But a C version of loading code. Doesn't load off the CD.
 */
u64 loadc(const char* file_name, kheapinfo* heap, u32 flags) {
  char decodedName[260];  // could be 256 or 260?

  auto loading_pack_sym = Ptr<Symbol>(s7.offset + FIX_SYM_LOADING_PACKAGE);
  auto last_loading_pack = loading_pack_sym->value;
  loading_pack_sym->value = make_ptr(heap).offset;
  printf("****** CALL TO loadc() ******\n");  // not added by me
  auto name = MakeFileName(CODE_FILE_TYPE, file_name, 0);
  kstrcpy(decodedName, name);

  s32 returnValue = load_and_link(file_name, decodedName, heap, flags);

  loading_pack_sym->value = last_loading_pack;

  if (returnValue < 0) {
    return s7.offset;
  } else {
    return returnValue;
  }
}

/*!
 * Load Object? Uses DATA_FILE_TYPE and doesn't inform listener about the load, or execute a
 * top level segment if a V3 is loaded. Doesn't load off the CD.
 */
u64 loado(u32 file_name_in, u32 heap_in) {
  char loadName[272];
  Ptr<String> file_name(file_name_in);
  Ptr<kheapinfo> heap(heap_in);
  printf("****** CALL TO loado(%s) ******\n", file_name->data());
  kstrcpy(loadName, MakeFileName(DATA_FILE_TYPE, file_name->data(), 0));
  s32 returnValue = load_and_link(file_name->data(), loadName, heap.c(), LINK_FLAG_PRINT_LOGIN);

  if (returnValue < 0) {
    return s7.offset;
  } else {
    return returnValue;
  }
}

/*!
 * "Unload". Doesn't free memory, just informs listener we unloaded.
 */
u64 unload(u32 name) {
  output_unload(Ptr<String>(name)->data());
  return 0;
}

/*!
 * load and link and exec.  Common function in load/loado/loadc.
 * Doesn't load off the CD.
 */
s64 load_and_link(const char* filename, char* decode_name, kheapinfo* heap, u32 flags) {
  (void)filename;
  s32 sz;
  auto rv = FileLoad(decode_name, make_ptr(heap), Ptr<u8>(0), KMALLOC_ALIGN_64, &sz);
  if (((s32)rv.offset) > -1) {
    return (s32)link_and_exec(rv, decode_name, sz, make_ptr(heap), flags).offset;
  }
  return (s32)rv.offset;
}

// todo, read lcock code