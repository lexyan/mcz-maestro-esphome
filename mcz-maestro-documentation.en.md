# MCZ Maestro stove: WiFi modules, protocol and ESPHome firmware

*[Version française](mcz-maestro-documentation.md)*

This document summarises the analysis of the firmware in the WiFi modules of an MCZ pellet stove (Maestro technology) and their replacement with ESPHome, for direct control from Home Assistant.

It covers:

1. the hardware architecture and the exchanges between the parts;
2. the results of decompiling the three original firmwares;
3. the serial protocol of the mainboard;
4. the connections needed to reprogram the ESP8266 chips;
5. reprogramming with esptool;
6. the ESPHome configuration.

> **Warning.** This information comes from firmware analysis and a community project, not from manufacturer documentation. A pellet stove is a combustion appliance: run your first remote-control tests while standing in front of the stove.

Entities are referred to by their configuration key; their names are free.

---

## 1. Overview

### The parts

| Part | Chip | Original firmware | Role |
|---|---|---|---|
| WiFi module 1 (local) | ESP-WROOM-02 (ESP8266, 2 MB) | Fluidsoft 1.2.4 | WiFi access point of the stove and websocket server |
| WiFi module 2 (cloud) | ESP-WROOM-02 (ESP8266, 2 MB) | Fluidsoft "MCZ-RemoteService" 1.2.5 | Connection to the home WiFi and to the remote server |
| Remote room probe | ESP8266, 2 MB | Fluidsoft "MCZ-Sensor" 1.3.1 | Measures the room temperature and sends it to the stove |
| Stove mainboard | — | 1.8.2 on the stove studied | Interprets every command |

The two WiFi modules are two separate ESP8266 chips mounted on the same board, each connected to the mainboard through its own serial link. The mainboard also controls their power-up, one after the other (see part 4).

### Who talks to whom

```
App (stove WiFi) ────────────┐
                             ├─ websocket :81 ─► WiFi module 1 ─ serial ─┐
Room probe ──────────────────┘                                           ├─► Mainboard
                                                                         │
Remote server ◄─ socket.io ─► WiFi module 2 ──────────────── serial ─────┘
```

Key point: **the WiFi modules only relay**. None of the three firmwares interprets the content of the commands. All the meaning of the protocol lives in the mainboard.

### Tools used

- Splitting the dumps: `esp8266_split.py` script.
- Decompilation: Ghidra 11.3 (Xtensa processor), after converting the application image to an ELF file with the ESP8266 ROM symbols.
- The three firmwares are built with the ESP8266 Arduino core 2.3.0 (SDK 1.5.3).

### Flash layout (2 MB)

| Address | Content |
|---|---|
| `0x000000` | Bootloader (eboot from the Arduino core) |
| `0x001000` | Application |
| `0x1FB000` | Application EEPROM (probe and cloud module) |
| `0x1FC000` | RF calibration data |
| `0x1FD000` – `0x1FF000` | SDK WiFi configuration |

The dumps also contain unused leftovers of the factory Espressif AT firmware.

> **Privacy.** The dump of the cloud module contains the name and password of the home WiFi, in clear text in the EEPROM. Do not share that file as is.

---

## 2. Decompilation results

### 2.1 Room probe "MCZ-Sensor" 1.3.1

**Role.** Battery-powered probe that wakes up periodically, measures the temperature, sends it to the stove, then goes back to sleep.

**Hardware.**

| Pin | Use |
|---|---|
| GPIO4 / GPIO5 | I2C (SDA / SCL) to the NCT75 sensor, address `0x48` |
| GPIO14 | Sensor power supply, switched on only during the measurement |
| GPIO13 | Reset button (active when grounded at startup) |
| GPIO2 | Configured as an output but never driven |

**One cycle.**

1. Power the NCT75, read 2 bytes, convert to °C (0.0625 °C per step), cut the power. A negative value or one above 100 °C is replaced by 0.
2. Connect to the stove access point with the fixed IP `192.168.120.51`.
3. Open a websocket to `192.168.120.1`, port 81, sub-protocol `arduino`.
4. Send a single text frame (see below).
5. Receive the reply: two hexadecimal characters giving the number of minutes until the next wake-up.
6. Go into deep sleep. With no reply within 5 seconds, sleep for the last known interval (15 minutes by default).

