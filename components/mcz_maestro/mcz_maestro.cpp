#include "mcz_maestro.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome::mcz_maestro {

static const char *const TAG = "mcz_maestro";

static const size_t MAX_QUEUE = 16;
static const size_t MAX_FRAME = 1024;
static const uint32_t REPLY_TIMEOUT_MS = 2000;
static const uint32_t LINK_TIMEOUT_MS = 60000;
static const uint32_t PROBE_FIRST_SEND_MS = 30000;
static const char *const INFO_COMMAND = "C|RecuperoInfo";
// Auxiliary frames only change when written: they are read rarely. interval 0 = read once.
struct AuxDef {
  const char *command;
  const char *prefix;  // frame type of the reply
  uint32_t interval_ms;
};
static const AuxDef AUX_DEFS[AUX_COUNT] = {
    {"C|RecuperoParametriExtra|11", "03", 600000},
    {"C|RecuperoParametri", "00", 0},
    {"C|RecuperoVersioneSW", "0E", 0},
    {"C|RecuperoSondeWiFi", "0B", 600000},
    {"C|RecuperaAllarmi", "0A", 600000},
};
static const uint32_t AUX_RETRY_MS = 60000;
static const size_t MAX_ALARMS = 5;
static const char *const PROBE_COMMAND = "C|RecuperaTemperaturaWiFi|";
static const size_t PROBE_COMMAND_LEN = 26;
static const uint8_t PROBE_ID_BASE = 51;  // probes 1, 2 and 3 are sent as 51, 52 and 53

// Texts are chosen at build time with the "language" option of the component.
#ifdef MCZ_MAESTRO_LANG_FR
#define MCZ_T(en, fr) fr
#else
#define MCZ_T(en, fr) en
#endif

bool mcz_state_is_burning(uint32_t state) { return (state >= 1 && state <= 15) || state == 31; }
bool mcz_state_is_waiting(uint32_t state) { return state == 45 || state == 46; }
bool mcz_state_is_on(uint32_t state) { return mcz_state_is_burning(state) || (state >= 40 && state <= 43); }

