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
static const uint8_t MCZ_MAX_PROBES = 3;
static const uint8_t ROOM_INPUT_WIFI_PROBE = 255;
static const uint8_t MCZ_MAX_VALUES = 32;

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

/// Frames read in addition to the information frame. They only change when written,
/// so they are requested rarely, and only when an entity needs them.
enum MczAuxKind : uint8_t {
  // C|RecuperoParametriExtra|11, type 03: 0 = air recipe, 1 = pellet recipe, 2 = room input,
  // 3 = eco-stop delay (seconds), 4 = eco-stop hysteresis
  AUX_EXTRA = 0,
  // C|RecuperoParametri, type 00, one value per byte: 2/3 = setpoint min/max, 8/14/20 = front fan /
  // ducted fan 1 / ducted fan 2 fitted, 30 = silent mode available
  AUX_PARAMS,
  // C|RecuperoVersioneSW, type 0E: text fields, decoded by the hub
  AUX_VERSIONS,
  // C|RecuperoSondeWiFi, type 0B: 0 = interval (min), 1 = summer interval (min), 2 = last
  // connection (timestamp), 6 = signal (%), 7 = offset
  AUX_PROBES,
  // C|RecuperaAllarmi, type 0A: pairs of state / timestamp, decoded by the hub
  AUX_ALARMS,
  AUX_COUNT,
};

/// Values of an auxiliary frame, in the order of the frame.
struct MczValues {
  uint32_t values[MCZ_MAX_VALUES];
  uint32_t valid{0};

  bool has(uint8_t index) const { return index < MCZ_MAX_VALUES && ((this->valid >> index) & 1UL) != 0; }
  uint32_t get(uint8_t index) const { return this->values[index]; }
};

/// Implemented by the entities that follow the information frame.
class MczListener {
 public:
  virtual void on_info(const MczInfo &info) = 0;
  virtual void on_aux(MczAuxKind kind, const MczValues &values) {}
  virtual void on_hub_setup() {}
};

