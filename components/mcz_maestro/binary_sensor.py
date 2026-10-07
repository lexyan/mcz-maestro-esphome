import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczBinaryMode = mcz_maestro_ns.enum("MczBinaryMode")
NONZERO = MczBinaryMode.BIN_NONZERO
EQUALS = MczBinaryMode.BIN_EQUALS
RANGE = MczBinaryMode.BIN_RANGE

# key: (field of the information frame, mode, a, b, schema)
BINARY_SENSORS = {
    # Stove state between 50 and 67: alarms A01 to A23
    "alarm": (
        1,
        RANGE,
        50,
        67,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    # Igniter (0 = off)
    "igniter": (
        10,
        NONZERO,
        0,
        0,
        binary_sensor.binary_sensor_schema(
            icon="mdi:candle", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    ),
    # Brazier to be cleaned (0 = clean)
    "brazier_dirty": (
        17,
        NONZERO,
        0,
        0,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
    # Pellet level sensor reporting an empty tank (11)
    "pellet_empty": (
        47,
        EQUALS,
        11,
        0,
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    ),
}

# True while the mainboard answers on the serial link
CONF_LINK = "link"

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[4] for key, item in BINARY_SENSORS.items()}
).extend(
    {
        cv.Optional(CONF_LINK): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (field, mode, a, b, _) in BINARY_SENSORS.items():
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(parent.register_binary_sensor(sens, field, mode, a, b))
    if conf := config.get(CONF_LINK):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(parent.set_link_binary_sensor(sens))