#ifdef USE_TEXT_SENSOR
static const char *state_text(uint32_t state) {
  switch (state) {
    case 0:
      return MCZ_T("Off", "Éteint");
    case 1:
      return MCZ_T("Checking hot or cold", "Contrôle chaud/froid");
    case 2:
      return MCZ_T("Cleaning, cold", "Nettoyage à froid");
    case 3:
      return MCZ_T("Loading pellets, cold", "Chargement pellets à froid");
    case 4:
      return MCZ_T("Start 1, cold", "Démarrage 1 à froid");
    case 5:
      return MCZ_T("Start 2, cold", "Démarrage 2 à froid");
    case 6:
      return MCZ_T("Cleaning, hot", "Nettoyage à chaud");
    case 7:
      return MCZ_T("Loading pellets, hot", "Chargement pellets à chaud");
    case 8:
      return MCZ_T("Start 1, hot", "Démarrage 1 à chaud");
    case 9:
      return MCZ_T("Start 2, hot", "Démarrage 2 à chaud");
    case 10:
      return MCZ_T("Stabilising", "Stabilisation");
    case 11:
      return MCZ_T("Power 1", "Puissance 1");
    case 12:
      return MCZ_T("Power 2", "Puissance 2");
    case 13:
      return MCZ_T("Power 3", "Puissance 3");
    case 14:
      return MCZ_T("Power 4", "Puissance 4");
    case 15:
      return MCZ_T("Power 5", "Puissance 5");
    case 30:
    case 48:
      return MCZ_T("Diagnostics", "Diagnostic");
    case 31:
      return MCZ_T("On", "Allumé");
    case 40:
      return MCZ_T("Extinguishing", "Extinction");
    case 41:
      return MCZ_T("Cooling", "Refroidissement");
    case 42:
      return MCZ_T("Cleaning, low", "Nettoyage bas");
    case 43:
      return MCZ_T("Cleaning, high", "Nettoyage haut");
    case 44:
      return MCZ_T("Unlocking auger", "Déblocage vis");
    case 45:
      return MCZ_T("Auto eco", "Auto éco");
    case 46:
      return MCZ_T("Standby", "Veille");
    case 49:
      return MCZ_T("Loading auger", "Chargement vis");
    case 50:
      return MCZ_T("A01 - Ignition failed", "A01 - Allumage raté");
    case 51:
      return MCZ_T("A02 - No flame", "A02 - Pas de flamme");
    case 52:
      return MCZ_T("A03 - Tank overheating", "A03 - Surchauffe réservoir");
    case 53:
      return MCZ_T("A04 - Flue gas too hot", "A04 - Fumées trop chaudes");
    case 54:
      return MCZ_T("A05 - Flue obstructed / wind", "A05 - Conduit obstrué / vent");
    case 55:
      return MCZ_T("A06 - Insufficient draught", "A06 - Tirage insuffisant");
    case 56:
      return MCZ_T("A09 - Flue gas probe", "A09 - Sonde fumées");
    case 57:
      return MCZ_T("A11 - Gear motor", "A11 - Motoréducteur");
    case 58:
      return MCZ_T("A13 - Mainboard temperature", "A13 - Température carte mère");
    case 59:
      return MCZ_T("A14 - Active fault", "A14 - Défaut Active");
    case 60:
      return MCZ_T("A18 - Water temperature", "A18 - Température eau");
    case 61:
      return MCZ_T("A19 - Water probe", "A19 - Sonde eau");
    case 62:
      return MCZ_T("A20 - Auxiliary probe", "A20 - Sonde auxiliaire");
    case 63:
      return MCZ_T("A21 - Pressure switch", "A21 - Pressostat");
    case 64:
      return MCZ_T("A22 - Room probe", "A22 - Sonde ambiante");
    case 65:
      return MCZ_T("A23 - Brazier closing", "A23 - Fermeture brasier");
    case 66:
      return MCZ_T("A12 - Gear motor controller", "A12 - Contrôle motoréducteur");
    case 67:
      return MCZ_T("A17 - Auger jammed", "A17 - Vis sans fin bloquée");
    case 69:
      return MCZ_T("Waiting for safety alarms", "Attente alarmes sécurité");
    default:
      return MCZ_T("Unknown", "Inconnu");
  }
}
#endif

void MczMaestro::setup() {
  this->rx_.reserve(384);
  if (this->announce_) {
    // Same announcement as the original firmware at power-up
    uint8_t mac[6];
    get_mac_address_raw(mac);
    char buf[64];
    snprintf(buf, sizeof(buf), "RispostaAccensioneRemoto|%02X%02X%02X%02X%02X%02X|%s", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5], this->module_version_);
    this->tx_queue_.emplace_back(buf);
  }
  this->request_info();
  for (uint8_t kind = 0; kind < AUX_COUNT; kind++) {
    if (this->aux_[kind].enabled)
      this->request_aux((MczAuxKind) kind);
  }
  for (auto *listener : this->listeners_)
    listener->on_hub_setup();
}

void MczMaestro::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MCZ Maestro:\n"
                "  Announce at start-up: %s\n"
                "  Write guard: %u ms\n"
                "  Listeners: %u\n"
                "  Auxiliary frames: extra %s, capabilities %s, versions %s, WiFi probes %s, alarms %s",
                YESNO(this->announce_), (unsigned) this->write_guard_ms_, (unsigned) this->listeners_.size(),
                YESNO(this->aux_[AUX_EXTRA].enabled), YESNO(this->aux_[AUX_PARAMS].enabled),
                YESNO(this->aux_[AUX_VERSIONS].enabled), YESNO(this->aux_[AUX_PROBES].enabled),
                YESNO(this->aux_[AUX_ALARMS].enabled));
#ifdef USE_SENSOR
  for (uint8_t i = 0; i < MCZ_MAX_PROBES; i++) {
    if (this->probes_[i].source != nullptr)
      ESP_LOGCONFIG(TAG, "  Virtual WiFi probe %u: sent as %u, version %s", i + 1, PROBE_ID_BASE + i,
                    this->probes_[i].version);
  }
