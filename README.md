# MCZ Maestro stove — ESPHome component and protocol notes

*[Version française de la documentation](mcz-maestro-documentation.md)*

Local control of an MCZ pellet stove (Maestro technology) from Home Assistant,
by replacing the firmware of the stove's cloud WiFi module with ESPHome.
No cloud, no extra hardware: the module talks to the stove mainboard over its
existing serial link.

> **Warning.** This is not an official MCZ project. A pellet stove is a
> combustion appliance: test remote commands while standing in front of it.
> Use at your own risk.

## What you get in Home Assistant

- A thermostat (on/off, setpoint, room temperature, manual/automatic mode)
- Power level, front and ducted fans, eco / silent / Active modes
- Stove state, alarms, temperatures, operating hours and other diagnostics
- A "virtual probe" that feeds any Home Assistant temperature sensor to the
  stove in place of the MCZ remote WiFi probe
- Optional entities for hydro stoves, boiler, second ducted fan and pellet sensor

## The `mcz_maestro` component

You declare only the entities your stove has; nothing else is compiled.

```yaml
external_components:
  - source: github://lexyan/mcz-maestro-esphome
    components: [ mcz_maestro ]

logger:
  baud_rate: 0              # the serial link is wired to the stove

uart:
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 115200

mcz_maestro:
  id: stove

climate:
  - platform: mcz_maestro
    name: "Thermostat"

sensor:
  - platform: mcz_maestro
    ambient_temperature:
      name: "Room temperature"
    fume_temperature:
      name: "Flue gas temperature"

text_sensor:
  - platform: mcz_maestro
    state:
      name: "State"

select:
  - platform: mcz_maestro
    fan:
      name: "Fan"
```

### Component options

| Option | Default | Role |
|---|---|---|
| `update_interval` | `15s` | Polling period of the mainboard |
| `language` | `en` | Language of the text sensors: `en` or `fr` |
| `announce` | `true` | Announce the module to the mainboard at start-up, like the original firmware |
| `module_version` | `1.2.6` | Version sent in that announcement |
| `write_guard` | `20s` | No parameter is written to the stove during this delay after boot |
| `time_id` | | Time source used by the `set_time` button |
| `virtual_probe` | | Replaces the remote WiFi probe (see below) |

```yaml
mcz_maestro:
  id: stove
  time_id: ha_time
  virtual_probe:
    temperature_sensor: room_temperature   # id of any ESPHome sensor
    probe: 1                               # 1, 2 or 3
```

Stoves with ducted outputs can have up to three WiFi probes. Declare them as a list,
one entry per probe number; each probe follows the interval the stove asks for it:

```yaml
mcz_maestro:
  id: stove
  virtual_probe:
    - temperature_sensor: room_temperature
      probe: 1
    - temperature_sensor: zone_2_temperature
      probe: 2
```

### Entities