enum MczSensorConv : uint8_t {
  CONV_RAW = 0,   // value as sent
  CONV_HALF,      // value / 2
  CONV_HALF_OPT,  // value / 2, 255 = probe absent
  CONV_HOURS,     // seconds to hours
  CONV_POWER,     // 11..15 to 1..5
  CONV_OPT,       // value as sent, 255 = not available
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
  // Texts below come from the auxiliary frames
  TEXT_BOOTLOADER,
  TEXT_WIFI_DIRECT,
  TEXT_WIFI_REMOTE,
  TEXT_WIFI_PROBE,
  TEXT_DATABASE_NAME,
  TEXT_DATABASE_REVISION,
  TEXT_SERIAL_NUMBER,
  TEXT_PROBE_LAST_SEEN,
  TEXT_LAST_ALARM,
  TEXT_ALARM_HISTORY,
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
  /// Poll an auxiliary frame. Called by the entities that need it.
  void enable_aux(MczAuxKind kind) { this->aux_[kind].enabled = true; }
#ifdef USE_SENSOR
  /// Declare a virtual WiFi probe. number is 1, 2 or 3 (sent as 51, 52 or 53).
  void add_probe(uint8_t number, sensor::Sensor *source, const char *version, bool require_api) {
    if (number < 1 || number > MCZ_MAX_PROBES)
      return;
    auto &probe = this->probes_[number - 1];
    probe.source = source;
    probe.version = version;
    probe.require_api = require_api;
    // The room input tells whether the stove listens to the WiFi probe
    this->enable_aux(AUX_EXTRA);
  }
  void set_probe_sent_sensor(uint8_t number, sensor::Sensor *s) {
    if (number >= 1 && number <= MCZ_MAX_PROBES)
      this->probes_[number - 1].sent_sensor = s;
  }
  void set_probe_interval_sensor(uint8_t number, sensor::Sensor *s) {
    if (number >= 1 && number <= MCZ_MAX_PROBES)
      this->probes_[number - 1].interval_sensor = s;
  }
  void register_sensor(sensor::Sensor *s, uint8_t field, MczSensorConv conv) {
    this->sensors_.push_back({s, field, conv});
  }
  void register_aux_sensor(sensor::Sensor *s, MczAuxKind kind, uint8_t index, MczSensorConv conv) {
    this->aux_sensors_.push_back({s, kind, index, conv});
    this->enable_aux(kind);
  }
#endif
#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(binary_sensor::BinarySensor *s, uint8_t field, MczBinaryMode mode, uint8_t a,
                              uint8_t b) {
    this->binary_sensors_.push_back({s, field, mode, a, b});
  }
  void set_link_binary_sensor(binary_sensor::BinarySensor *s) { this->link_sensor_ = s; }
  void register_aux_binary_sensor(binary_sensor::BinarySensor *s, MczAuxKind kind, uint8_t index) {
    this->aux_binary_sensors_.push_back({s, kind, index});
    this->enable_aux(kind);
  }
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
  /// Queue C|WriteBancaDati|<cell>|<bytes>|<value in hex>: writes a cell of the stove's
  /// parameter database (1 or 2 bytes). The extra parameters are read back afterwards.
  /// The technical parameters of the stove live in the same database: only write cells
  /// whose meaning is known.
  /// Returns false if the write was not queued (start-up guard or queue full).
  bool write_database(uint16_t cell, uint8_t bytes, uint32_t value);
  /// Queue a request for the information frame.
  void request_info();
  /// Queue a request for an auxiliary frame.
  void request_aux(MczAuxKind kind);
  /// Request the information frame and every auxiliary frame in use.
  void refresh_all() {
    this->request_info();
    for (uint8_t kind = 0; kind < AUX_COUNT; kind++) {
      if (this->aux_[kind].enabled)
        this->request_aux((MczAuxKind) kind);
    }
  }
  /// Read an auxiliary frame again once the pending writes are sent.
  void refresh_aux(MczAuxKind kind) {
    if (this->aux_[kind].enabled)
      this->aux_[kind].pending = true;
  }
  /// Set the stove clock from the configured time source.
  void sync_time();

  // --- State ---
  /// Regulation mode reported by the stove: -1 unknown, 0 manual, 1 automatic.
  int8_t get_control_mode() const { return this->control_mode_; }
  bool is_linked() const;
  /// Suspend or resume the transmissions of every virtual probe.
  void set_probe_enabled(bool enabled) { this->probe_enabled_ = enabled; }
  bool get_probe_enabled() const { return this->probe_enabled_; }

 protected:
  void handle_frame_();
  void handle_info_();
  void handle_aux_(MczAuxKind kind);
  void handle_versions_();
  void handle_alarms_();
  void publish_text_(MczTextKind kind, const std::string &text);
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
  struct AuxQuery {
    bool enabled{false};
    bool pending{false};   // to be requested once the queue is empty
    bool received{false};
    uint32_t last_request{0};
  };
  AuxQuery aux_[AUX_COUNT];
  int8_t last_aux_{-1};  // auxiliary frame whose reply is awaited, -1 if none
  int8_t alarm_active_{-1};
  uint32_t busy_since_{0};
  uint32_t last_reply_{0};
  uint32_t last_tick_{0};
  int8_t control_mode_{-1};
  MczInfo info_;

  bool announce_{true};
  const char *module_version_{"1.2.6"};
  uint32_t write_guard_ms_{20000};

  // Virtual WiFi probes: all of them are suspended together by the virtual_probe switch
  bool probe_enabled_{true};
  int8_t last_probe_index_{-1};  // probe whose reply is awaited, -1 if none
  // Room input reported by the stove: -1 unknown, 255 WiFi probe, 0 thermostat, 1 stove probe
  int16_t room_input_{-1};

#ifdef USE_SENSOR
  struct SensorEntry {
    sensor::Sensor *sensor;
    uint8_t field;
    MczSensorConv conv;
  };
  std::vector<SensorEntry> sensors_;
  struct AuxSensorEntry {
    sensor::Sensor *sensor;
    MczAuxKind kind;
    uint8_t index;
    MczSensorConv conv;
  };
  std::vector<AuxSensorEntry> aux_sensors_;
  struct ProbeEntry {
    sensor::Sensor *source{nullptr};  // nullptr = probe not declared
    sensor::Sensor *sent_sensor{nullptr};
    sensor::Sensor *interval_sensor{nullptr};
    const char *version{"1.9.9"};
    bool require_api{true};
    uint32_t interval_s{300};  // adjusted by the reply of the stove
    uint32_t last_send{0};
  };
  ProbeEntry probes_[MCZ_MAX_PROBES];
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
  struct AuxBinaryEntry {
    binary_sensor::BinarySensor *sensor;
    MczAuxKind kind;
    uint8_t index;
  };
  std::vector<AuxBinaryEntry> aux_binary_sensors_;
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