#endif
  LOG_UPDATE_INTERVAL(this);
}

void MczMaestro::update() {
  this->request_info();
  const uint32_t now = millis();
  for (uint8_t kind = 0; kind < AUX_COUNT; kind++) {
    auto &aux = this->aux_[kind];
    if (!aux.enabled)
      continue;
    // Until the first reply the request is repeated every minute
    const uint32_t interval = aux.received ? AUX_DEFS[kind].interval_ms : AUX_RETRY_MS;
    if (interval != 0 && now - aux.last_request >= interval)
      this->request_aux((MczAuxKind) kind);
  }
}

bool MczMaestro::is_linked() const {
  return this->last_reply_ != 0 && millis() - this->last_reply_ < LINK_TIMEOUT_MS;
}

void MczMaestro::request_info() {
  // One pending refresh is enough
  for (const auto &queued : this->tx_queue_) {
    if (queued == INFO_COMMAND)
      return;
  }
  this->send_command(INFO_COMMAND);
}

void MczMaestro::request_aux(MczAuxKind kind) {
  this->aux_[kind].last_request = millis();
  this->aux_[kind].pending = false;
  for (const auto &queued : this->tx_queue_) {
    if (queued == AUX_DEFS[kind].command)
      return;
  }
  this->send_command(AUX_DEFS[kind].command);
}

bool MczMaestro::write_database(uint16_t cell, uint8_t bytes, uint32_t value) {
  // Safety: nothing is written to the stove right after boot
  if (millis() < this->write_guard_ms_) {
    ESP_LOGW(TAG, "Database write %u=%u ignored (start-up guard)", cell, (unsigned) value);
    return false;
  }
  if (bytes != 1 && bytes != 2)
    return false;
  if (value > (bytes == 1 ? 0xFFu : 0xFFFFu)) {
    ESP_LOGW(TAG, "Database write %u: value %u does not fit in %u byte(s)", cell, (unsigned) value, bytes);
    return false;
  }
  char buf[48];
  // Same format as the MCZ app: lower-case hexadecimal, 2 digits per byte
  snprintf(buf, sizeof(buf), "C|WriteBancaDati|%u|%u|%0*x", cell, bytes, bytes * 2, (unsigned) value);
  if (!this->send_command(buf))
    return false;
  // A single read-back follows the last write of a burst
  this->refresh_aux(AUX_EXTRA);
  return true;
}

bool MczMaestro::send_command(const std::string &command) {
  if (this->tx_queue_.size() >= MAX_QUEUE) {
    ESP_LOGW(TAG, "Queue full, command dropped: %s", command.c_str());
    return false;
  }
  this->tx_queue_.push_back(command);
  return true;
}

bool MczMaestro::write_parameter(uint16_t param, int value) {
  // Safety: nothing is written to the stove right after boot
  if (millis() < this->write_guard_ms_) {
    ESP_LOGW(TAG, "Write %u=%d ignored (start-up guard)", param, value);
    return false;
  }
  char buf[40];
  snprintf(buf, sizeof(buf), "C|WriteParametri|%u|%d", param, value);
  if (!this->send_command(buf))
    return false;
  // A single refresh follows the last write of a burst
  this->refresh_pending_ = true;
  return true;
}

void MczMaestro::sync_time() {
#ifdef USE_TIME
  if (this->time_ == nullptr) {
    ESP_LOGW(TAG, "No time source configured (time_id)");
    return;
  }
  auto now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGW(TAG, "Time source not synchronised yet");
    return;
  }
  char buf[40];
  snprintf(buf, sizeof(buf), "C|SalvaDataOra|%02d%02d%04d%02d%02d", now.day_of_month, now.month, now.year, now.hour,
           now.minute);
  if (this->send_command(buf))
    this->refresh_pending_ = true;
#else
  ESP_LOGW(TAG, "No time component in this configuration");
#endif
}

