#include "mcz_select.h"

#include "esphome/core/log.h"

namespace esphome::mcz_maestro {

static const char *const TAG = "mcz_maestro.select";

void MczSelect::control(size_t index) {
  if (index >= this->mappings_.size())
    return;
  // The state is published when the stove reports the new value
  this->parent_->write_parameter(this->param_, this->mappings_[index]);
}

void MczSelect::on_info(const MczInfo &info) {
  if (!info.has(this->field_))
    return;
  uint32_t value = info.get(this->field_);
  if ((int16_t) value == this->last_)
    return;
  for (size_t i = 0; i < this->mappings_.size(); i++) {
    if (this->mappings_[i] == value) {
      this->last_ = (int16_t) value;
      this->publish_state(i);
      return;
    }
  }
  ESP_LOGW(TAG, "Field %u: value %u has no option", this->field_, (unsigned) value);
}

}  // namespace esphome::mcz_maestro
