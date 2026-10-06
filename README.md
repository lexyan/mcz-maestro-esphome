# MCZ Maestro stove — ESPHome firmware and protocol notes

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

## Repository contents

| Path | Content |
|---|---|
| `mcz-poele.yaml` | ESPHome configuration for the cloud WiFi module |
| `mcz-maestro-documentation.en.md` | Full documentation (English) |
| `mcz-maestro-documentation.md` | Full documentation (French) |
| `esp8266_split.py` | Splits an ESP8266 flash dump into its regions |
| `images/` | Board photos: connector pinout, power vias, GPIO0 header |

## Quick start

1. Back up the original firmware of the module (see the documentation, part 5).
2. Adapt the substitutions at the top of `mcz-poele.yaml` and create `secrets.yaml`.
3. Compile with ESPHome and flash the module with esptool.
4. Add the device to Home Assistant.

The wiring, the esptool commands and the way back to the original firmware
are described in the [documentation](mcz-maestro-documentation.en.md).

## Tested on

MCZ Ego 2, mainboard firmware 1.8.2, no hydro module, one ducted fan.
Other Maestro stoves use the same protocol but may expose other fields.

## Credits

The command and field tables come from
[Chibald/maestrogateway](https://github.com/Chibald/maestrogateway).

## License

GPL-3.0. See [LICENSE](LICENSE).
