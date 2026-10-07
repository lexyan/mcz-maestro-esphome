#pragma once

#include "esphome/components/number/number.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

enum MczNumberKind : uint8_t {
  NUMBER_RAW = 0,   // integer sent as is
  NUMBER_HALF,      // temperature: value x 2
  NUMBER_HALF_OPT,  // temperature: value x 2, 255 read back = not available
  NUMBER_POWER,     // power level 1..5 sent as 11..15, refused in automatic mode
};

class MczNumber : public number::Number, public MczListener, public Parented<MczMaestro> {
 public:
  void set_kind(MczNumberKind kind) { this->kind_ = kind; }
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }

  void on_info(const MczInfo &info) override;

 protected:
  void control(float value) override;

  MczNumberKind kind_{NUMBER_RAW};
  uint16_t param_{0};
  uint8_t field_{MCZ_NO_FIELD};
  int32_t last_{-1};
};

}  // namespace esphome::mcz_maestro