void MczMaestro::loop() {
  uint8_t c;
  while (this->available()) {
    if (!this->read_byte(&c))
      break;
    if (c == '\r' || c == '\n')
      continue;
    if (c != '^') {
      if (this->rx_.size() < MAX_FRAME)
        this->rx_.push_back((char) c);
      continue;
    }
    this->handle_frame_();
    this->rx_.clear();
  }

  const uint32_t now = millis();

  // No reply: release the link
  if (this->busy_ && now - this->busy_since_ > REPLY_TIMEOUT_MS) {
    if (this->last_was_announce_) {
      ESP_LOGD(TAG, "No reply to the start-up announcement");
    } else {
      ESP_LOGW(TAG, "No reply from the mainboard");
    }
    this->busy_ = false;
    this->last_was_probe_ = false;
    this->last_was_write_ = false;
    this->last_aux_ = -1;
    this->rx_.clear();
  }

  if (!this->busy_) {
    if (this->tx_queue_.empty() && this->refresh_pending_) {
      this->refresh_pending_ = false;
      this->request_info();
    }
    for (uint8_t kind = 0; kind < AUX_COUNT && this->tx_queue_.empty(); kind++) {
      if (this->aux_[kind].pending)
        this->request_aux((MczAuxKind) kind);
    }
    if (!this->tx_queue_.empty())
      this->send_next_();
  }

  // Slow housekeeping, once per second
  if (now - this->last_tick_ >= 1000) {
    this->last_tick_ = now;
#ifdef USE_BINARY_SENSOR
    if (this->link_sensor_ != nullptr) {
      int8_t linked = this->is_linked() ? 1 : 0;
      if (linked != this->link_published_) {
        this->link_published_ = linked;
        this->link_sensor_->publish_state(linked != 0);
      }
    }
#endif
    this->probe_tick_();
  }
}

void MczMaestro::send_next_() {
  std::string cmd = std::move(this->tx_queue_.front());
  this->tx_queue_.erase(this->tx_queue_.begin());
  ESP_LOGD(TAG, "TX: %s", cmd.c_str());
  this->last_was_probe_ = cmd.rfind(PROBE_COMMAND, 0) == 0;
  this->last_probe_index_ = -1;
  if (this->last_was_probe_) {
    // The reply carries no probe number: remember which probe it belongs to
    int id = atoi(cmd.c_str() + PROBE_COMMAND_LEN);
    if (id >= PROBE_ID_BASE && id < PROBE_ID_BASE + MCZ_MAX_PROBES)
      this->last_probe_index_ = (int8_t) (id - PROBE_ID_BASE);
  }
  this->last_was_announce_ = cmd.rfind("RispostaAccensione", 0) == 0;
  this->last_was_write_ = cmd.rfind("C|WriteParametri", 0) == 0;
  this->last_aux_ = -1;
  for (uint8_t kind = 0; kind < AUX_COUNT; kind++) {
    if (cmd == AUX_DEFS[kind].command)
      this->last_aux_ = (int8_t) kind;
  }
  this->write_str(cmd.c_str());
  this->write_str("^\r\n");
  this->busy_ = true;
  this->busy_since_ = millis();
}

