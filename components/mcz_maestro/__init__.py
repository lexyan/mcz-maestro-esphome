"""ESPHome component for MCZ pellet stoves with the Maestro mainboard.

It talks to the mainboard over the serial link of the stove's WiFi module, with
the same protocol as the original firmware:
    sent:   <command>^\\r\\n        (e.g. C|RecuperoInfo^)
    reply:  hexadecimal fields separated by | and terminated by ^
The table of fields and parameters comes from the Chibald/maestrogateway project.
"""

import esphome.codegen as cg
from esphome.components import sensor, time, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID, CONF_VERSION

CODEOWNERS = ["@lexyan"]
DEPENDENCIES = ["uart"]

CONF_MCZ_MAESTRO_ID = "mcz_maestro_id"
CONF_ANNOUNCE = "announce"
CONF_MODULE_VERSION = "module_version"
CONF_WRITE_GUARD = "write_guard"
CONF_LANGUAGE = "language"
CONF_VIRTUAL_PROBE = "virtual_probe"
CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_PROBE = "probe"
CONF_REQUIRE_API = "require_api"

mcz_maestro_ns = cg.esphome_ns.namespace("mcz_maestro")
# Frames read in addition to the information frame (see MczAuxKind in mcz_maestro.h)
MczAuxKind = mcz_maestro_ns.enum("MczAuxKind")
AUX_EXTRA = MczAuxKind.AUX_EXTRA
AUX_PARAMS = MczAuxKind.AUX_PARAMS
AUX_VERSIONS = MczAuxKind.AUX_VERSIONS
AUX_PROBES = MczAuxKind.AUX_PROBES
AUX_ALARMS = MczAuxKind.AUX_ALARMS
MczMaestro = mcz_maestro_ns.class_(
    "MczMaestro", cg.PollingComponent, uart.UARTDevice
)

def _no_separator(value):
    """Version strings end up in a frame whose fields are separated by '|'."""
    value = cv.All(cv.string_strict, cv.Length(min=1, max=12))(value)
    if "|" in value or "^" in value:
        raise cv.Invalid("The characters | and ^ are not allowed")
    return value


def _unique_probes(value):
    numbers = [probe[CONF_PROBE] for probe in value]
    if len(set(numbers)) != len(numbers):
        raise cv.Invalid("Each virtual probe needs its own probe number (1, 2 or 3)")
    return value


VIRTUAL_PROBE_SCHEMA = cv.Schema(
    {
        # Sensor whose value is sent to the stove in place of the remote WiFi probe
        cv.Required(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
        # Probe simulated: 1, 2 or 3 (sent as 51, 52 or 53)
        cv.Optional(CONF_PROBE, default=1): cv.int_range(min=1, max=3),
        cv.Optional(CONF_VERSION, default="1.9.9"): _no_separator,
        # Send only while a Home Assistant API client is connected
        cv.Optional(CONF_REQUIRE_API, default=True): cv.boolean,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MczMaestro),
            # Announce the module to the mainboard at start-up, like the original firmware
            cv.Optional(CONF_ANNOUNCE, default=True): cv.boolean,
            cv.Optional(CONF_MODULE_VERSION, default="1.2.6"): _no_separator,
            # No parameter is written to the stove during this delay after boot
            cv.Optional(
                CONF_WRITE_GUARD, default="20s"
            ): cv.positive_time_period_milliseconds,
            # Language of the texts published by the text sensors
            cv.Optional(CONF_LANGUAGE, default="en"): cv.one_of("en", "fr", lower=True),
            # Time source used by the "set_time" button and by sync_time()
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # One probe, or a list of up to three (one per probe number)
            cv.Optional(CONF_VIRTUAL_PROBE): cv.All(
                cv.ensure_list(VIRTUAL_PROBE_SCHEMA),
                cv.Length(min=1, max=3),
                _unique_probes,
            ),
        }
    )
    .extend(cv.polling_component_schema("15s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "mcz_maestro", baud_rate=115200, require_tx=True, require_rx=True
)

# Schema fragment shared by all the platforms of this component
HUB_CHILD_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_MCZ_MAESTRO_ID): cv.use_id(MczMaestro)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_announce(config[CONF_ANNOUNCE]))
    cg.add(var.set_module_version(config[CONF_MODULE_VERSION]))
    cg.add(var.set_write_guard(config[CONF_WRITE_GUARD]))
    if config[CONF_LANGUAGE] == "fr":
        cg.add_define("MCZ_MAESTRO_LANG_FR")

    if CONF_TIME_ID in config:
        time_ = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_))

    for probe in config.get(CONF_VIRTUAL_PROBE, []):
        source = await cg.get_variable(probe[CONF_TEMPERATURE_SENSOR])
        cg.add(
            var.add_probe(
                probe[CONF_PROBE],
                source,
                probe[CONF_VERSION],
                probe[CONF_REQUIRE_API],
            )
        )
