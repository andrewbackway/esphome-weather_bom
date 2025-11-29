import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_TYPE,
)
from . import (
    CONF_WEATHER_BOM_ID,
    HUB_CHILD_SCHEMA,
)

DEPENDENCIES = ["weather_bom"]

ICON_ALERT = "mdi:alert"
ICON_CLOCK = "mdi:clock-outline"

# Text sensor types
TEXT_SENSOR_TYPE_TODAY_SUMMARY = "today_summary"
TEXT_SENSOR_TYPE_TODAY_ICON = "today_icon"
TEXT_SENSOR_TYPE_TOMORROW_SUMMARY = "tomorrow_summary"
TEXT_SENSOR_TYPE_TOMORROW_ICON = "tomorrow_icon"
TEXT_SENSOR_TYPE_WARNINGS_JSON = "warnings_json"
TEXT_SENSOR_TYPE_LOCATION_NAME = "location_name"
TEXT_SENSOR_TYPE_OUT_GEOHASH = "out_geohash"
TEXT_SENSOR_TYPE_LAST_UPDATE = "last_update"

# Text sensor type configurations (icon or None)
TEXT_SENSOR_TYPE_CONFIGS = {
    TEXT_SENSOR_TYPE_TODAY_SUMMARY: None,
    TEXT_SENSOR_TYPE_TODAY_ICON: None,
    TEXT_SENSOR_TYPE_TOMORROW_SUMMARY: None,
    TEXT_SENSOR_TYPE_TOMORROW_ICON: None,
    TEXT_SENSOR_TYPE_WARNINGS_JSON: ICON_ALERT,
    TEXT_SENSOR_TYPE_LOCATION_NAME: None,
    TEXT_SENSOR_TYPE_OUT_GEOHASH: None,
    TEXT_SENSOR_TYPE_LAST_UPDATE: ICON_CLOCK,
}


def _create_text_sensor_schema(sensor_type):
    """Create a text sensor schema for a specific text sensor type."""
    icon = TEXT_SENSOR_TYPE_CONFIGS[sensor_type]
    if icon:
        base_schema = text_sensor.text_sensor_schema(icon=icon)
    else:
        base_schema = text_sensor.text_sensor_schema()
    
    return (
        base_schema
        .extend(HUB_CHILD_SCHEMA)
        .extend(
            {
                cv.Required(CONF_TYPE): cv.one_of(*TEXT_SENSOR_TYPE_CONFIGS.keys(), lower=True),
            }
        )
    )


def _typed_schema(config):
    """Validate and apply the correct schema based on the text sensor type."""
    sensor_type = config.get(CONF_TYPE)
    if sensor_type is None:
        raise cv.Invalid("Missing required key 'type'")
    
    sensor_type = cv.string_strict(sensor_type).lower()
    if sensor_type not in TEXT_SENSOR_TYPE_CONFIGS:
        raise cv.Invalid(
            f"Invalid text sensor type '{sensor_type}'. Must be one of: {', '.join(TEXT_SENSOR_TYPE_CONFIGS.keys())}"
        )
    
    schema = _create_text_sensor_schema(sensor_type)
    return schema(config)


CONFIG_SCHEMA = _typed_schema


async def to_code(config):
    hub = await cg.get_variable(config[CONF_WEATHER_BOM_ID])
    var = await text_sensor.new_text_sensor(config)
    text_sensor_type = config[CONF_TYPE]

    # Map text sensor type to the appropriate setter function
    setter_map = {
        TEXT_SENSOR_TYPE_TODAY_SUMMARY: "set_today_summary_text",
        TEXT_SENSOR_TYPE_TODAY_ICON: "set_today_icon_text",
        TEXT_SENSOR_TYPE_TOMORROW_SUMMARY: "set_tomorrow_summary_text",
        TEXT_SENSOR_TYPE_TOMORROW_ICON: "set_tomorrow_icon_text",
        TEXT_SENSOR_TYPE_WARNINGS_JSON: "set_warnings_json_text",
        TEXT_SENSOR_TYPE_LOCATION_NAME: "set_location_name_text",
        TEXT_SENSOR_TYPE_OUT_GEOHASH: "set_out_geohash_text",
        TEXT_SENSOR_TYPE_LAST_UPDATE: "set_last_update_text",
    }

    setter = setter_map.get(text_sensor_type)
    if setter:
        cg.add(getattr(hub, setter)(var))
