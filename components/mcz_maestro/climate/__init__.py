import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv

from .. import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

CONF_MANUAL_PRESET = "manual_preset"
CONF_AUTO_PRESET = "auto_preset"

MczClimate = mcz_maestro_ns.class_("MczClimate", climate.Climate)

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(MczClimate, icon="mdi:fireplace")
    .extend(HUB_CHILD_SCHEMA)
    .extend(
        {
            # Labels of the two presets carrying the regulation mode
            cv.Optional(CONF_MANUAL_PRESET, default="Manual"): cv.string_strict,
            cv.Optional(CONF_AUTO_PRESET, default="Auto"): cv.string_strict,
        }
    ),
)


def _validate(config):
    if config[CONF_MANUAL_PRESET] == config[CONF_AUTO_PRESET]:
        raise cv.Invalid("manual_preset and auto_preset must be different")
    return config


CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _validate)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    var = await climate.new_climate(config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_presets(config[CONF_MANUAL_PRESET], config[CONF_AUTO_PRESET]))
    cg.add(parent.register_listener(var))
