/*!
 * @file xdbg.h
 * Debugging utility library. This hides the platform specific details of the debugger.
 * Nothing in here should hold state, that should all be managed in Debugger.
 */

#pragma once

#include <string>
#include <cstdint>
#include "common/common_types.h"

#ifdef __linux
#include <sys/types.h>
#elif __APPLE__
#include <sys/types.h>
#elif _WIN32
// todo - windows includes
#endif

namespace xdbg {
#ifdef __APPLE__

/*!
 * Identification for a thread.
 */
struct ThreadID {
  pid_t id = 0;

  std::string to_string() const;
  explicit ThreadID(const std::string& str);
  explicit ThreadID(pid_t _id);
  ThreadID() = default;
};

/*!
 * Handle for the memory of a process.
 */
struct MemoryHandle {
  int fd;
};

#elif _WIN32
struct ThreadID {
  // todo - whatever windows uses to identify a thread
  std::string to_string() const;
  ThreadID(const std::string& str);
  ThreadID();  // todo - add id type here, like in linux version
};

struct MemoryHandle {
  int a;
};

#elif __LINUX__
/*!
 * Identification for a thread.
 */
struct ThreadID {
  pid_t id = 0;

  std::string to_string() const;
  explicit ThreadID(const std::string& str);
  explicit ThreadID(pid_t _id);
  ThreadID() = default;
};
#endif

/*!
 * The info required to debug the target.
 */
struct DebugContext {
  ThreadID tid;    //! The target's GOAL thread
  uintptr_t base;  //! The base address for the GOAL memory
  uint32_t s7;     //! The value of s7 (GOAL address)
};

/*!
 * The x86-64 registers, including rip.
 */
struct Regs {
  u64 gprs[16];
  u128 xmms[16];

  u64 rip;

  std::string print_gprs() const;
  std::string print_xmms_as_flt() const;
  std::string print_xmms_as_int() const;
  std::string print_xmms_as_flt_vec() const;
};

/*!
 * Information about why the target has stopped.
 */
struct SignalInfo {
  enum Kind {
    SEGFAULT,        // access bad memory
    BREAK,           // hit a breakpoint or execute int3
    MATH_EXCEPTION,  // divide by zero
    UNKNOWN          // some other signal that is unsupported
  } kind = UNKNOWN;
};

// Functions
ThreadID get_current_thread_id();
bool attach_and_break(const ThreadID& tid);
void allow_debugging();
bool detach_and_resume(const ThreadID& tid);
bool get_regs_now(const ThreadID& tid, Regs* out);
bool set_regs_now(const ThreadID& tid, const Regs& in);
bool break_now(const ThreadID& tid);
bool cont_now(const ThreadID& tid);
bool open_memory(const ThreadID& tid, MemoryHandle* out);
bool close_memory(const ThreadID& tid, MemoryHandle* handle);
bool read_goal_memory(u8* dest_buffer,
                      int size,
                      u32 goal_addr,
                      const DebugContext& context,
                      const MemoryHandle& mem);

bool write_goal_memory(const u8* src_buffer,
                       int size,
                       u32 goal_addr,
                       const DebugContext& context,
                       const MemoryHandle& mem);

template <typename T>
bool write_goal_value(T& value,
                      u32 goal_addr,
                      const DebugContext& context,
                      const MemoryHandle& handle) {
  return write_goal_memory(&value, sizeof(value), goal_addr, context, handle);
}

bool check_stopped(const ThreadID& tid, SignalInfo* out);

}  // namespace xdbg