void MczMaestro::handle_frame_() {
  this->busy_ = false;
  this->last_reply_ = millis();

  // The separator may arrive URL-encoded
  size_t pos;
  while ((pos = this->rx_.find("%7C")) != std::string::npos)
    this->rx_.replace(pos, 3, "|");
  const int8_t aux = this->last_aux_;
  this->last_aux_ = -1;
  if (aux == AUX_VERSIONS) {
    // This frame carries the name and password of the stove's WiFi access point
    ESP_LOGD(TAG, "RX: version frame, %u bytes (not logged)", (unsigned) this->rx_.size());
  } else {
    ESP_LOGD(TAG, "RX: %s", this->rx_.c_str());
  }

  if (this->last_was_probe_) {
    // Reply to the WiFi probe frame: 2 hex digits = minutes before the next transmission
    this->last_was_probe_ = false;
    if (this->rx_.size() >= 2 && isxdigit((unsigned char) this->rx_[0]) && isxdigit((unsigned char) this->rx_[1])) {
      char hex[3] = {this->rx_[0], this->rx_[1], 0};
      int minutes = (int) strtol(hex, nullptr, 16);
#ifdef USE_SENSOR
      if (this->last_probe_index_ >= 0) {
        auto &probe = this->probes_[this->last_probe_index_];
        if (probe.interval_sensor != nullptr)
          probe.interval_sensor->publish_state(minutes);
        if (minutes < 1)
          minutes = 1;
        if (minutes > 30)
          minutes = 30;
        probe.interval_s = minutes * 60;
      }
#endif
    }
    this->last_probe_index_ = -1;
    return;
  }

  // The information frame (type 01) and the auxiliary frames are decoded
  const bool after_write = this->last_was_write_;
  this->last_was_write_ = false;
  if (this->rx_.size() >= 3 && this->rx_[0] == '0' && this->rx_[1] == '1' && this->rx_[2] == '|') {
    // The mainboard answers a write with the updated information frame: when it is the
    // last write of a burst, the refresh that would follow is not needed.
    if (after_write && this->tx_queue_.empty())
      this->refresh_pending_ = false;
    this->handle_info_();
  } else if (aux >= 0 && this->rx_.size() >= 3 && this->rx_[2] == '|' &&
             strncmp(this->rx_.c_str(), AUX_DEFS[aux].prefix, 2) == 0) {
    // An auxiliary frame is only accepted as the reply to its own request
    this->handle_aux_((MczAuxKind) aux);
  }
}

// Splits "a|b|c" from start into hexadecimal values. Fields that are not numbers are left invalid.
static void parse_hex_fields(const std::string &frame, size_t start, MczValues &out) {
  uint8_t index = 0;
  const size_t len = frame.size();
  while (start <= len && index < MCZ_MAX_VALUES) {
    size_t end = frame.find('|', start);
    if (end == std::string::npos)
      end = len;
    if (end > start) {
      char *stop = nullptr;
      unsigned long value = strtoul(frame.c_str() + start, &stop, 16);
      if (stop == frame.c_str() + end) {
        out.values[index] = (uint32_t) value;
        out.valid |= 1UL << index;
      }
    }
    index++;
    start = end + 1;
  }
}

// Field number index of "a|b|c" from start, empty if absent.
static std::string text_field(const std::string &frame, size_t start, uint8_t index) {
  const size_t len = frame.size();
  while (start <= len) {
    size_t end = frame.find('|', start);
    if (end == std::string::npos)
      end = len;
    if (index == 0)
      return frame.substr(start, end - start);
    index--;
    start = end + 1;
  }
  return "";
}

// The stove counts seconds since 1970 on its own clock: the date is shown as the stove sees it.
static bool format_timestamp(uint32_t timestamp, char *buf, size_t size) {
  if (timestamp == 0 || timestamp == 0xFFFFFFFFUL)
    return false;
  time_t t = (time_t) timestamp;
  struct tm tm;
  if (gmtime_r(&t, &tm) == nullptr)
    return false;
  snprintf(buf, size, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
           tm.tm_min);
  return true;
}

void MczMaestro::publish_text_(MczTextKind kind, const std::string &text) {
#ifdef USE_TEXT_SENSOR
  auto *sensor = this->text_sensors_[kind];
  if (sensor == nullptr)
    return;
  if (sensor->has_state() && sensor->get_state() == text)
    return;
  sensor->publish_state(text);
#endif
}

