#include "mcz_button.h"

namespace esphome::mcz_maestro {

void MczButton::press_action() {
  switch (this->kind_) {
    case BUTTON_WRITE:
      this->parent_->write_parameter(this->param_, this->value_);
      break;
    case BUTTON_SET_TIME:
      this->parent_->sync_time();
      break;
    default:
      this->parent_->request_info();
      break;
  }
}

}  // namespace esphome::mcz_maestro
