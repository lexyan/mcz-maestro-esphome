#include "mcz_climate.h"

#include <cmath>

namespace esphome::mcz_maestro {

climate::ClimateTraits MczClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.set_visual_min_temperature(5);
  traits.set_visual_max_temperature(35);
  traits.set_visual_target_temperature_step(0.5f);
  traits.set_visual_current_temperature_step(0.5f);
  return traits;
}

void MczClimate::control(const climate::ClimateCall &call) {
  // Commands only: the state is published when the stove reports it
  if (call.get_mode().has_value()) {
    auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_HEAT) {
      this->parent_->write_parameter(PARAM_POWER, 1);
    } else if (mode == climate::CLIMATE_MODE_OFF) {
      this->parent_->write_parameter(PARAM_POWER, 40);
    }
  }
  if (call.has_custom_preset())
    this->parent_->write_parameter(PARAM_CONTROL_MODE, call.get_custom_preset() == this->auto_preset_ ? 1 : 0);
  if (call.get_target_temperature().has_value())
    this->parent_->write_parameter(PARAM_SETPOINT, (int) lroundf(*call.get_target_temperature() * 2));
}

void MczClimate::on_info(const MczInfo &info) {
  bool changed = !this->published_;

  if (info.has(FIELD_STATE)) {
    uint32_t state = info.get(FIELD_STATE);
    bool burning = mcz_state_is_burning(state);
    bool waiting = mcz_state_is_waiting(state);
    auto mode = (burning || waiting) ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;
    auto action = burning   ? climate::CLIMATE_ACTION_HEATING
                  : waiting ? climate::CLIMATE_ACTION_IDLE
                            : climate::CLIMATE_ACTION_OFF;
    if (mode != this->mode || action != this->action) {
      this->mode = mode;
      this->action = action;
      changed = true;
    }
  }

  if (info.has(FIELD_AMBIENT)) {
    float current = info.get(FIELD_AMBIENT) / 2.0f;
    if (current != this->current_temperature) {
      this->current_temperature = current;
      changed = true;
    }
  }

  if (info.has(FIELD_SETPOINT)) {
    float target = info.get(FIELD_SETPOINT) / 2.0f;
    if (target != this->target_temperature) {
      this->target_temperature = target;
      changed = true;
    }
  }

  int8_t control_mode = this->parent_->get_control_mode();
  if (control_mode >= 0 && control_mode != this->preset_mode_) {
    this->preset_mode_ = control_mode;
    this->set_custom_preset_(control_mode == 1 ? this->auto_preset_ : this->manual_preset_);
    changed = true;
  }

  if (changed) {
    this->published_ = true;
    this->publish_state();
  }
}

}  // namespace esphome::mcz_maestro
