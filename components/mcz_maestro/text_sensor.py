import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import (
    AUX_ALARMS,
    AUX_PROBES,
    AUX_VERSIONS,
    CONF_MCZ_MAESTRO_ID,
    HUB_CHILD_SCHEMA,
    mcz_maestro_ns,
)

DEPENDENCIES = ["mcz_maestro"]

MczTextKind = mcz_maestro_ns.enum("MczTextKind")

# key: (kind, schema)
TEXT_SENSORS = {
    # Stove state in plain text (language set on the mcz_maestro component)
    "state": (
        MczTextKind.TEXT_STATE,
        text_sensor.text_sensor_schema(icon="mdi:fireplace"),
    ),
    # Date and time of the stove clock, "YYYY-MM-DD HH:MM"
    "datetime": (
        MczTextKind.TEXT_DATETIME,
        text_sensor.text_sensor_schema(
            icon="mdi:calendar-clock", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    ),
    # Mainboard firmware version, e.g. "1.8.2"
    "firmware": (
        MczTextKind.TEXT_FIRMWARE,
        text_sensor.text_sensor_schema(
            icon="mdi:chip", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    ),
    # Hydro stoves: position of the 3-way valve
    "valve_3way": (
        MczTextKind.TEXT_VALVE,
        text_sensor.text_sensor_schema(icon="mdi:valve"),
    ),
    # Pellet level sensor
    "pellet_level": (
        MczTextKind.TEXT_PELLET,
        text_sensor.text_sensor_schema(icon="mdi:grain"),
    ),
}



def _diagnostic(icon):
    return text_sensor.text_sensor_schema(
        icon=icon, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )


# Texts read from an auxiliary frame. key: (kind, frame, schema)
AUX_TEXT_SENSORS = {
    # Versions reported by the mainboard
    "bootloader_version": (
        MczTextKind.TEXT_BOOTLOADER,
        AUX_VERSIONS,
        _diagnostic("mdi:chip"),
    ),
    "wifi_direct_version": (
        MczTextKind.TEXT_WIFI_DIRECT,
        AUX_VERSIONS,
        _diagnostic("mdi:wifi"),
    ),
    "wifi_remote_version": (
        MczTextKind.TEXT_WIFI_REMOTE,
        AUX_VERSIONS,
        _diagnostic("mdi:wifi"),
    ),
    "wifi_probe_version": (
        MczTextKind.TEXT_WIFI_PROBE,
        AUX_VERSIONS,
        _diagnostic("mdi:thermometer"),
    ),
    # Parameter database loaded in the mainboard
    "database_name": (
        MczTextKind.TEXT_DATABASE_NAME,
        AUX_VERSIONS,
        _diagnostic("mdi:database"),
    ),
    "database_revision": (
        MczTextKind.TEXT_DATABASE_REVISION,
        AUX_VERSIONS,
        _diagnostic("mdi:database"),
    ),
    "serial_number": (
        MczTextKind.TEXT_SERIAL_NUMBER,
        AUX_VERSIONS,
        _diagnostic("mdi:barcode"),
    ),
    # Last time the stove heard from the WiFi probe, on the stove's clock
    "wifi_probe_last_seen": (
        MczTextKind.TEXT_PROBE_LAST_SEEN,
        AUX_PROBES,
        _diagnostic("mdi:clock-check-outline"),
    ),
    # Most recent alarm, and the last five alarms with their description and date
    "last_alarm": (
        MczTextKind.TEXT_LAST_ALARM,
        AUX_ALARMS,
        _diagnostic("mdi:alarm-light-outline"),
    ),
    "alarm_history": (
        MczTextKind.TEXT_ALARM_HISTORY,
        AUX_ALARMS,
        _diagnostic("mdi:history"),
    ),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in TEXT_SENSORS.items()}
).extend({cv.Optional(key): item[2] for key, item in AUX_TEXT_SENSORS.items()})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (kind, _) in TEXT_SENSORS.items():
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(parent.set_text_sensor(kind, sens))
    for key, (kind, aux, _) in AUX_TEXT_SENSORS.items():
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(parent.set_text_sensor(kind, sens))
            cg.add(parent.enable_aux(aux))