**Frame sent.**

```
C|RecuperaTemperaturaWiFi|<probe>|<T>|<version>|<field5>|<quality>
```

| Field | Value | Origin |
|---|---|---|
| `<probe>` | `51`, `52` or `53` | Probe number: `51` = probe 1, `52` = probe 2, `53` = probe 3. The firmware studied sends `51`, hard-coded |
| `<T>` | temperature × 2, integer | 21.5 °C gives `43` |
| `<version>` | `1.3.1` | Probe firmware version |
| `<field5>` | `00` | Never modified; role unknown |
| `<quality>` | 0 to 100 | 0 if RSSI ≤ -100 dBm, 100 if ≥ -50 dBm, otherwise `2 × (RSSI + 100)` |

The mainboard does not check the version number (observed in use). It only works in half degrees.

**Pairing.**

- With no saved network, the probe opens an access point named "MCZ-Sensor" with a "Select your Stove" page for 8 minutes.
- On the first contact after pairing, it also sends `C|WriteBancaDati|356|1|FF`, probably to enable the WiFi probe on the stove side.
- Grounding GPIO13 at startup erases the configuration.

**EEPROM.**

| Address | Content |
|---|---|
| `0x00` – `0x1F` | Stove SSID |
| `0x20` – `0x3F` | Password |
| `0x40` | Pairing flag (`B` = just paired, `A` = already announced) |
| `0x41` – `0x42` | Last sleep interval received, in hexadecimal |

### 2.2 WiFi module 1 (local), firmware 1.2.4

**Role.** WiFi access point of the stove and websocket gateway to the serial link of the mainboard. The app in direct mode and the room probe both connect to it.

**Network.**

- SSID: `MCZ-01` followed by the access point MAC address in uppercase hexadecimal.
- Password: derived from the SSID (see box).
- Module address: `192.168.120.1`.
- DHCP: addresses `.100` to `.200`, 4 clients at most.
- Captive portal on port 80, showing only "Connesso con successo alla stufa".

> **Password derivation.** The password is the concatenation of the decimal ASCII codes of the SSID characters at positions 15, 13, 17, 12 and 16 (counting from 0).
> Fictional example: for `MCZ-01AABBCCDDEEFF`, those characters are `E`, `D`, `F`, `D`, `F`, i.e. `69`, `68`, `70`, `68`, `70`, giving the password `6968706870`.

**Gateway.**

- Websocket server on port 81, sub-protocol `arduino`, 5 clients at most.
- A text frame starting with `C` is forwarded unchanged to the mainboard, followed by `^` and a line break.
- The reply is read up to the `^`, every `%7C` is replaced by `|`, then it is sent back to the requesting client only.
- A frame starting with `P` is a keep-alive and gets no reply. Everything else is ignored.

**Other behaviour.**

- At startup, then after 30 seconds without traffic, it sends `RispostaAccensioneDirect|<ssid>|<password>|1.2.4^` to the mainboard.
- Every 20 seconds, it sends a websocket ping to the clients. A client at `.51` to `.60` (the probe range) that does not answer is disconnected; a DHCP client silent for 20 seconds causes the module to restart.
- Serial speed: 115200 baud by default. The `CambioBaudSerial` command allows 9600, 38400 or 57600; the value is kept in EEPROM.
- LEDs: GPIO13 blinks at 1 Hz; GPIO12 is set to 0 at startup and never changed, which keeps its LED on (it is active low).

### 2.3 WiFi module 2 (cloud), firmware "MCZ-RemoteService" 1.2.5

**Role.** Remote access. The module connects to the home WiFi, then to a socket.io server, and relays the server's commands to the mainboard.

**Sequence.**

1. Send `RispostaAccensioneRemoto|<MAC>|1.2.5^` to the mainboard.
2. Connect to the WiFi network saved in EEPROM. With no network, open an unprotected access point named "MCZ-RemoteService" with a network selection page.
3. Ask the mainboard for the server address with `RecuperoSerialeIP^`. The reply has four fields separated by `|`; the last three are the serial number, the host and the port. **The server address is therefore not in the firmware.**
4. Connect with socket.io (path `/socket.io/?EIO=3`) and send the `join` event with the serial number, the MAC address, the type `stove` and the revision.
5. For each command received: the server sends a JSON object with `richiesta`, `socketChiamata` and `idChiamata`. The content of `richiesta` goes to the mainboard followed by `^`; the reply is returned in a `rispondo` event, with both identifiers copied back.