void MczMaestro::handle_aux_(MczAuxKind kind) {
  this->aux_[kind].received = true;

  if (kind == AUX_VERSIONS) {
    this->handle_versions_();
    return;
  }
  if (kind == AUX_ALARMS) {
    this->handle_alarms_();
    return;
  }

  MczValues values;
  if (kind == AUX_PARAMS) {
    // One value per byte, written as a single run of hexadecimal digits
    const size_t len = this->rx_.size();
    for (uint8_t i = 0; i < MCZ_MAX_VALUES && 3 + 2 * (size_t) i + 1 < len; i++) {
      const char hex[3] = {this->rx_[3 + 2 * i], this->rx_[4 + 2 * i], 0};
      if (!isxdigit((unsigned char) hex[0]) || !isxdigit((unsigned char) hex[1]))
        break;
      values.values[i] = (uint32_t) strtoul(hex, nullptr, 16);
      values.valid |= 1UL << i;
    }
  } else {
    parse_hex_fields(this->rx_, 3, values);
  }

#ifdef USE_SENSOR
  for (auto &entry : this->aux_sensors_) {
    if (entry.kind != kind || !values.has(entry.index))
      continue;
    float value = values.get(entry.index);
    if (entry.conv == CONV_OPT && value == 255)
      value = NAN;
    entry.sensor->publish_state(value);
  }
#endif
#ifdef USE_BINARY_SENSOR
  for (auto &entry : this->aux_binary_sensors_) {
    if (entry.kind == kind && values.has(entry.index))
      entry.sensor->publish_state(values.get(entry.index) != 0);
  }
#endif
#ifdef USE_TEXT_SENSOR
  if (kind == AUX_PROBES && values.has(2)) {
    char buf[24];
    if (format_timestamp(values.get(2), buf, sizeof(buf))) {
      this->publish_text_(TEXT_PROBE_LAST_SEEN, buf);
    } else {
      this->publish_text_(TEXT_PROBE_LAST_SEEN, MCZ_T("Never", "Jamais"));
    }
  }
#endif

  for (auto *listener : this->listeners_)
    listener->on_aux(kind, values);
}

void MczMaestro::handle_versions_() {
#ifdef USE_TEXT_SENSOR
  // 0E|serial|firmware|database name (hex)|database revision (hex)|direct SSID|direct password|
  // direct version|remote MAC|remote version|bootloader|probe version|baud 1|baud 2
  auto field = [this](uint8_t index) {
    std::string text = text_field(this->rx_, 3, index);
    if (text.size() > 32)
      text.resize(32);
    return text;
  };
  this->publish_text_(TEXT_SERIAL_NUMBER, field(0));
  this->publish_text_(TEXT_WIFI_DIRECT, field(6));
  this->publish_text_(TEXT_WIFI_REMOTE, field(8));
  this->publish_text_(TEXT_BOOTLOADER, field(9));
  this->publish_text_(TEXT_WIFI_PROBE, field(10));

  // Database name: ASCII text written in hexadecimal
  const std::string hex = text_field(this->rx_, 3, 2);
  std::string name;
  for (size_t i = 0; i + 1 < hex.size() && name.size() < 32; i += 2) {
    const char pair[3] = {hex[i], hex[i + 1], 0};
    const char c = (char) strtoul(pair, nullptr, 16);
    if (c >= 32 && c < 127)
      name.push_back(c);
  }
  while (!name.empty() && name.back() == ' ')
    name.pop_back();
  while (!name.empty() && name.front() == ' ')
    name.erase(name.begin());
  this->publish_text_(TEXT_DATABASE_NAME, name);

  const std::string revision = text_field(this->rx_, 3, 3);
  if (!revision.empty()) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", strtoul(revision.c_str(), nullptr, 16));
    this->publish_text_(TEXT_DATABASE_REVISION, buf);
  }
#endif
}

