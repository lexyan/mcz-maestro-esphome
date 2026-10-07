import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG, ENTITY_CATEGORY_DIAGNOSTIC

from .. import CONF_MCZ_MAESTRO_ID, HUB_CHILD_SCHEMA, mcz_maestro_ns

DEPENDENCIES = ["mcz_maestro"]

MczButton = mcz_maestro_ns.class_("MczButton", button.Button)
MczButtonKind = mcz_maestro_ns.enum("MczButtonKind")
WRITE = MczButtonKind.BUTTON_WRITE

# key: (kind, write parameter, value, schema)
BUTTONS = {
    # Request the information frame now
    "refresh": (
        MczButtonKind.BUTTON_REFRESH,
        0,
        0,
        button.button_schema(MczButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    ),
    # Acknowledge the current alarm: parameter 1 = 255
    "reset_alarm": (
        WRITE,
        1,
        255,
        button.button_schema(
            MczButton,
            icon="mdi:alarm-light-off",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    # Set the stove clock from the time source (time_id of the component)
    "set_time": (
        MczButtonKind.BUTTON_SET_TIME,
        0,
        0,
        button.button_schema(
            MczButton, icon="mdi:clock-check", entity_category=ENTITY_CATEGORY_CONFIG
        ),
    ),
    # Reset the service counter ("hours to service"): parameter 43 = 0
    "reset_service": (
        WRITE,
        43,
        0,
        button.button_schema(
            MczButton, icon="mdi:wrench-clock", entity_category=ENTITY_CATEGORY_CONFIG
        ),
    ),
    # Reset the Active function: parameter 2 = 255. Untested.
    "reset_active": (
        WRITE,
        2,
        255,
        button.button_schema(MczButton, entity_category=ENTITY_CATEGORY_CONFIG),
    ),
    # Load the auger: parameter 34 = 49. Feeds pellets into the brazier.
    # Untested; only use it with the stove off and cold.
    "load_auger": (
        WRITE,
        34,
        49,
        button.button_schema(
            MczButton, icon="mdi:screw-lag", entity_category=ENTITY_CATEGORY_CONFIG
        ),
    ),
}

CONFIG_SCHEMA = HUB_CHILD_SCHEMA.extend(
    {cv.Optional(key): item[3] for key, item in BUTTONS.items()}
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_MCZ_MAESTRO_ID])
    for key, (kind, param, value, _) in BUTTONS.items():
        if conf := config.get(key):
            var = await button.new_button(conf)
            cg.add(var.set_parent(parent))
            cg.add(var.set_kind(kind))
            cg.add(var.set_write(param, value))