| Platform | Keys |
|---|---|
| `climate` | one thermostat; `manual_preset` and `auto_preset` set the preset labels |
| `sensor` | `ambient_temperature`, `fume_temperature`, `power_level`, `state_code`, `board_temperature`, `fume_fan_rpm`, `auger_rpm`, `auger_rpm_set`, `active_set`, `active_live`, `active_temperature`, `profile`, `total_hours`, `hours_power_1` … `hours_power_5`, `hours_to_service`, `ignitions`, `minutes_to_switch_off`, `wifi_probe_1` … `wifi_probe_3`, `wifi_probe_signal`, `setpoint_min`, `setpoint_max`, `virtual_probe_temperature`, `virtual_probe_interval` (and `virtual_probe_2_…`, `virtual_probe_3_…` for probes 2 and 3) |
| `sensor` (hydro, pellet sensor, raw) | `puffer_temperature`, `boiler_temperature`, `ntc3_temperature`, `return_temperature`, `pump_pwm`, `pellet_sensor_code`, `modbus_address`, `database_id`, `field_51`, `field_55`, `set_puffer`, `set_boiler`, `set_health` |
| `binary_sensor` | `alarm`, `brazier_dirty`, `igniter`, `link`, `pellet_empty`, `fan_fitted`, `ducted_fan_1_fitted`, `ducted_fan_2_fitted`, `silent_mode_available` |
| `text_sensor` | `state`, `datetime`, `firmware`, `valve_3way`, `pellet_level`, `last_alarm`, `alarm_history`, `wifi_probe_last_seen`, `bootloader_version`, `wifi_direct_version`, `wifi_remote_version`, `wifi_probe_version`, `database_name`, `database_revision`, `serial_number` |
| `switch` | `power`, `eco_mode`, `silent_mode`, `active_mode`, `chronothermostat`, `sounds`, `virtual_probe`, `pellet_sensor` |
| `select` | `control_mode`, `fan`, `ducted_fan_1`, `ducted_fan_2`, `season_mode`, `air_recipe`, `pellet_recipe`, `room_input`, `wifi_probe_interval`, `wifi_probe_summer_interval`, `wifi_probe_offset` |
| `number` | `setpoint`, `power`, `boiler_setpoint`, `chrono_t1` … `chrono_t3`, `profile`, `temperature_unit`, `sleep`, `antifreeze`, `eco_stop_delay`, `eco_stop_hysteresis` |
| `button` | `refresh`, `reset_alarm`, `set_time`, `reset_service`, `reset_active`, `load_auger` |

The labels of a `select` can be translated; the keys are the values sent to the stove:

```yaml
select:
  - platform: mcz_maestro
    control_mode:
      name: "Mode de régulation"
      options: { 0: "Manuel", 1: "Automatique" }
```

From a lambda, `id(stove).send_command("C|RecuperoInfo")` sends a raw frame and
`id(stove).write_parameter(42, 43)` writes a parameter.

`air_recipe` and `pellet_recipe` are the combustion recipes of the MCZ app (air -2 to +2,
pellets -3 to +3). They are read with `C|RecuperoParametriExtra|11` and written to the
stove's database with `C|WriteBancaDati` (cells 459 and 460); verified on an Ego 2 (mainboard 1.8.2).

The eco stop settings, the room input, the WiFi probe settings, the alarm history, the
versions and what the stove is fitted with are read from other frames of the MCZ app
protocol, requested only when an entity needs them. See the documentation for details.

The virtual probe only sends while the stove's room input is set to the WiFi probe.

Complete configurations: [`examples/full.yaml`](examples/full.yaml) (every option)
and [`examples/mcz-ego2-fr.yaml`](examples/mcz-ego2-fr.yaml) (the stove this was built on, in French).

### Safeguards

- Nothing is written to the stove at start-up: every state comes from the stove.
- Writes are refused during `write_guard` after boot.
- The power setting is refused while the stove is in automatic mode.
- `load_auger` feeds pellets into the brazier: stove off and cold only.

### Status

Tested on an MCZ Ego 2 (mainboard firmware 1.8.2, no hydro module, one ducted fan).
The entities for hydro stoves, the boiler, the second ducted fan and the pellet
sensor come from the maestrogateway tables and have **not** been tested on a
stove that has them. Diagnostic commands (`C|Diagnostica`) and the factory reset
are deliberately not exposed.

## Repository contents

| Path | Content |
|---|---|
| `components/mcz_maestro/` | The ESPHome external component |
| `examples/` | Example configurations using the component |
| `mcz-maestro-documentation.en.md` | Full documentation (English) |
| `mcz-maestro-documentation.md` | Full documentation (French) |
| `esp8266_split.py` | Splits an ESP8266 flash dump into its regions |
| `images/` | Board photos: connector pinout, power vias, GPIO0 header |

## Quick start

1. Back up the original firmware of the module (see the documentation, part 5).
2. Start from one of the files in `examples/` and create `secrets.yaml`.
3. Compile with ESPHome and flash the module with esptool.
4. Add the device to Home Assistant.

The wiring, the esptool commands and the way back to the original firmware
are described in the [documentation](mcz-maestro-documentation.en.md).

## Credits

The command and field tables come from
[Chibald/maestrogateway](https://github.com/Chibald/maestrogateway).

## License

GPL-3.0. See [LICENSE](LICENSE).