void MczMaestro::handle_alarms_() {
#ifdef USE_TEXT_SENSOR
  // 0A|state|timestamp|state|timestamp|... ; an unused entry has the timestamp ffffffff
  MczValues values;
  parse_hex_fields(this->rx_, 3, values);
  struct Alarm {
    uint32_t state;
    uint32_t timestamp;
  };
  Alarm alarms[MCZ_MAX_VALUES / 2];
  size_t count = 0;
  for (uint8_t i = 0; i + 1 < MCZ_MAX_VALUES; i += 2) {
    if (!values.has(i) || !values.has(i + 1) || values.get(i + 1) == 0xFFFFFFFFUL)
      continue;
    alarms[count++] = {values.get(i), values.get(i + 1)};
  }
  // Most recent first
  for (size_t i = 1; i < count; i++) {
    for (size_t j = i; j > 0 && alarms[j].timestamp > alarms[j - 1].timestamp; j--)
      std::swap(alarms[j], alarms[j - 1]);
  }

  if (count == 0) {
    this->publish_text_(TEXT_LAST_ALARM, MCZ_T("None", "Aucune"));
    this->publish_text_(TEXT_ALARM_HISTORY, MCZ_T("None", "Aucune"));
    return;
  }

  char date[24];
  std::string last = state_text(alarms[0].state);
  if (format_timestamp(alarms[0].timestamp, date, sizeof(date))) {
    last += " (";
    last += date;
    last += ")";
  }
  this->publish_text_(TEXT_LAST_ALARM, last);

  // Short form, to stay within the 255 characters of a Home Assistant state
  std::string history;
  for (size_t i = 0; i < count && i < MAX_ALARMS; i++) {
    if (i != 0)
      history += ", ";
    const char *text = state_text(alarms[i].state);
    const char *dash = strstr(text, " - ");
    history.append(text, dash != nullptr ? (size_t) (dash - text) : strlen(text));
    if (format_timestamp(alarms[i].timestamp, date, sizeof(date))) {
      history += " ";
      history += date;
    }
  }
  this->publish_text_(TEXT_ALARM_HISTORY, history);
#endif
}

void MczMaestro::handle_info_() {
  MczInfo &info = this->info_;
  info.valid = 0;
  uint8_t field = 0;
  size_t start = 0;
  const size_t len = this->rx_.size();
  while (start <= len && field < MCZ_MAX_FIELDS) {
    size_t end = this->rx_.find('|', start);
    if (end == std::string::npos)
      end = len;
    if (end > start) {
      char *stop = nullptr;
      const char *text = this->rx_.c_str() + start;
      unsigned long value = strtoul(text, &stop, 16);
      if (stop == this->rx_.c_str() + end) {
        info.values[field] = (uint32_t) value;
        info.valid |= 1ULL << field;
      }
    }
    field++;
    start = end + 1;
  }

  if (info.has(FIELD_STATE)) {
    // The alarm history is read again when an alarm appears or is cleared
    const uint32_t state = info.get(FIELD_STATE);
    const int8_t alarm = state >= 50 && state <= 67 ? 1 : 0;
    if (this->alarm_active_ != -1 && alarm != this->alarm_active_)
      this->refresh_aux(AUX_ALARMS);
    this->alarm_active_ = alarm;
  }

  if (info.has(FIELD_CONTROL_MODE)) {
    uint32_t mode = info.get(FIELD_CONTROL_MODE);
    if (mode <= 1) {
      this->control_mode_ = (int8_t) mode;
    } else {
      ESP_LOGW(TAG, "Unknown regulation mode: %u", (unsigned) mode);
    }
  }

#ifdef USE_SENSOR
  for (auto &entry : this->sensors_) {
    if (!info.has(entry.field))
      continue;
    float value = info.get(entry.field);
    switch (entry.conv) {
      case CONV_HALF:
        value /= 2.0f;
        break;
      case CONV_HALF_OPT:
        value = value == 255 ? NAN : value / 2.0f;
        break;
      case CONV_HOURS:
        value /= 3600.0f;
        break;
      case CONV_POWER:
        if (value > 10)
          value -= 10;
        break;
      default:
        break;
    }
    entry.sensor->publish_state(value);
  }
#endif

#ifdef USE_BINARY_SENSOR
  for (auto &entry : this->binary_sensors_) {
    if (!info.has(entry.field))
      continue;
    uint32_t value = info.get(entry.field);
    bool state;
    switch (entry.mode) {
      case BIN_EQUALS:
        state = value == entry.a;
        break;
      case BIN_RANGE:
        state = value >= entry.a && value <= entry.b;
        break;
      default:
        state = value != 0;
        break;
    }
    entry.sensor->publish_state(state);
  }
#endif

#ifdef USE_TEXT_SENSOR
  this->publish_texts_(info);
#endif

  for (auto *listener : this->listeners_)
    listener->on_info(info);
}