**Other behaviour.**

- socket.io ping every 25 seconds. Restart if the WiFi drops.
- After 30 seconds without a message from the server, a new serial announcement to the mainboard.
- Serial speed: same mechanism as the local module, stored at EEPROM address `0x46`.
- LEDs: GPIO13 blinks during the configuration portal, then follows the connection state; GPIO12 is set to 0 at startup, which turns its LED on.

**EEPROM.**

| Address | Content |
|---|---|
| `0x00` – `0x1F` | Home WiFi SSID |
| `0x20` – `0x3F` | WiFi password, in clear text |
| `0x40` | Flag written by the configuration portal |
| `0x46` – `0x49` | Serial speed divided by 2, in hexadecimal |

### 2.4 Limits of the analysis

- Function and variable names do not exist in the binaries; the logic was read from the pseudo-C produced by Ghidra.
- A few points are interpretations rather than direct readings: the details of the LED blinking, the erasing of the WiFi configuration when the local module starts, the restart of the cloud module when the WiFi drops.
- Whether the connection to the remote server is encrypted was not checked.
- The role of the first field of the `RecuperoSerialeIP` reply is unknown.

---

## 3. Mainboard serial protocol

**Link.** Main UART of the ESP8266 (TX on GPIO1, RX on GPIO3), 115200 baud.

**Format.**

- Sent: `<command>^` followed by a line break.
- Reply: fields separated by `|`, terminated by `^`. The separator may arrive encoded as `%7C`.
- The values in the information frame are hexadecimal.

### Commands

| Command | Effect |
|---|---|
| `C\|RecuperoInfo` | Requests the information frame (type `01`) |
| `C\|WriteParametri\|<no.>\|<value>` | Writes a parameter |
| `C\|SalvaDataOra\|ddmmyyyyHHMM` | Sets the date and time |
| `C\|RecuperaTemperaturaWiFi\|<probe>\|…` | Temperature from a WiFi probe, `<probe>` being 51, 52 or 53 for probes 1 to 3 (see 2.1) |
| `C\|WriteBancaDati\|356\|1\|FF` | Sent by the probe on its first contact |
| `CambioBaudSerial` | Changes the serial speed |
| `RecuperoSerialeIP` | Serial number and remote server address |

### Write parameters used

| No. | Function | Values |
|---|---|---|
| 34 | On / off | `1` = turn on, `40` = turn off |
| 42 | Temperature setpoint | temperature × 2 |
| 36 | Power | `11` to `15` for power levels 1 to 5 |
| 37 | Front fan | `0` = off ("No Air"), 1 to 5 = manual speed, `6` = automatic |
| 38 | Ducted fan 1 | `0` = off ("No Air"), 1 to 5 = manual speed, `6` = automatic |
| 35 | Active mode | 0 / 1 |
| 40 | Regulation mode | `0` = manual, `1` = automatic |
| 41 | Eco mode | 0 / 1 |
| 45 | Silent mode | 0 / 1 |
| 50 | Sounds | 0 / 1 |
| 1111 | Chronothermostat | 0 / 1 |
| 1 | Alarm acknowledgement | `255` |
| 43 | Service counter reset | `0` |

### Information frame fields used

