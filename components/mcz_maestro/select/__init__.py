import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS

from .. import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczSelect = mcz_maestro_ns.class_("MczSelect", select.Select)

# Fans: 0 = off ("No Air" in the MCZ app), 1 to 5 = manual speed, 6 = automatic
FAN_OPTIONS = {0: "Off", 1: "1", 2: "2", 3: "3", 4: "4", 5: "5", 6: "Auto"}
# Regulation mode: 0 = manual (fixed power), 1 = automatic (follows the setpoint)
CONTROL_MODE_OPTIONS = {0: "Manual", 1: "Auto"}


def _option_map(allowed):
    """Options as {value sent to the stove: label}. Labels can be translated;
    values must be among the ones the stove understands."""

    def validator(value):
        value = cv.Schema({cv.int_range(min=0, max=255): cv.string_strict})(value)
        if not value:
            raise cv.Invalid("At least one option is required")
        unknown = [v for v in value if v not in allowed]
        if unknown:
            raise cv.Invalid(f"Values not supported by the stove: {unknown}")
        if len(set(value.values())) != len(value):
            raise cv.Invalid("Labels must be unique")
        return value

    return validator


def _schema(defaults, icon):
    return select.select_schema(MczSelect, icon=icon).extend(
        {cv.Optional(CONF_OPTIONS, default=defaults): _option_map(defaults)}
    )


# key: (write parameter, field read back, schema)
SELECTS = {
    "control_mode": (40, 22, _schema(CONTROL_MODE_OPTIONS, "mdi:tune-variant")),
    "fan": (37, 2, _schema(FAN_OPTIONS, "mdi:fan")),
    "ducted_fan_1": (38, 3, _schema(FAN_OPTIONS, "mdi:fan")),
    "ducted_fan_2": (39, 4, _schema(FAN_OPTIONS, "mdi:fan")),
}

# Combustion recipes ("Ricetta Aria" / "Ricetta Pellet" in the MCZ app). They are read
# with C|RecuperoParametriExtra|11 and written to the stove's parameter database.
AIR_RECIPE_OPTIONS = {0: "-2", 1: "-1", 2: "0", 3: "+1", 4: "+2"}
PELLET_RECIPE_OPTIONS = {0: "-3", 1: "-2", 2: "-1", 3: "0", 4: "+1", 5: "+2", 6: "+3"}

# key: (database cell written, index in the extra parameters, schema)
RECIPE_SELECTS = {
    "air_recipe": (459, 0, _schema(AIR_RECIPE_OPTIONS, "mdi:weather-windy")),
    "pellet_recipe": (460, 1, _schema(PELLET_RECIPE_OPTIONS, "mdi:grain")),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[2] for key, item in SELECTS.items()}
).extend({cv.Optional(key): item[2] for key, item in RECIPE_SELECTS.items()})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (param, field, _) in SELECTS.items():
        if conf := config.get(key):
            options = conf[CONF_OPTIONS]
            var = await select.new_select(conf, options=list(options.values()))
            cg.add(var.set_parent(parent))
            cg.add(var.set_param(param))
            cg.add(var.set_field(field))
            cg.add(var.set_mappings(list(options.keys())))
            cg.add(parent.register_listener(var))
    for key, (cell, index, _) in RECIPE_SELECTS.items():
        if conf := config.get(key):
            options = conf[CONF_OPTIONS]
            var = await select.new_select(conf, options=list(options.values()))
            cg.add(var.set_parent(parent))
            cg.add(var.set_extra(index, cell))
            cg.add(var.set_mappings(list(options.keys())))
            cg.add(parent.register_listener(var))
            cg.add(parent.set_extra_enabled(True))