#ifdef USE_TEXT_SENSOR
void MczMaestro::publish_texts_(const MczInfo &info) {
  // Each text is published only when its source value changes
  auto changed = [this](MczTextKind kind, int32_t key) {
    if (this->text_sensors_[kind] == nullptr || this->text_cache_[kind] == key)
      return false;
    this->text_cache_[kind] = key;
    return true;
  };

  if (info.has(FIELD_STATE) && changed(TEXT_STATE, (int32_t) info.get(FIELD_STATE)))
    this->text_sensors_[TEXT_STATE]->publish_state(state_text(info.get(FIELD_STATE)));

  if (info.has(32) && info.has(33) && info.has(34) && info.has(35) && info.has(36)) {
    int32_t key = (int32_t) (info.get(33) + 60 * (info.get(32) + 24 * (info.get(34) + 32 * info.get(35))));
    if (changed(TEXT_DATETIME, key)) {
      char buf[24];
      snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u", (unsigned) info.get(36), (unsigned) info.get(35),
               (unsigned) info.get(34), (unsigned) info.get(32), (unsigned) info.get(33));
      this->text_sensors_[TEXT_DATETIME]->publish_state(buf);
    }
  }

  if (info.has(30) && changed(TEXT_FIRMWARE, (int32_t) info.get(30))) {
    // 3 bytes: major.minor.patch (0x010802 = 1.8.2)
    uint32_t v = info.get(30);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u", (unsigned) ((v >> 16) & 0xFF), (unsigned) ((v >> 8) & 0xFF),
             (unsigned) (v & 0xFF));
    this->text_sensors_[TEXT_FIRMWARE]->publish_state(buf);
  }

  if (info.has(15) && changed(TEXT_VALVE, (int32_t) info.get(15)))
    this->text_sensors_[TEXT_VALVE]->publish_state(info.get(15) == 1 ? MCZ_T("Domestic hot water", "Sanitaire")
                                                                      : MCZ_T("Heating", "Chauffage"));

  if (info.has(FIELD_PELLET_SENSOR) && changed(TEXT_PELLET, (int32_t) info.get(FIELD_PELLET_SENSOR))) {
    const char *text;
    switch (info.get(FIELD_PELLET_SENSOR)) {
      case 0:
        text = MCZ_T("No sensor", "Absent");
        break;
      case 10:
        text = MCZ_T("Level OK", "Niveau correct");
        break;
      case 11:
        text = MCZ_T("Tank empty", "Réservoir vide");
        break;
      default:
        text = MCZ_T("Unknown", "Inconnu");
        break;
    }
    this->text_sensors_[TEXT_PELLET]->publish_state(text);
  }
}
#endif

void MczMaestro::probe_tick_() {
#ifdef USE_SENSOR
  if (!this->probe_enabled_)
    return;
  const uint32_t now = millis();
  if (now < PROBE_FIRST_SEND_MS)
    return;

  for (uint8_t i = 0; i < MCZ_MAX_PROBES; i++) {
    auto &probe = this->probes_[i];
    if (probe.source == nullptr)
      continue;
    if (probe.last_send != 0 && now - probe.last_send < probe.interval_s * 1000UL)
      continue;
#ifdef USE_API
    // Without Home Assistant the source value is stale: nothing is sent
    if (probe.require_api && (api::global_api_server == nullptr || !api::global_api_server->is_connected()))
      continue;
#endif
    float temperature = probe.source->state;
    if (std::isnan(temperature))
      continue;  // source unavailable: nothing is sent
    if (temperature < 0 || temperature > 100)
      temperature = 0;  // same rule as the original probe

    int quality = 100;
#ifdef USE_WIFI
    if (wifi::global_wifi_component != nullptr) {
      int rssi = wifi::global_wifi_component->wifi_rssi();
      quality = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : 2 * (rssi + 100));
    }
#endif

    int half = (int) lroundf(temperature * 2);  // the stove works in half degrees
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%u|%d|%s|00|%d", PROBE_COMMAND, PROBE_ID_BASE + i, half, probe.version, quality);
    if (!this->send_command(buf))
      continue;  // queue full: retried at the next tick
    probe.last_send = now;
    if (probe.sent_sensor != nullptr)
      probe.sent_sensor->publish_state(half / 2.0f);
  }
#endif
}

}  // namespace esphome::mcz_maestro
