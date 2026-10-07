#pragma once

#include <vector>

#include "esphome/components/select/select.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

class MczSelect : public select::Select, public MczListener, public Parented<MczMaestro> {
 public:
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }
  /// Value read from an auxiliary frame instead of the information frame.
  void set_aux(MczAuxKind kind, uint8_t index) {
    this->aux_kind_ = kind;
    this->aux_index_ = index;
  }
  /// Value written to a cell of the database (1 byte) instead of a parameter.
  void set_cell(uint16_t cell) { this->cell_ = cell; }
  /// Room input: choosing the thermostat also switches the stove to automatic regulation.
  void set_auto_mode_on_zero(bool enabled) { this->auto_mode_on_zero_ = enabled; }
  void set_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void on_info(const MczInfo &info) override;
  void on_aux(MczAuxKind kind, const MczValues &values) override;

 protected:
  void control(size_t index) override;
  void publish_value_(uint32_t value, const char *what, uint8_t number);

  uint16_t param_{0};
  uint16_t cell_{0};
  uint8_t field_{MCZ_NO_FIELD};
  uint8_t aux_index_{MCZ_NO_FIELD};
  MczAuxKind aux_kind_{AUX_EXTRA};
  bool auto_mode_on_zero_{false};
  int16_t last_{-1};
  std::vector<uint8_t> mappings_;
};

}  // namespace esphome::mcz_maestro
