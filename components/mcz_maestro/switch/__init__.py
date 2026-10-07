import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczSwitch = mcz_maestro_ns.class_("MczSwitch", switch.Switch)
MczSwitchKind = mcz_maestro_ns.enum("MczSwitchKind")
PARAM = MczSwitchKind.SWITCH_PARAM
NO_FIELD = 0xFF


def _schema(icon=None, config=False):
    kwargs = {}
    if icon:
        kwargs["icon"] = icon
    if config:
        kwargs["entity_category"] = ENTITY_CATEGORY_CONFIG
    return switch.switch_schema(MczSwitch, **kwargs)


# key: (kind, write parameter, field read back, schema)
SWITCHES = {
    # Stove on/off: parameter 34 (1 = on, 40 = off), state from the stove state
    "power": (MczSwitchKind.SWITCH_POWER, 34, 1, _schema(icon="mdi:power")),
    "eco_mode": (PARAM, 41, 23, _schema(config=True)),
    "silent_mode": (PARAM, 45, 24, _schema(config=True)),
    "active_mode": (PARAM, 35, 20, _schema(config=True)),
    "chronothermostat": (PARAM, 1111, 25, _schema(config=True)),
    "sounds": (PARAM, 50, 49, _schema(config=True)),
    # Pellet level sensor: state read from field 47 (non-zero = sensor present)
    "pellet_sensor": (PARAM, 148, 47, _schema(config=True)),
    # Summer / winter mode: see the season_mode select
    # Enables the virtual WiFi probe. On at every boot, sends nothing by itself.
    "virtual_probe": (
        MczSwitchKind.SWITCH_VIRTUAL_PROBE,
        0,
        NO_FIELD,
        _schema(icon="mdi:thermometer-auto", config=True),
    ),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[3] for key, item in SWITCHES.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (kind, param, field, _) in SWITCHES.items():
        if conf := config.get(key):
            var = await switch.new_switch(conf)
            cg.add(var.set_parent(parent))
            cg.add(var.set_kind(kind))
            cg.add(var.set_param(param))
            cg.add(var.set_field(field))
            cg.add(parent.register_listener(var))
