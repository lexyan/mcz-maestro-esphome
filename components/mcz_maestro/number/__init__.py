import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_MODE,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_CELSIUS,
    UNIT_MINUTE,
)

from .. import AUX_EXTRA, CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczNumber = mcz_maestro_ns.class_("MczNumber", number.Number)
MczNumberKind = mcz_maestro_ns.enum("MczNumberKind")
RAW = MczNumberKind.NUMBER_RAW
HALF = MczNumberKind.NUMBER_HALF
HALF_OPT = MczNumberKind.NUMBER_HALF_OPT
POWER = MczNumberKind.NUMBER_POWER
MINUTES = MczNumberKind.NUMBER_MINUTES
NO_FIELD = 0xFF


def _box(schema):
    """Numbers are shown as an input box unless the user asks otherwise."""
    return schema.extend(
        {
            cv.Optional(CONF_MODE, default="BOX"): cv.enum(
                number.NUMBER_MODES, upper=True
            )
        }
    )


def _temperature(config=False):
    kwargs = {
        "unit_of_measurement": UNIT_CELSIUS,
        "device_class": DEVICE_CLASS_TEMPERATURE,
    }
    if config:
        kwargs["entity_category"] = ENTITY_CATEGORY_CONFIG
    return _box(number.number_schema(MczNumber, **kwargs))


def _raw():
    return _box(number.number_schema(MczNumber, entity_category=ENTITY_CATEGORY_CONFIG))


# key: (kind, write parameter, field read back, min, max, step, schema)
NUMBERS = {
    # Room temperature setpoint: parameter 42 = temperature x 2
    "setpoint": (HALF, 42, 26, 5, 35, 0.5, _temperature()),
    # Power level 1 to 5 (sent as 11 to 15). Only used in manual regulation mode.
    "power": (
        POWER,
        36,
        29,
        1,
        5,
        1,
        number.number_schema(MczNumber, icon="mdi:fire"),
    ),
    # Boiler setpoint (hydro stoves)
    "boiler_setpoint": (HALF_OPT, 51, 27, 5, 90, 0.5, _temperature()),
    # Chronothermostat temperatures: not part of the information frame, no read-back
    "chrono_t1": (HALF, 1108, NO_FIELD, 5, 35, 0.5, _temperature(config=True)),
    "chrono_t2": (HALF, 1109, NO_FIELD, 5, 35, 0.5, _temperature(config=True)),
    "chrono_t3": (HALF, 1110, NO_FIELD, 5, 35, 0.5, _temperature(config=True)),
    # Integer parameters whose values are not documented: sent as is. Untested.
    "profile": (RAW, 149, 18, 0, 255, 1, _raw()),
    "temperature_unit": (RAW, 49, 48, 0, 255, 1, _raw()),
    "sleep": (RAW, 57, 50, 0, 255, 1, _raw()),
    "antifreeze": (RAW, 154, 60, 0, 255, 1, _raw()),
}

# Numbers read from an auxiliary frame and written to the stove's database.
# key: (kind, frame, index in the frame, cell, bytes, min, max, step, schema)
AUX_NUMBERS = {
    # Eco stop: minutes at the setpoint before the stove switches off
    "eco_stop_delay": (
        MINUTES,
        AUX_EXTRA,
        3,
        148,
        2,
        1,
        30,
        1,
        _box(
            number.number_schema(
                MczNumber,
                icon="mdi:timer-sand",
                unit_of_measurement=UNIT_MINUTE,
                entity_category=ENTITY_CATEGORY_CONFIG,
            )
        ),
    ),
    # Eco stop: degrees below the setpoint at which the stove restarts
    "eco_stop_hysteresis": (
        RAW,
        AUX_EXTRA,
        4,
        294,
        1,
        2,
        5,
        1,
        _box(
            number.number_schema(
                MczNumber,
                icon="mdi:thermometer-chevron-down",
                unit_of_measurement=UNIT_CELSIUS,
                entity_category=ENTITY_CATEGORY_CONFIG,
            )
        ),
    ),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[6] for key, item in NUMBERS.items()}
).extend({cv.Optional(key): item[8] for key, item in AUX_NUMBERS.items()})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (kind, param, field, min_, max_, step, _) in NUMBERS.items():
        if conf := config.get(key):
            var = await number.new_number(
                conf, min_value=min_, max_value=max_, step=step
            )
            cg.add(var.set_parent(parent))
            cg.add(var.set_kind(kind))
            cg.add(var.set_param(param))
            cg.add(var.set_field(field))
            cg.add(parent.register_listener(var))
    for key, item in AUX_NUMBERS.items():
        kind, aux, index, cell, nbytes, min_, max_, step, _ = item
        if conf := config.get(key):
            var = await number.new_number(
                conf, min_value=min_, max_value=max_, step=step
            )
            cg.add(var.set_parent(parent))
            cg.add(var.set_kind(kind))
            cg.add(var.set_aux(aux, index))
            cg.add(var.set_cell(cell, nbytes))
            cg.add(parent.register_listener(var))
            cg.add(parent.enable_aux(aux))
