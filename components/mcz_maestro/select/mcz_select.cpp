#include "mcz_select.h"

#include "esphome/core/log.h"

namespace esphome::mcz_maestro {

static const char *const TAG = "mcz_maestro.select";

void MczSelect::control(size_t index) {
  if (index >= this->mappings_.size())
    return;
  const uint8_t value = this->mappings_[index];
  // The state is published when the stove reports the new value
  if (this->cell_ != 0) {
    if (!this->parent_->write_database(this->cell_, 1, value))
      return;
    // Same sequence as the MCZ app
    if (this->auto_mode_on_zero_ && value == 0)
      this->parent_->write_parameter(PARAM_CONTROL_MODE, 1);
    return;
  }
  if (this->parent_->write_parameter(this->param_, value) && this->aux_index_ != MCZ_NO_FIELD)
    this->parent_->refresh_aux(this->aux_kind_);
}

void MczSelect::on_info(const MczInfo &info) {
  if (this->field_ == MCZ_NO_FIELD || !info.has(this->field_))
    return;
  this->publish_value_(info.get(this->field_), "Field", this->field_);
}

void MczSelect::on_aux(MczAuxKind kind, const MczValues &values) {
  if (this->aux_index_ == MCZ_NO_FIELD || kind != this->aux_kind_ || !values.has(this->aux_index_))
    return;
  this->publish_value_(values.get(this->aux_index_), "Auxiliary value", this->aux_index_);
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
