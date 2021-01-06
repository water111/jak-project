#pragma once

#include <string>

/*!
 * A label to a location in an object file.
 * Doesn't have to be word aligned.
 */
struct DecompilerLabel {
  std::string name;
  int target_segment;
  int offset;  // in bytes
};