#pragma once

#include "esphome/components/button/button.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

enum MczButtonKind : uint8_t {
  BUTTON_REFRESH = 0,  // request the information frame
  BUTTON_WRITE,        // write a fixed value to a parameter
  BUTTON_SET_TIME,     // set the stove clock from the time source
};

class MczButton : public button::Button, public Parented<MczMaestro> {
 public:
  void set_kind(MczButtonKind kind) { this->kind_ = kind; }
  void set_write(uint16_t param, uint8_t value) {
    this->param_ = param;
    this->value_ = value;
  }

 protected:
  void press_action() override;

  MczButtonKind kind_{BUTTON_REFRESH};
  uint16_t param_{0};
  uint8_t value_{0};
};

}  // namespace esphome::mcz_maestro
