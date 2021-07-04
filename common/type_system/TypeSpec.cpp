/*!
 * @file TypeSpec.cpp
 * A GOAL TypeSpec is a reference to a type or compound type.
 */

#include "TypeSpec.h"
#include "third-party/fmt/core.h"

bool TypeTag::operator==(const TypeTag& other) const {
  return name == other.name && value == other.value;
}

std::string TypeSpec::print() const {
  if (m_arguments.empty() && m_tags.empty()) {
    return m_type;
  } else {
    std::string result = "(" + m_type;
    for (const auto& tag : m_tags) {
      result += fmt::format(" :{} {}", tag.name, tag.value);
    }
    for (auto& x : m_arguments) {
      result += " " + x.print();
    }
    return result + ")";
  }
}

bool TypeSpec::operator!=(const TypeSpec& other) const {
  return !(other == *this);
}

bool TypeSpec::operator==(const TypeSpec& other) const {
  return m_type == other.m_type && m_arguments == other.m_arguments && m_tags == other.m_tags;
}

TypeSpec TypeSpec::substitute_for_method_call(const std::string& method_type) const {
  TypeSpec result;
  result.m_type = (m_type == "_type_") ? method_type : m_type;
  for (const auto& x : m_arguments) {
    result.m_arguments.push_back(x.substitute_for_method_call(method_type));
  }
  return result;
}

bool TypeSpec::is_compatible_child_method(const TypeSpec& implementation,
                                          const std::string& child_type) const {
  bool ok = implementation.m_type == m_type ||
            (m_type == "_type_" && implementation.m_type == child_type);
  if (!ok || implementation.m_arguments.size() != m_arguments.size()) {
    return false;
  }

  for (size_t i = 0; i < m_arguments.size(); i++) {
    if (!m_arguments[i].is_compatible_child_method(implementation.m_arguments[i], child_type)) {
      return false;
    }
  }

  return true;
}

void TypeSpec::add_new_tag(const std::string& tag_name, const std::string& tag_value) {
  for (auto& v : m_tags) {
    if (v.name == tag_name) {
      throw std::runtime_error(
          fmt::format("Attempted to add a duplicate tag {} to typespec.", tag_name));
    }
  }

  m_tags.push_back({tag_name, tag_value});
}

std::optional<std::string> TypeSpec::try_get_tag(const std::string& tag_name) const {
  for (auto& tag : m_tags) {
    if (tag.name == tag_name) {
      return tag.value;
    }
  }
  return {};
}

const std::string& TypeSpec::get_tag(const std::string& tag_name) const {
  for (auto& tag : m_tags) {
    if (tag.name == tag_name) {
      return tag.value;
    }
  }
  throw std::runtime_error(fmt::format("TypeSpec didn't have tag {}", tag_name));
}

void TypeSpec::modify_tag(const std::string& tag_name, const std::string& tag_value) {
  for (auto& tag : m_tags) {
    if (tag.name == tag_name) {
      tag.value = tag_value;
      return;
    }
  }
  throw std::runtime_error(fmt::format("TypeSpec didn't have tag {}", tag_name));
}

void TypeSpec::add_or_modify_tag(const std::string& tag_name, const std::string& tag_value) {
  for (auto& tag : m_tags) {
    if (tag.name == tag_name) {
      tag.value = tag_value;
      return;
    }
  }
  m_tags.push_back({tag_name, tag_value});
}