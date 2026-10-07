#include "mcz_maestro.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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
  for (auto *listener : this->listeners_)
    listener->on_hub_setup();
}

void MczMaestro::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MCZ Maestro:\n"
                "  Announce at start-up: %s\n"
                "  Write guard: %u ms\n"
                "  Listeners: %u",
                YESNO(this->announce_), (unsigned) this->write_guard_ms_, (unsigned) this->listeners_.size());
#ifdef USE_SENSOR
  if (this->probe_source_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Virtual WiFi probe: number %u, version %s", this->probe_id_, this->probe_version_);
  }
#endif
  LOG_UPDATE_INTERVAL(this);
}

void MczMaestro::update() { this->request_info(); }

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
    this->rx_.clear();
  }

  if (!this->busy_) {
    if (this->tx_queue_.empty() && this->refresh_pending_) {
      this->refresh_pending_ = false;
      this->request_info();
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
  this->last_was_probe_ = cmd.rfind("C|RecuperaTemperaturaWiFi", 0) == 0;
  this->last_was_announce_ = cmd.rfind("RispostaAccensione", 0) == 0;
  this->last_was_write_ = cmd.rfind("C|WriteParametri", 0) == 0;
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
  ESP_LOGD(TAG, "RX: %s", this->rx_.c_str());

  if (this->last_was_probe_) {
    // Reply to the WiFi probe frame: 2 hex digits = minutes before the next transmission
    this->last_was_probe_ = false;
    if (this->rx_.size() >= 2 && isxdigit((unsigned char) this->rx_[0]) && isxdigit((unsigned char) this->rx_[1])) {
      char hex[3] = {this->rx_[0], this->rx_[1], 0};
      int minutes = (int) strtol(hex, nullptr, 16);
#ifdef USE_SENSOR
      if (this->probe_interval_sensor_ != nullptr)
        this->probe_interval_sensor_->publish_state(minutes);
#endif
      if (minutes < 1)
        minutes = 1;
      if (minutes > 30)
        minutes = 30;
      this->probe_interval_s_ = minutes * 60;
    }
    return;
  }

  // Only the information frame (type 01) is decoded
  const bool after_write = this->last_was_write_;
  this->last_was_write_ = false;
  if (this->rx_.size() >= 3 && this->rx_[0] == '0' && this->rx_[1] == '1' && this->rx_[2] == '|') {
    // The mainboard answers a write with the updated information frame: when it is the
    // last write of a burst, the refresh that would follow is not needed.
    if (after_write && this->tx_queue_.empty())
      this->refresh_pending_ = false;
    this->handle_info_();
  }
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
  if (this->probe_source_ == nullptr || !this->probe_enabled_)
    return;
  const uint32_t now = millis();
  if (now < PROBE_FIRST_SEND_MS)
    return;
  if (this->probe_last_send_ != 0 && now - this->probe_last_send_ < this->probe_interval_s_ * 1000UL)
    return;
#ifdef USE_API
  // Without Home Assistant the source value is stale: nothing is sent
  if (this->probe_require_api_ && (api::global_api_server == nullptr || !api::global_api_server->is_connected()))
    return;
#endif
  float temperature = this->probe_source_->state;
  if (std::isnan(temperature))
    return;  // source unavailable: nothing is sent
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
  snprintf(buf, sizeof(buf), "C|RecuperaTemperaturaWiFi|%u|%d|%s|00|%d", this->probe_id_, half, this->probe_version_,
           quality);
  this->send_command(buf);
  this->probe_last_send_ = now;
  if (this->probe_sent_sensor_ != nullptr)
    this->probe_sent_sensor_->publish_state(half / 2.0f);
#endif
}

}  // namespace esphome::mcz_maestro