| Field | Content | Conversion |
|---|---|---|
| 1 | Stove state | code (see below) |
| 2 | Front fan | 0 = off ("No Air"), 1 to 5 = manual speed, 6 = automatic |
| 3 | Ducted fan 1 | 0 = off ("No Air"), 1 to 5 = manual speed, 6 = automatic |
| 5 | Flue gas temperature | °C |
| 6 | Room temperature | ÷ 2 |
| 10 | Igniter | 0 = off |
| 11 | Active, setpoint | no unit |
| 12 | Flue gas extractor | rpm |
| 13 | Auger, setpoint | rpm |
| 14 | Auger, actual | rpm |
| 17 | Brazier | 0 = clean |
| 18 | Profile | code |
| 20 | Active mode | 0 / 1 |
| 21 | Active, measured | no unit |
| 22 | Regulation mode | 0 = manual, 1 = automatic |
| 23 | Eco mode | 0 / 1 |
| 24 | Silent mode | 0 / 1 |
| 25 | Chronothermostat | 0 / 1 |
| 26 | Setpoint | ÷ 2 |
| 28 | Mainboard temperature | ÷ 2 |
| 29 | Power | 11 to 15 for power levels 1 to 5 |
| 30 | Mainboard firmware | 3 bytes: `0x010802` = 1.8.2 |
| 32 – 36 | Hour, minute, day, month, year | |
| 37 | Total operating time | seconds |
| 38 – 42 | Time at power level 1 to 5 | seconds |
| 43 | Hours before service | hours |
| 45 | Number of ignitions | |
| 46 | Active, temperature | no unit |
| 49 | Sounds | 0 / 1 |
| 52 | WiFi probe 1 temperature | ÷ 2 |

### Stove states

| Code | State | Code | State |
|---|---|---|---|
| 0 | Off | 31 | On |
| 1 | Checking hot or cold | 40 | Extinguishing |
| 2 | Cleaning, cold | 41 | Cooling |
| 3 | Loading pellets, cold | 42 – 43 | Cleaning low / high |
| 4 – 5 | Start 1 / 2, cold | 44 | Unlocking auger |
| 6 | Cleaning, hot | 45 | Auto eco |
| 7 | Loading pellets, hot | 46 | Standby |
| 8 – 9 | Start 1 / 2, hot | 49 | Loading auger |
| 10 | Stabilising | 50 – 67 | Alarms A01 to A23 |
| 11 – 15 | Power 1 to 5 | 69 | Waiting for safety alarms |
| 30, 48 | Diagnostics | | |

