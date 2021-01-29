#pragma once

/*!
 * @file BinaryWriter.h
 * Write raw data like a stream.
 */

#include <cassert>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>

struct BinaryWriterRef {
  size_t offset;
  size_t write_size;
};

class BinaryWriter {
 public:
  BinaryWriter() = default;

  template <typename T>
  BinaryWriterRef add(const T& obj) {
    auto orig_size = data.size();
    data.resize(orig_size + sizeof(T));
    memcpy(data.data() + orig_size, &obj, sizeof(T));
    return {orig_size, sizeof(T)};
  }

  template <typename T>
  void add_at_ref(const T& obj, const BinaryWriterRef& ref) {
    assert(ref.write_size == sizeof(T));
    assert(ref.offset + ref.write_size < get_size());
    memcpy(data.data() + ref.offset, &obj, sizeof(T));
  }

  void add_str_len(const std::string& str, size_t len) { add_cstr_len(str.c_str(), len); }

  void add_cstr_len(const char* str, size_t len) {
    size_t i = 0;
    while (*str) {
      data.push_back(*str);
      str++;
      i++;
    }

    while (i < len) {
      data.push_back(0);
      i++;
    }
  }

  BinaryWriterRef add_data(void* d, size_t len) {
    auto orig_size = data.size();
    data.resize(orig_size + len);
    memcpy(data.data() + orig_size, d, len);
    return {orig_size, len};
  }

  size_t get_size() { return data.size(); }

  void* get_data() { return data.data(); }

  void write_to_file(const std::string& filename) {
    auto fp = fopen(filename.c_str(), "wb");
    if (!fp)
      throw std::runtime_error("failed to open " + filename);
    if (fwrite(get_data(), get_size(), 1, fp) != 1)
      throw std::runtime_error("failed to write " + filename);
    fclose(fp);
  }

 private:
  std::vector<uint8_t> data;
};
