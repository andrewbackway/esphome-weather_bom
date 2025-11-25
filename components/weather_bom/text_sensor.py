import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE
from . import ns, WeatherBOM, CONF_WEATHER_BOM_ID

DEPENDENCIES = ["weather_bom"]

ICON_ALERT = "mdi:alert"
ICON_CLOCK = "mdi:clock-outline"
ICON_WEATHER = "mdi:weather-partly-cloudy"
ICON_MAP_MARKER = "mdi:map-marker"

TEXT_SENSOR_TYPES = {
    "today_summary": {
        "setter": "set_today_summary_text",
        "icon": ICON_WEATHER,
    },
    "today_icon": {
        "setter": "set_today_icon_text",
        "icon": ICON_WEATHER,
    },
    "today_sunrise": {
        "setter": "set_today_sunrise_text",
        "icon": ICON_CLOCK,
    },
    "today_sunset": {
        "setter": "set_today_sunset_text",
        "icon": ICON_CLOCK,
    },
    "tomorrow_summary": {
        "setter": "set_tomorrow_summary_text",
        "icon": ICON_WEATHER,
    },
    "tomorrow_icon": {
        "setter": "set_tomorrow_icon_text",
        "icon": ICON_WEATHER,
    },
    "tomorrow_sunrise": {
        "setter": "set_tomorrow_sunrise_text",
        "icon": ICON_CLOCK,
    },
    "tomorrow_sunset": {
        "setter": "set_tomorrow_sunset_text",
        "icon": ICON_CLOCK,
    },
    "warnings_json": {
        "setter": "set_warnings_json_text",
        "icon": ICON_ALERT,
    },
    "location_name": {
        "setter": "set_location_name_text",
        "icon": ICON_MAP_MARKER,
    },
    "out_geohash": {
        "setter": "set_out_geohash_text",
        "icon": ICON_MAP_MARKER,
    },
    "last_update": {
        "setter": "set_last_update_text",
        "icon": ICON_CLOCK,
    },
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema()
    .extend(
        {
            cv.GenerateID(CONF_WEATHER_BOM_ID): cv.use_id(WeatherBOM),
            cv.Required(CONF_TYPE): cv.one_of(*TEXT_SENSOR_TYPES, lower=True),
        }
    )
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_WEATHER_BOM_ID])
    sensor_type = config[CONF_TYPE]
    sensor_config = TEXT_SENSOR_TYPES[sensor_type]

    var = await text_sensor.new_text_sensor(config)
    cg.add(getattr(hub, sensor_config["setter"])(var))
