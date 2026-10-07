#pragma once

#include "esphome/components/climate/climate.h"
#include "../mcz_maestro.h"

namespace esphome::mcz_maestro {

/// Thermostat: on/off (parameter 34), setpoint (42), room temperature and
/// regulation mode as two custom presets (parameter 40).
/// The state shown always comes from the stove; nothing is sent at start-up.
class MczClimate : public climate::Climate, public MczListener, public Parented<MczMaestro> {
 public:
  void set_presets(const char *manual, const char *automatic) {
    this->manual_preset_ = manual;
    this->auto_preset_ = automatic;
    this->set_supported_custom_presets({manual, automatic});
  }

  void on_info(const MczInfo &info) override;

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  const char *manual_preset_{"Manual"};
  const char *auto_preset_{"Auto"};
  int8_t preset_mode_{-1};
  bool published_{false};
};

}  // namespace esphome::mcz_maestro
