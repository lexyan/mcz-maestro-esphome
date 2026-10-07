#pragma once

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome::mcz_maestro {

static const uint8_t MCZ_MAX_FIELDS = 64;
static const uint8_t MCZ_NO_FIELD = 0xFF;

// Fields of the information frame (reply to C|RecuperoInfo)
static const uint8_t FIELD_STATE = 1;
static const uint8_t FIELD_AMBIENT = 6;
static const uint8_t FIELD_CONTROL_MODE = 22;
static const uint8_t FIELD_SETPOINT = 26;
static const uint8_t FIELD_PELLET_SENSOR = 47;

// Write parameters (C|WriteParametri|<param>|<value>)
static const uint16_t PARAM_POWER = 34;
static const uint16_t PARAM_CONTROL_MODE = 40;
static const uint16_t PARAM_SETPOINT = 42;

/// Decoded information frame. Values are the raw integers sent by the mainboard.
struct MczInfo {
  uint32_t values[MCZ_MAX_FIELDS];
  uint64_t valid{0};

  bool has(uint8_t field) const { return field < MCZ_MAX_FIELDS && ((this->valid >> field) & 1ULL) != 0; }
  uint32_t get(uint8_t field) const { return this->values[field]; }
};

/// True for the states in which the stove is considered switched on (1-15, 31, 40-43).
bool mcz_state_is_on(uint32_t state);
/// True while the stove is igniting or burning (1-15, 31).
bool mcz_state_is_burning(uint32_t state);
/// True while the stove waits for a heat demand (auto eco 45, standby 46).
bool mcz_state_is_waiting(uint32_t state);

/// Implemented by the entities that follow the information frame.
class MczListener {
 public:
  virtual void on_info(const MczInfo &info) = 0;
  virtual void on_hub_setup() {}
};

enum MczSensorConv : uint8_t {
  CONV_RAW = 0,   // value as sent
  CONV_HALF,      // value / 2
  CONV_HALF_OPT,  // value / 2, 255 = probe absent
  CONV_HOURS,     // seconds to hours
  CONV_POWER,     // 11..15 to 1..5
};

enum MczBinaryMode : uint8_t {
  BIN_NONZERO = 0,
  BIN_EQUALS,
  BIN_RANGE,
};

enum MczTextKind : uint8_t {
  TEXT_STATE = 0,
  TEXT_DATETIME,
  TEXT_FIRMWARE,
  TEXT_VALVE,
  TEXT_PELLET,
  TEXT_KIND_COUNT,
};

class MczMaestro : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- Configuration ---
  void set_announce(bool announce) { this->announce_ = announce; }
  void set_module_version(const char *version) { this->module_version_ = version; }
  void set_write_guard(uint32_t ms) { this->write_guard_ms_ = ms; }
  void set_probe_id(uint8_t id) { this->probe_id_ = id; }
  void set_probe_version(const char *version) { this->probe_version_ = version; }
  void set_probe_require_api(bool require) { this->probe_require_api_ = require; }
#ifdef USE_SENSOR
  void set_probe_source(sensor::Sensor *source) { this->probe_source_ = source; }
  void set_probe_sent_sensor(sensor::Sensor *s) { this->probe_sent_sensor_ = s; }
  void set_probe_interval_sensor(sensor::Sensor *s) { this->probe_interval_sensor_ = s; }
  void register_sensor(sensor::Sensor *s, uint8_t field, MczSensorConv conv) {
    this->sensors_.push_back({s, field, conv});
  }
#endif
#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(binary_sensor::BinarySensor *s, uint8_t field, MczBinaryMode mode, uint8_t a,
                              uint8_t b) {
    this->binary_sensors_.push_back({s, field, mode, a, b});
  }
  void set_link_binary_sensor(binary_sensor::BinarySensor *s) { this->link_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_text_sensor(MczTextKind kind, text_sensor::TextSensor *s) { this->text_sensors_[kind] = s; }
#endif
#ifdef USE_TIME
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
#endif
  void register_listener(MczListener *listener) { this->listeners_.push_back(listener); }

  // --- Actions, usable from lambdas ---
  /// Queue a raw command, without the final '^'. Returns false if the queue is full.
  bool send_command(const std::string &command);
  /// Queue C|WriteParametri|<param>|<value>. A refresh follows unless the mainboard
  /// answers the write with the information frame itself. Returns false if the
  /// write was not queued (start-up guard or queue full).
  bool write_parameter(uint16_t param, int value);
  /// Queue a request for the information frame.
  void request_info();
  /// Set the stove clock from the configured time source.
  void sync_time();

  // --- State ---
  /// Regulation mode reported by the stove: -1 unknown, 0 manual, 1 automatic.
  int8_t get_control_mode() const { return this->control_mode_; }
  bool is_linked() const;
  void set_probe_enabled(bool enabled) { this->probe_enabled_ = enabled; }
  bool get_probe_enabled() const { return this->probe_enabled_; }

 protected:
  void handle_frame_();
  void handle_info_();
  void send_next_();
  void probe_tick_();
  void publish_texts_(const MczInfo &info);

  std::string rx_;
  std::vector<std::string> tx_queue_;
  bool busy_{false};
  bool last_was_probe_{false};
  bool last_was_announce_{false};
  bool last_was_write_{false};
  bool refresh_pending_{false};
  uint32_t busy_since_{0};
  uint32_t last_reply_{0};
  uint32_t last_tick_{0};
  int8_t control_mode_{-1};
  MczInfo info_;

  bool announce_{true};
  const char *module_version_{"1.2.6"};
  uint32_t write_guard_ms_{20000};

  // Virtual WiFi probe
  uint8_t probe_id_{51};
  const char *probe_version_{"1.9.9"};
  bool probe_enabled_{true};
  bool probe_require_api_{true};
  uint32_t probe_interval_s_{300};
  uint32_t probe_last_send_{0};

#ifdef USE_SENSOR
  struct SensorEntry {
    sensor::Sensor *sensor;
    uint8_t field;
    MczSensorConv conv;
  };
  std::vector<SensorEntry> sensors_;
  sensor::Sensor *probe_source_{nullptr};
  sensor::Sensor *probe_sent_sensor_{nullptr};
  sensor::Sensor *probe_interval_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  struct BinaryEntry {
    binary_sensor::BinarySensor *sensor;
    uint8_t field;
    MczBinaryMode mode;
    uint8_t a;
    uint8_t b;
  };
  std::vector<BinaryEntry> binary_sensors_;
  binary_sensor::BinarySensor *link_sensor_{nullptr};
  int8_t link_published_{-1};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *text_sensors_[TEXT_KIND_COUNT]{};
  int32_t text_cache_[TEXT_KIND_COUNT]{-1, -1, -1, -1, -1};
#endif
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  std::vector<MczListener *> listeners_;
};

}  // namespace esphome::mcz_maestro