The complete table of fields, commands and alarms comes from the [Chibald/maestrogateway](https://github.com/Chibald/maestrogateway) project.

---

## 4. Connections for reprogramming

Both ESP8266 chips are on the same board. Reprogramming uses three access points: the module connector (serial link), a 2-pin header (programming mode) and two vias (power for the ESP being programmed).

### Board power supply

In normal operation, the ESP8266 chips are not powered directly from the connector:

- the +5 V of the connector feeds an AMS1117 regulator, which produces the 3.3 V of the board;
- each ESP8266 receives this 3.3 V through a transistor;
- the mainboard drives these transistors through other pins of the connector, and powers the two ESP8266 chips one after the other at startup, to avoid a large inrush current.

Consequence for reprogramming: **the +5 V of the connector is not used**. Without the mainboard to drive the transistors, it would not power any ESP8266. Only the ESP8266 being programmed is powered, at 3.3 V, through two vias on the board.

### WiFi module connector

The connector is a two-row header. Pin 1 is on the side of WiFi module 2 and of the 2 LEDs (marking `J5 WIFI2` on the board). Odd pins are on one row, even pins on the other.

| Pin | Signal | Module | Used for programming |
|---|---|---|---|
| 1 | TX | WiFi module 1 (local) | yes, for module 1 |
| 2 | RX | WiFi module 1 (local) | yes, for module 1 |
| 3 | TX | WiFi module 2 (cloud) | yes, for module 2 |
| 4 | RX | WiFi module 2 (cloud) | yes, for module 2 |
| 5 | GND | | yes |
| 6 | +5 V | | no |

The other pins, including those through which the mainboard controls the power of the ESP8266 chips, are not listed here.

![Pinout of the WiFi module connector](images/Connecteur.png)

*Figure 1 — WiFi module connector, solder side and header side: TX and RX of both modules, ground and position of pin 1.*

![Overview of the WiFi board](images/carte-vue-ensemble.png)

*Figure 2 — WiFi board, both sides. Component side: module 1 ("Local", marking `J3 WIFI1`) and module 2 ("Cloud", marking `J5 WIFI2`). Opposite side: the header of the connector to the mainboard.*

### Power vias

The board has one via per ESP8266. Each via bypasses the transistor and goes straight to the 3V3 pin of its ESP8266: 3.3 V is applied there to power only the module being programmed.

| Via | Location on the board |
|---|---|
| 3.3 V of WiFi module 1 (local) | above the module, near the `J3 WIFI1` marking |
| 3.3 V of WiFi module 2 (cloud) | near capacitors C6 and C9, next to the `J5 WIFI2` marking |

![3.3 V power vias of both modules](images/vias-3v3-esp.png)

*Figure 3 — 3.3 V power vias: "WiFi Direct" for module 1, "WiFi Cloud" for module 2.*

### 2-pin header (programming mode)

This header sits between WiFi module 1 and the main connector, near the `ROA` marking.

| Pin | Signal |
|---|---|
| WiFi module 1 side | GND |
| Main connector side | GPIO0 of both ESP8266 chips |

Bridging these two pins grounds GPIO0 on both ESP8266 chips. Only the one powered through its via starts in programming mode; the other one is not powered and is left untouched.

![2-pin GND / GPIO0 header](images/GPIO0.png)

*Figure 4 — 2-pin header: GND and GPIO0.*

### Wiring to the USB-serial adapter

To reprogram **WiFi module 2 (cloud)**:

| WiFi board | USB-serial adapter |
|---|---|
| Pin 3 (TX of module 2) | RX |
| Pin 4 (RX of module 2) | TX |
| Pin 5 (GND) | GND |
| Power via of module 2 | 3.3 V |

For WiFi module 1 (local), use pin 1 (TX), pin 2 (RX) and the power via of module 1.

**Precautions.**

- Unplug the board from the stove before connecting it to the adapter.
- TX and RX are crossed: the TX of the board goes to the RX of the adapter.
- Supply 3.3 V only, and set the adapter to 3.3 V logic levels. Never apply 5 V to a via.
- An ESP8266 draws peaks of several hundred milliamps when using WiFi. If the adapter cannot supply enough current at 3.3 V, programming fails at random: in that case use a separate 3.3 V supply, sharing its ground with the adapter.

---

## 5. Reprogramming with esptool

Replace `COM3` with the port of the adapter (`/dev/ttyUSB0` on Linux). With an esptool version older than 5, the commands are written with an underscore (`read_flash`, `erase_flash`, `write_flash`, `--flash_mode`, `--flash_size`).

### Entering programming mode

1. Fit the jumper between GND and GPIO0.
2. Apply 3.3 V to the via of the ESP8266 to be programmed.
3. The jumper can stay in place for all esptool operations. After each command, cut and restore the 3.3 V to return to programming mode.

### Checking the connection

```
esptool --port COM3 flash-id
```

### Backing up the original firmware

Do this before any write. It is the only way to go back.

```
esptool --port COM3 --baud 115200 read-flash 0 0x200000 backup-wifi2.img
```

### Erasing the flash

```
esptool --port COM3 erase-flash
```

Erasing removes the leftovers of the old firmware, including the WiFi password stored in clear text, and prevents ESPHome from starting on old settings.

### Writing the ESPHome firmware

On ESP8266, ESPHome produces a single file, to be written at address `0x0`. It contains only the program (about 470 kB), not a complete flash image.

```
esptool --port COM3 --baud 115200 write-flash --flash-mode dout --flash-size 2MB 0x0 firmware.bin
```

The `dout` mode works with every flash chip.

### Restarting

1. Cut the 3.3 V.
2. Remove the GND–GPIO0 jumper.
3. Restore the 3.3 V for a bench test, or refit the board on the stove: the module joins the WiFi and appears in Home Assistant. If it cannot connect, it opens a fallback access point.

No message appears on the serial port: logging is disabled there, because the link is reserved for the stove. Logs go over WiFi. Later updates are done over WiFi (OTA).

### Going back to the original firmware

```
esptool --port COM3 write-flash --flash-mode dout --flash-size 2MB 0x0 backup-wifi2.img
```

---

## 6. ESPHome configuration

The firmware of WiFi module 2 (cloud) is replaced with ESPHome, using the `mcz_maestro` external component of the repository (`components/mcz_maestro` folder). WiFi module 1 (local) is not modified: the app in direct mode and the stove access point keep working.

Two complete configurations are provided:

- `examples/mcz-ego2-fr.yaml`: the one of the stove studied, with French entity names;
- `examples/full.yaml`: every available entity.

### Principle

- The module connects to the home WiFi and to Home Assistant through the native ESPHome API.
- It polls the mainboard with `C|RecuperoInfo` every 15 seconds, and once after each write or series of writes.
- Only one command is in progress at a time on the serial link, with a 2-second timeout.
- At startup, it sends the announcement `RispostaAccensioneRemoto|<MAC>|<version>`, like the original firmware.
- Only the wanted entities are declared: those that are not listed are neither compiled nor exposed in Home Assistant.

### Declaring the component

```yaml
external_components:
  - source: github://lexyan/mcz-maestro-esphome
    components: [ mcz_maestro ]

logger:
  baud_rate: 0          # the serial link is reserved for the stove

uart:
  tx_pin: GPIO1
  rx_pin: GPIO3
  baud_rate: 115200

mcz_maestro:
  id: stove
  time_id: ha_time
  virtual_probe:
    temperature_sensor: room_temperature
    probe: 1
```

| Option | Default | Role |
|---|---|---|
| `update_interval` | `15s` | Polling period of the mainboard |
| `language` | `en` | Language of the published texts (state, valve, pellets): `en` or `fr` |
| `announce` | `true` | Announce the module to the mainboard at startup |
| `module_version` | `1.2.6` | Version sent in that announcement |
| `write_guard` | `20s` | Delay after startup during which no write is sent |
| `time_id` | | Time source used by the `set_time` button |
| `virtual_probe` | | Virtual probe (see below) |

Secrets expected by the examples, in `secrets.yaml`: `wifi_ssid`, `wifi_password`, `esphome_encryption_key`, `ap_wifi_password`. OTA updates are encrypted with the API key.

### Hardware

| Item | Setting |
|---|---|
| Board | `esp_wroom_02` (generic ESP8266, 2 MB) |
| Serial link | TX GPIO1, RX GPIO3, 115200 baud |
| Serial logging | Disabled (`baud_rate: 0`) |
| Status LED | GPIO13, active low. Off in normal operation, blinking on a warning or an error |
| GPIO12 LED | Active low: set to 0 at startup, so it is on. Not exposed in Home Assistant |

### Entities

Each entity is declared by its key, under the matching platform:

```yaml
sensor:
  - platform: mcz_maestro
    ambient_temperature:
      name: "Room temperature"
```

The name is free. The "Field" and "Parameter" columns refer to part 3.

**Thermostat (`climate`)**

A single entity, without a key: off / heat, setpoint, room temperature and two presets for the regulation mode. The `manual_preset` and `auto_preset` options set their labels ("Manual" and "Auto" by default). Parameters 34, 42 and 40.

**Sensors (`sensor`)**

| Key | Content | Field |
|---|---|---|
| `ambient_temperature` | Room temperature | 6 |
| `fume_temperature` | Flue gas temperature | 5 |
| `power_level` | Actual power level, 1 to 5 | 29 |
| `state_code` | Stove state (code) | 1 |
| `board_temperature` | Mainboard temperature | 28 |
| `fume_fan_rpm` | Flue gas extractor | 12 |
| `auger_rpm`, `auger_rpm_set` | Auger, actual and setpoint | 14, 13 |
| `active_set`, `active_live`, `active_temperature` | Active values, no unit | 11, 21, 46 |
| `profile` | Profile (code) | 18 |
| `total_hours` | Operating hours | 37 |
| `hours_power_1` to `hours_power_5` | Hours per power level | 38 to 42 |
| `hours_to_service` | Hours before service | 43 |
| `ignitions` | Number of ignitions | 45 |
| `minutes_to_switch_off` | Minutes to switch-off | 44 |
| `wifi_probe_1` to `wifi_probe_3` | WiFi probes as read back by the stove | 52 to 54 |
| `virtual_probe_temperature` | Last temperature sent by the virtual probe | |
| `virtual_probe_interval` | Interval requested by the stove, in minutes | |

**Binary sensors (`binary_sensor`)**

| Key | Content | Field |
|---|---|---|
| `alarm` | Alarm (state 50 to 67) | 1 |
| `brazier_dirty` | Brazier needs cleaning | 17 |
| `igniter` | Igniter | 10 |
| `link` | The mainboard answers on the serial link | |

**Texts (`text_sensor`)**

| Key | Content | Field |
|---|---|---|
| `state` | Stove state in plain text | 1 |
| `datetime` | Stove date and time | 32 to 36 |
| `firmware` | Mainboard firmware | 30 |

**Switches (`switch`)**

| Key | Content | Field | Parameter |
|---|---|---|---|
| `power` | On / off | 1 | 34 |
| `eco_mode` | Eco mode | 23 | 41 |
| `silent_mode` | Silent mode | 24 | 45 |
| `active_mode` | Active mode | 20 | 35 |
| `chronothermostat` | Chronothermostat | 25 | 1111 |
| `sounds` | Sounds | 49 | 50 |
| `virtual_probe` | Enables the virtual probe | | |

**Selects (`select`)**

| Key | Content | Field | Parameter |
|---|---|---|---|
| `control_mode` | Regulation mode | 22 | 40 |
| `fan` | Front fan | 2 | 37 |
| `ducted_fan_1` | Ducted fan 1 | 3 | 38 |

The `options` option maps each value sent to the stove to a label, so that labels can be translated:

```yaml
select:
  - platform: mcz_maestro
    control_mode:
      name: "Mode de régulation"
      options: { 0: "Manuel", 1: "Automatique" }
```

**Numbers (`number`)**

| Key | Content | Field | Parameter |
|---|---|---|---|
| `setpoint` | Setpoint, 5 to 35 °C in steps of 0.5 | 26 | 42 |
| `power` | Power setting, 1 to 5 (sent as 11 to 15), refused in automatic mode | 29 | 36 |

**Buttons (`button`)**

| Key | Content | Parameter |
|---|---|---|
| `refresh` | Requests the information frame | |
| `reset_alarm` | Acknowledges the alarm | 1 = `255` |
| `set_time` | Sets the stove clock (`time_id` option of the component) | |
| `reset_service` | Resets the service counter ("hours before service") | 43 = `0` |

**Entities depending on the stove configuration**

These entities are for hydro stoves, the boiler, the second ducted fan and the pellet sensor.

| Platform | Key | Content | Field | Parameter |
|---|---|---|---|---|
| `select` | `ducted_fan_2` | Ducted fan 2 | 4 | 39 |
| `sensor` | `puffer_temperature` | Buffer tank temperature | 7 | |
| `sensor` | `boiler_temperature` | Boiler temperature | 8 | |
| `sensor` | `ntc3_temperature` | NTC3 probe temperature | 9 | |
| `sensor` | `return_temperature` | Return temperature | 59 | |
| `sensor` | `pump_pwm` | Pump, raw value | 16 | |
| `text_sensor` | `valve_3way` | 3-way valve: domestic hot water (1) / heating | 15 | |
| `number` | `boiler_setpoint` | Boiler setpoint | 27 | 51 |
| `sensor` | `pellet_sensor_code` | Pellet sensor: 0 = no sensor, 10 = level OK, 11 = empty | 47 | |
| `text_sensor` | `pellet_level` | Pellet level in plain text | 47 | |
| `binary_sensor` | `pellet_empty` | Pellet tank empty | 47 | |
| `switch` | `pellet_sensor` | Pellet sensor | 47 | 148 |
| `switch` | `summer_mode` | Summer mode, no read-back | | 58 |
| `number` | `chrono_t1` to `chrono_t3` | Chronothermostat temperatures, no read-back | | 1108 to 1110 |
| `number` | `profile` | Profile, raw value | 18 | 149 |
| `number` | `temperature_unit` | Temperature unit, raw value | 48 | 49 |
| `number` | `sleep` | Sleep, raw value | 50 | 57 |
| `number` | `antifreeze` | Antifreeze, raw value | 60 | 154 |
| `button` | `reset_active` | Resets the Active function | | 2 = `255` |
| `button` | `load_auger` | Loads the auger | | 34 = `49` |
| `sensor` | `modbus_address`, `database_id`, `field_51`, `field_55`, `set_puffer`, `set_boiler`, `set_health` | Raw values, meaning not documented | 19, 31, 51, 55, 56 to 58 | |

- These entities come from the maestrogateway table and **have not been tested**: the stove studied has none of these options.
- For the optional temperatures, the value `255` is treated as "probe absent".
- The `load_auger` button feeds pellets into the brazier: use it only with the stove off and cold.
- Deliberately not offered: the diagnostic commands (`C|Diagnostica|…`, direct control of the extractor, the auger, the igniter, the fans, the pump and the valve) and the factory reset (parameter 46). They remain reachable with a raw frame.

### Thermostat

The thermostat entity groups on/off, the setpoint and the room temperature in a Home Assistant thermostat card.

| Stove state | Mode shown | Activity shown |
|---|---|---|
| Ignition and combustion (1 to 15, 31) | Heat | Heating |
| Auto eco, standby (45, 46) | Heat | Idle |
| All others (off, extinguishing, cooling, alarms) | Off | Off |

- Switching to heat mode sends parameter 34 with `1`; switching to off sends it with `40`.
- Changing the setpoint sends parameter 42, rounded to the half degree.
- The state shown comes only from what the stove reports. After a command, it is updated with the next information frame.
- The two presets carry the regulation mode: choosing one sends parameter 40.
- The `power`, `setpoint` and `control_mode` entities can be declared as well; they stay in sync with the thermostat.

### Regulation mode

The stove has two operating modes:

- **Automatic**: it modulates its own power according to the setpoint and the room temperature. The thermostat drives the stove.
- **Manual**: it runs at the chosen power level. The thermostat setpoint has no effect; the thermostat is only used to turn the stove on and off.

The mode is chosen with the thermostat presets or with the `control_mode` select; both read field 22 and write parameter 40 (0 = manual, 1 = automatic).

Power is exposed by two entities:

| Key | Role |
|---|---|
| `power_level` (`sensor`) | Read-only: actual power level, in both modes |
| `power` (`number`) | Control, useful in manual mode. In automatic mode, the write is refused and a warning is written to the logs |

To show only the useful controls, the visibility of the dashboard cards can be tied to the state of the `control_mode` select: thermostat and power sensor in automatic mode, power setting in manual mode.

### Raw frames

The component exposes two functions usable in a lambda: `send_command("…")` sends a raw frame, without the final `^`, and `write_parameter(no., value)` writes a parameter. The examples use them to offer an action to Home Assistant, useful for testing a command that is not covered:

```yaml
api:
  actions:
    - action: send_command
      variables:
        command: string
      then:
        - lambda: 'id(stove).send_command(command);'
```

```yaml
action: esphome.mcz_stove_send_command
data:
  command: "C|WriteParametri|42|43"
```

### Virtual probe

It replaces the remote room probe with a Home Assistant sensor. It is configured in the component, with the `virtual_probe` option.

| Option | Default | Role |
|---|---|---|
| `temperature_sensor` | | Id of the ESPHome sensor to send, for example a `homeassistant` sensor |
| `probe` | `1` | Probe simulated: 1, 2 or 3, sent as `51`, `52` or `53` |
| `version` | `1.9.9` | Version announced by the probe |
| `require_api` | `true` | Send only while Home Assistant is connected |

- Frame sent: `C|RecuperaTemperaturaWiFi|<probe>|<T × 2>|<version>|00|<WiFi quality>`.
- The temperature is rounded to the nearest half degree (23.3 °C is sent as 23.5 °C), which avoids the downward bias of truncation.
- First transmission 30 seconds after startup, then at the interval returned by the stove, bounded between 1 and 30 minutes.
- Nothing is sent if the sensor is unavailable or if Home Assistant cannot be reached.
- The `virtual_probe` switch, if declared, suspends the transmissions; it is on at every startup.
- Turn off the original probe, otherwise both send their own temperature.
- If the virtual probe stops sending valid temperatures, the stove automatically goes back to manual mode and the app reports the WiFi probe as disconnected.

### Safeguards

- **No command at startup.** The component writes nothing until an entity is operated; every state comes from what the stove reports. An early version of the configuration used generic ESPHome switches, which return to "off" by default at startup and run their turn-off action: the module sent six writes at every startup, which started the stove during a test.
- **Guard.** Every write is ignored during the first 20 seconds after startup (`write_guard` option).
- **Power in automatic mode.** The power setting is refused while the stove is in automatic mode.
- **No flash writes.** In the examples, preferences are never written to flash (`flash_write_interval: never`).


---

## Sources

- [Chibald/maestrogateway](https://github.com/Chibald/maestrogateway): table of commands, information frame fields and stove states.
- Dumps of the three firmwares (`backup.img`, `backup-wifi1.img`, `backup-wifi2.img`), analysed with Ghidra.
