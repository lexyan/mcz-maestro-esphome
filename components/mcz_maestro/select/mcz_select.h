#pragma once

#include <vector>

#include "esphome/components/select/select.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

class MczSelect : public select::Select, public MczListener, public Parented<MczMaestro> {
 public:
  void set_param(uint16_t param) { this->param_ = param; }
  void set_field(uint8_t field) { this->field_ = field; }
  void set_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void on_info(const MczInfo &info) override;

 protected:
  void control(size_t index) override;

  uint16_t param_{0};
  uint8_t field_{MCZ_NO_FIELD};
  int16_t last_{-1};
  std::vector<uint8_t> mappings_;
};

}  // namespace esphome::mcz_maestro
