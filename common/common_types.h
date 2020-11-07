#pragma once

/*!
 * @file common_types.h
 * Common Integer Types.
 */

#ifndef JAK1_COMMON_TYPES_H
#define JAK1_COMMON_TYPES_H

#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

struct u128 {
  union {
    u64 du64[2];
    u32 du32[4];
    u16 du16[8];
    u8 du8[16];
    float f[4];
  };
};
static_assert(sizeof(u128) == 16, "u128");

#endif  // JAK1_COMMON_TYPES_H
