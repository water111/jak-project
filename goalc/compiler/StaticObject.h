#ifndef JAK_STATICOBJECT_H
#define JAK_STATICOBJECT_H

#include <string>
#include "goalc/emitter/ObjectGenerator.h"

class StaticObject {
 public:
  virtual std::string print() const = 0;

  struct LoadInfo {
    bool requires_load = false;
    int load_size = -1;
    bool load_signed = false;
    bool prefer_xmm = false;
  };

  virtual LoadInfo get_load_info() const = 0;
  virtual void generate(emitter::ObjectGenerator* gen) = 0;
  virtual int get_addr_offset() const = 0;

  emitter::StaticRecord rec;
};

class StaticString : public StaticObject {
 public:
  explicit StaticString(std::string data, int _seg);
  std::string text;
  int seg = -1;
  std::string print() const override;
  LoadInfo get_load_info() const override;
  void generate(emitter::ObjectGenerator* gen) override;
  int get_addr_offset() const override;
};

class StaticFloat : public StaticObject {
 public:
  explicit StaticFloat(float _value, int _seg);
  float value = 0;
  int seg = -1;
  std::string print() const override;
  LoadInfo get_load_info() const override;
  void generate(emitter::ObjectGenerator* gen) override;
  int get_addr_offset() const override;
};

#endif  // JAK_STATICOBJECT_H
