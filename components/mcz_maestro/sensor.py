import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_HOUR,
    UNIT_MINUTE,
    UNIT_REVOLUTIONS_PER_MINUTE,
)

from . import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczSensorConv = mcz_maestro_ns.enum("MczSensorConv")
RAW = MczSensorConv.CONV_RAW
HALF = MczSensorConv.CONV_HALF
HALF_OPT = MczSensorConv.CONV_HALF_OPT
HOURS = MczSensorConv.CONV_HOURS
POWER = MczSensorConv.CONV_POWER


def _temperature(decimals=1, diagnostic=False):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=decimals,
        **({"entity_category": ENTITY_CATEGORY_DIAGNOSTIC} if diagnostic else {}),
    )


def _hours():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_HOUR,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=1,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _raw(icon=None, unit=None, measurement=False):
    kwargs = {
        "accuracy_decimals": 0,
        "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
    }
    if icon:
        kwargs["icon"] = icon
    if unit:
        kwargs["unit_of_measurement"] = unit
    if measurement:
        kwargs["state_class"] = STATE_CLASS_MEASUREMENT
    return sensor.sensor_schema(**kwargs)


# key: (field of the information frame, conversion, schema)
SENSORS = {
    # --- Every stove ---
    "state_code": (1, RAW, _raw(icon="mdi:numeric")),
    "fume_temperature": (5, RAW, _temperature(decimals=0)),
    "ambient_temperature": (6, HALF, _temperature()),
    "board_temperature": (28, HALF, _temperature(diagnostic=True)),
    "power_level": (
        29,
        POWER,
        sensor.sensor_schema(
            icon="mdi:fire",
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
    ),
    "fume_fan_rpm": (
        12,
        RAW,
        _raw(icon="mdi:fan", unit=UNIT_REVOLUTIONS_PER_MINUTE, measurement=True),
    ),
    "auger_rpm": (
        14,
        RAW,
        _raw(icon="mdi:screw-lag", unit=UNIT_REVOLUTIONS_PER_MINUTE, measurement=True),
    ),
    "auger_rpm_set": (
        13,
        RAW,
        _raw(icon="mdi:screw-lag", unit=UNIT_REVOLUTIONS_PER_MINUTE, measurement=True),
    ),
    # The "Active" values have no unit
    "active_set": (11, RAW, _raw(icon="mdi:gauge", measurement=True)),
    "active_live": (21, RAW, _raw(icon="mdi:gauge", measurement=True)),
    "active_temperature": (46, RAW, _raw(icon="mdi:thermometer", measurement=True)),
    "profile": (18, RAW, _raw()),
    "total_hours": (37, HOURS, _hours()),
    "hours_power_1": (38, HOURS, _hours()),
    "hours_power_2": (39, HOURS, _hours()),
    "hours_power_3": (40, HOURS, _hours()),
    "hours_power_4": (41, HOURS, _hours()),
    "hours_power_5": (42, HOURS, _hours()),
    "hours_to_service": (
        43,
        RAW,
        sensor.sensor_schema(
            unit_of_measurement=UNIT_HOUR,
            device_class=DEVICE_CLASS_DURATION,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    "ignitions": (
        45,
        RAW,
        sensor.sensor_schema(
            icon="mdi:fire",
            state_class=STATE_CLASS_TOTAL_INCREASING,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    "minutes_to_switch_off": (44, RAW, _raw(unit=UNIT_MINUTE)),
    # --- WiFi probes, as read back by the stove ---
    "wifi_probe_1": (52, HALF, _temperature(diagnostic=True)),
    "wifi_probe_2": (53, HALF, _temperature(diagnostic=True)),
    "wifi_probe_3": (54, HALF, _temperature(diagnostic=True)),
    # --- Hydro stoves, boiler (255 = probe absent) ---
    "puffer_temperature": (7, HALF_OPT, _temperature()),
    "boiler_temperature": (8, HALF_OPT, _temperature()),
    "ntc3_temperature": (9, HALF_OPT, _temperature()),
    "return_temperature": (59, HALF_OPT, _temperature()),
    "pump_pwm": (16, RAW, _raw(icon="mdi:pump", measurement=True)),
    # --- Pellet level sensor: 0 = no sensor, 10 = level OK, 11 = empty ---
    "pellet_sensor_code": (47, RAW, _raw()),
    # --- Raw values whose meaning is not documented ---
    "modbus_address": (19, RAW, _raw()),
    "database_id": (31, RAW, _raw()),
    "field_51": (51, RAW, _raw()),
    "field_55": (55, RAW, _raw()),
    "set_puffer": (56, RAW, _raw()),
    "set_boiler": (57, RAW, _raw()),
    "set_health": (58, RAW, _raw()),
}

# Sensors fed by the virtual WiFi probes rather than by the information frame.
# key: probe number. The keys without a number are those of probe 1.
VIRTUAL_PROBE_TEMPERATURES = {
    "virtual_probe_temperature": 1,
    "virtual_probe_2_temperature": 2,
    "virtual_probe_3_temperature": 3,
}
VIRTUAL_PROBE_INTERVALS = {
    "virtual_probe_interval": 1,
    "virtual_probe_2_interval": 2,
    "virtual_probe_3_interval": 3,
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): schema for key, (_, _, schema) in SENSORS.items()}
).extend(
    {
        # Temperature last sent to the stove
        **{
            cv.Optional(key): _temperature(diagnostic=True)
            for key in VIRTUAL_PROBE_TEMPERATURES
        },
        # Minutes requested by the stove before the next transmission
        **{cv.Optional(key): _raw(unit=UNIT_MINUTE) for key in VIRTUAL_PROBE_INTERVALS},
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (field, conv, _) in SENSORS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(parent.register_sensor(sens, field, conv))
    for key, number in VIRTUAL_PROBE_TEMPERATURES.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(parent.set_probe_sent_sensor(number, sens))
    for key, number in VIRTUAL_PROBE_INTERVALS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(parent.set_probe_interval_sensor(number, sens))
