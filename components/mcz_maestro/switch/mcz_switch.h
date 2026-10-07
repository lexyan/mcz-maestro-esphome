#pragma once

#include "esphome/components/switch/switch.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

enum MczSwitchKind : uint8_t {
  SWITCH_PARAM = 0,      // 0/1 parameter, state read from a field (non-zero = on)
  SWITCH_POWER,          // stove on/off: parameter 34, 1 = on, 40 = off
  SWITCH_VIRTUAL_PROBE,  // enables the virtual WiFi probe, sends nothing by itself
};

class MczSwitch : public switch_::Switch, public MczListener, public Parented<MczMaestro> {
 public:
  void set_kind(MczSwitchKind kind) { this->kind_ = kind; }
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }

  void on_hub_setup() override;
  void on_info(const MczInfo &info) override;

 protected:
  void write_state(bool state) override;

  MczSwitchKind kind_{SWITCH_PARAM};
  uint16_t param_{0};
  uint8_t field_{MCZ_NO_FIELD};
};

}  // namespace esphome::mcz_maestro
