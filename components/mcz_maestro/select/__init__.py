import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_OPTIONS, ENTITY_CATEGORY_CONFIG

from .. import (
    AUX_EXTRA,
    AUX_PROBES,
    CONF_MCZ_MAESTRO_ID,
    HUB_CHILD_SCHEMA,
    mcz_maestro_ns,
)

DEPENDENCIES = ["mcz_maestro"]

MczSelect = mcz_maestro_ns.class_("MczSelect", select.Select)

# Fans: 0 = off ("No Air" in the MCZ app), 1 to 5 = manual speed, 6 = automatic
FAN_OPTIONS = {0: "Off", 1: "1", 2: "2", 3: "3", 4: "4", 5: "5", 6: "Auto"}
# Regulation mode: 0 = manual (fixed power), 1 = automatic (follows the setpoint)
CONTROL_MODE_OPTIONS = {0: "Manual", 1: "Auto"}
# Season ("Estate / Inverno" in the MCZ app): 0 = winter, 1 = summer
SEASON_OPTIONS = {0: "Winter", 1: "Summer"}


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


def _schema(defaults, icon, config=False):
    kwargs = {"entity_category": ENTITY_CATEGORY_CONFIG} if config else {}
    return select.select_schema(MczSelect, icon=icon, **kwargs).extend(
        {cv.Optional(CONF_OPTIONS, default=defaults): _option_map(defaults)}
    )


# key: (write parameter, field read back, schema)
SELECTS = {
    "control_mode": (40, 22, _schema(CONTROL_MODE_OPTIONS, "mdi:tune-variant")),
    "fan": (37, 2, _schema(FAN_OPTIONS, "mdi:fan")),
    "ducted_fan_1": (38, 3, _schema(FAN_OPTIONS, "mdi:fan")),
    "ducted_fan_2": (39, 4, _schema(FAN_OPTIONS, "mdi:fan")),
    "season_mode": (
        58,
        51,
        _schema(SEASON_OPTIONS, "mdi:sun-snowflake-variant", config=True),
    ),
}

# Combustion recipes ("Ricetta Aria" / "Ricetta Pellet" in the MCZ app)
AIR_RECIPE_OPTIONS = {0: "-2", 1: "-1", 2: "0", 3: "+1", 4: "+2"}
PELLET_RECIPE_OPTIONS = {0: "-3", 1: "-2", 2: "-1", 3: "0", 4: "+1", 5: "+2", 6: "+3"}
# Source of the room temperature ("Ingresso ambiente")
ROOM_INPUT_OPTIONS = {255: "WiFi probe", 0: "Thermostat", 1: "Room probe"}
# WiFi probe: minutes between two transmissions, in winter and in summer mode
PROBE_INTERVAL_OPTIONS = {10: "10", 15: "15", 20: "20"}
PROBE_SUMMER_INTERVAL_OPTIONS = {45: "45", 60: "60", 90: "90", 120: "120"}
# WiFi probe: correction in degrees (negative values are sent as 100 + |value|)
PROBE_OFFSET_OPTIONS = {
    105: "-5",
    104: "-4",
    103: "-3",
    102: "-2",
    101: "-1",
    0: "0",
    1: "+1",
    2: "+2",
    3: "+3",
    4: "+4",
    5: "+5",
}

# Selects read from an auxiliary frame.
# key: (frame, index in the frame, write parameter, database cell, schema)
# Exactly one of "write parameter" and "database cell" is set.
AUX_SELECTS = {
    "air_recipe": (
        AUX_EXTRA,
        0,
        None,
        459,
        _schema(AIR_RECIPE_OPTIONS, "mdi:weather-windy", config=True),
    ),
    "pellet_recipe": (
        AUX_EXTRA,
        1,
        None,
        460,
        _schema(PELLET_RECIPE_OPTIONS, "mdi:grain", config=True),
    ),
    "room_input": (
        AUX_EXTRA,
        2,
        None,
        356,
        _schema(ROOM_INPUT_OPTIONS, "mdi:thermometer-lines", config=True),
    ),
    "wifi_probe_interval": (
        AUX_PROBES,
        0,
        110,
        None,
        _schema(PROBE_INTERVAL_OPTIONS, "mdi:timer-outline", config=True),
    ),
    "wifi_probe_summer_interval": (
        AUX_PROBES,
        1,
        111,
        None,
        _schema(PROBE_SUMMER_INTERVAL_OPTIONS, "mdi:timer-outline", config=True),
    ),
    "wifi_probe_offset": (
        AUX_PROBES,
        7,
        144,
        None,
        _schema(PROBE_OFFSET_OPTIONS, "mdi:thermometer-plus", config=True),
    ),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[2] for key, item in SELECTS.items()}
).extend({cv.Optional(key): item[4] for key, item in AUX_SELECTS.items()})


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
    for key, (kind, index, param, cell, _) in AUX_SELECTS.items():
        if conf := config.get(key):
            options = conf[CONF_OPTIONS]
            var = await select.new_select(conf, options=list(options.values()))
            cg.add(var.set_parent(parent))
            cg.add(var.set_aux(kind, index))
            if cell is not None:
                cg.add(var.set_cell(cell))
            else:
                cg.add(var.set_param(param))
            if key == "room_input":
                cg.add(var.set_auto_mode_on_zero(True))
            cg.add(var.set_mappings(list(options.keys())))
            cg.add(parent.register_listener(var))
            cg.add(parent.enable_aux(kind))
