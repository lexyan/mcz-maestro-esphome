#include "mcz_select.h"

#include "esphome/core/log.h"

namespace esphome::mcz_maestro {

static const char *const TAG = "mcz_maestro.select";

void MczSelect::control(size_t index) {
  if (index >= this->mappings_.size())
    return;
  // The state is published when the stove reports the new value
  if (this->cell_ != 0) {
    this->parent_->write_database(this->cell_, 1, this->mappings_[index]);
  } else {
    this->parent_->write_parameter(this->param_, this->mappings_[index]);
  }
}

void MczSelect::on_info(const MczInfo &info) {
  if (this->field_ == MCZ_NO_FIELD || !info.has(this->field_))
    return;
  this->publish_value_(info.get(this->field_), "Field", this->field_);
}

void MczSelect::on_extra(const MczExtra &extra) {
  if (this->extra_index_ == MCZ_NO_FIELD || !extra.has(this->extra_index_))
    return;
  this->publish_value_(extra.get(this->extra_index_), "Extra parameter", this->extra_index_);
}

void MczSelect::publish_value_(uint32_t value, const char *what, uint8_t number) {
  if (value <= 255 && (int16_t) value == this->last_)
    return;
  for (size_t i = 0; i < this->mappings_.size(); i++) {
    if (this->mappings_[i] == value) {
      this->last_ = (int16_t) value;
      this->publish_state(i);
      return;
    }
  }
  ESP_LOGW(TAG, "%s %u: value %u has no option", what, number, (unsigned) value);
}

}  // namespace esphome::mcz_maestro
