#include "mcz_switch.h"

namespace esphome::mcz_maestro {

void MczSwitch::on_hub_setup() {
  // Nothing is ever written to the stove at start-up: the state comes from the stove.
  if (this->kind_ == SWITCH_VIRTUAL_PROBE)
    this->publish_state(this->parent_->get_probe_enabled());
}

void MczSwitch::write_state(bool state) {
  switch (this->kind_) {
    case SWITCH_VIRTUAL_PROBE:
      this->parent_->set_probe_enabled(state);
      this->publish_state(state);
      break;
    case SWITCH_POWER:
      this->parent_->write_parameter(PARAM_POWER, state ? 1 : 40);
      break;
    default:
      if (this->parent_->write_parameter(this->param_, state ? 1 : 0) && this->field_ == MCZ_NO_FIELD)
        this->publish_state(state);  // no read-back available: last command sent
      break;
  }
}

void MczSwitch::on_info(const MczInfo &info) {
  switch (this->kind_) {
    case SWITCH_VIRTUAL_PROBE:
      break;
    case SWITCH_POWER:
      if (info.has(FIELD_STATE))
        this->publish_state(mcz_state_is_on(info.get(FIELD_STATE)));
      break;
    default:
      if (info.has(this->field_))
        this->publish_state(info.get(this->field_) != 0);
      break;
  }
}

}  // namespace esphome::mcz_maestro
