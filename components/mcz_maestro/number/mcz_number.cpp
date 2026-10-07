#include "mcz_number.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::mcz_maestro {

static const char *const TAG = "mcz_maestro.number";

void MczNumber::control(float value) {
  int raw;
  switch (this->kind_) {
    case NUMBER_HALF:
    case NUMBER_HALF_OPT:
      raw = (int) lroundf(value * 2);
      break;
    case NUMBER_POWER:
      // In automatic mode the stove modulates its own power
      if (this->parent_->get_control_mode() == 1) {
        ESP_LOGW(TAG, "Power setting ignored: the stove is in automatic mode");
        return;
      }
      raw = (int) lroundf(value) + 10;
      break;
    default:
      raw = (int) lroundf(value);
      break;
  }
  if (this->parent_->write_parameter(this->param_, raw) && this->field_ == MCZ_NO_FIELD)
    this->publish_state(value);  // no read-back available: last value sent
}

void MczNumber::on_info(const MczInfo &info) {
  if (!info.has(this->field_))
    return;
  uint32_t raw = info.get(this->field_);
  if ((int32_t) raw == this->last_)
    return;
  this->last_ = (int32_t) raw;
  float value = raw;
  switch (this->kind_) {
    case NUMBER_HALF:
      value /= 2.0f;
      break;
    case NUMBER_HALF_OPT:
      value = raw == 255 ? NAN : value / 2.0f;
      break;
    case NUMBER_POWER:
      if (value > 10)
        value -= 10;
      break;
    default:
      break;
  }
  this->publish_state(value);
}

}  // namespace esphome::mcz_maestro
