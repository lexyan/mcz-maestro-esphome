import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

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

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, schema) in TEXT_SENSORS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (kind, _) in TEXT_SENSORS.items():
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(parent.set_text_sensor(kind, sens))
