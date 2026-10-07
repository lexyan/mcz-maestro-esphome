#pragma once

#include <vector>

#include "esphome/components/select/select.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

class MczSelect : public select::Select, public MczListener, public Parented<MczMaestro> {
 public:
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }
  /// Recipe selects: value read from the extra parameters, written to a database cell.
  void set_extra(uint8_t index, uint16_t cell) {
    this->extra_index_ = index;
    this->cell_ = cell;
  }
  void set_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void on_info(const MczInfo &info) override;
  void on_extra(const MczExtra &extra) override;

 protected:
  void control(size_t index) override;

  uint16_t param_{0};
  void publish_value_(uint32_t value, const char *what, uint8_t number);

  uint8_t field_{MCZ_NO_FIELD};
  uint8_t extra_index_{MCZ_NO_FIELD};
  uint16_t cell_{0};
  int16_t last_{-1};
  std::vector<uint8_t> mappings_;
};

}  // namespace esphome::mcz_maestro
