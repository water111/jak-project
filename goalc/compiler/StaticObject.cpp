#include "third-party/fmt/core.h"
#include "StaticObject.h"
#include "common/goal_constants.h"

namespace {
template <typename T>
uint32_t push_data_to_byte_vector(T data, std::vector<uint8_t>& v) {
  auto* ptr = (uint8_t*)(&data);
  for (std::size_t i = 0; i < sizeof(T); i++) {
    v.push_back(ptr[i]);
  }
  return sizeof(T);
}
}  // namespace

////////////////
// StaticString
////////////////
StaticString::StaticString(std::string data, int _seg) : text(std::move(data)), seg(_seg) {}

std::string StaticString::print() const {
  return fmt::format("static-string \"{}\"", text);
}

StaticObject::LoadInfo StaticString::get_load_info() const {
  LoadInfo info;
  info.requires_load = false;
  info.prefer_xmm = false;
  return info;
}

void StaticString::generate(emitter::ObjectGenerator* gen) {
  rec = gen->add_static_to_seg(seg, 16);
  auto& d = gen->get_static_data(rec);

  // add "string" type tag:
  gen->link_static_type_ptr(rec, d.size(), "string");
  for (int i = 0; i < POINTER_SIZE; i++) {
    d.push_back(0xbe);
  }

  // add allocated size
  push_data_to_byte_vector<u32>(text.size() + 1, d);

  // add chars
  for (auto c : text) {
    d.push_back(c);
  }
  d.push_back(0);
}

int StaticString::get_addr_offset() const {
  return BASIC_OFFSET;
}

////////////////
// StaticFloat
////////////////

StaticFloat::StaticFloat(float _value, int _seg) : value(_value), seg(_seg) {}

StaticObject::LoadInfo StaticFloat::get_load_info() const {
  LoadInfo info;
  info.requires_load = true;
  info.load_size = 4;
  info.load_signed = false;
  info.prefer_xmm = true;
  return info;
}

void StaticFloat::generate(emitter::ObjectGenerator* gen) {
  rec = gen->add_static_to_seg(seg, 4);
  auto& d = gen->get_static_data(rec);
  push_data_to_byte_vector<float>(value, d);
}

int StaticFloat::get_addr_offset() const {
  return 0;
}

std::string StaticFloat::print() const {
  return fmt::format("(sf {})", value);
}