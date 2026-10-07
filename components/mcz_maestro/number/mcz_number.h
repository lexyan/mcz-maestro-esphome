#pragma once

#include "esphome/components/number/number.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

enum MczNumberKind : uint8_t {
  NUMBER_RAW = 0,   // integer sent as is
  NUMBER_HALF,      // temperature: value x 2
  NUMBER_HALF_OPT,  // temperature: value x 2, 255 read back = not available
  NUMBER_POWER,     // power level 1..5 sent as 11..15, refused in automatic mode
  NUMBER_MINUTES,   // minutes, stored by the stove in seconds
};

class MczNumber : public number::Number, public MczListener, public Parented<MczMaestro> {
 public:
  void set_kind(MczNumberKind kind) { this->kind_ = kind; }
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }
  /// Value read from an auxiliary frame and written to a cell of the database.
  void set_aux(MczAuxKind kind, uint8_t index) {
    this->aux_kind_ = kind;
    this->aux_index_ = index;
  }
  void set_cell(uint16_t cell, uint8_t bytes) {
    this->cell_ = cell;
    this->cell_bytes_ = bytes;
  }

  void on_info(const MczInfo &info) override;
  void on_aux(MczAuxKind kind, const MczValues &values) override;

 protected:
  void control(float value) override;

  MczNumberKind kind_{NUMBER_RAW};
  uint16_t param_{0};
  void publish_raw_(uint32_t raw);

  uint16_t cell_{0};
  uint8_t cell_bytes_{1};
  uint8_t field_{MCZ_NO_FIELD};
  uint8_t aux_index_{MCZ_NO_FIELD};
  MczAuxKind aux_kind_{AUX_EXTRA};
  int32_t last_{-1};
};

}  // namespace esphome::mcz_maestro
