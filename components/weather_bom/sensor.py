import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_TYPE,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_HUMIDITY,
    STATE_CLASS_MEASUREMENT,
)
from . import ns, WeatherBOM, CONF_WEATHER_BOM_ID

DEPENDENCIES = ["weather_bom"]

ICON_THERMOMETER = "mdi:thermometer"
ICON_RAIN_CHANCE = "mdi:umbrella-percent"
ICON_RAIN_AMOUNT = "mdi:weather-rainy"
ICON_WINDY = "mdi:weather-windy"
ICON_HUMIDITY = "mdi:water-percent"

SENSOR_TYPES = {
    "temperature": {
        "setter": "set_temperature_sensor",
        "unit": UNIT_CELSIUS,
        "icon": ICON_THERMOMETER,
        "accuracy": 1,
        "device_class": DEVICE_CLASS_TEMPERATURE,
        "state_class": STATE_CLASS_MEASUREMENT,
    },
    "humidity": {
        "setter": "set_humidity_sensor",
        "unit": UNIT_PERCENT,
        "icon": ICON_HUMIDITY,
        "accuracy": 0,
        "device_class": DEVICE_CLASS_HUMIDITY,
        "state_class": STATE_CLASS_MEASUREMENT,
    },
    "wind_speed_kmh": {
        "setter": "set_wind_kmh_sensor",
        "unit": "km/h",
        "icon": ICON_WINDY,
        "accuracy": 0,
        "state_class": STATE_CLASS_MEASUREMENT,
    },
    "rain_since_9am": {
        "setter": "set_rain_since_9am_sensor",
        "unit": "mm",
        "icon": ICON_RAIN_AMOUNT,
        "accuracy": 1,
        "state_class": STATE_CLASS_MEASUREMENT,
    },
    "today_min": {
        "setter": "set_today_min_sensor",
        "unit": UNIT_CELSIUS,
        "icon": ICON_THERMOMETER,
        "accuracy": 1,
        "device_class": DEVICE_CLASS_TEMPERATURE,
    },
    "today_max": {
        "setter": "set_today_max_sensor",
        "unit": UNIT_CELSIUS,
        "icon": ICON_THERMOMETER,
        "accuracy": 1,
        "device_class": DEVICE_CLASS_TEMPERATURE,
    },
    "today_rain_chance": {
        "setter": "set_today_rain_chance_sensor",
        "unit": UNIT_PERCENT,
        "icon": ICON_RAIN_CHANCE,
        "accuracy": 0,
    },
    "today_rain_min": {
        "setter": "set_today_rain_min_sensor",
        "unit": "mm",
        "icon": ICON_RAIN_AMOUNT,
        "accuracy": 1,
    },
    "today_rain_max": {
        "setter": "set_today_rain_max_sensor",
        "unit": "mm",
        "icon": ICON_RAIN_AMOUNT,
        "accuracy": 1,
    },
    "tomorrow_min": {
        "setter": "set_tomorrow_min_sensor",
        "unit": UNIT_CELSIUS,
        "icon": ICON_THERMOMETER,
        "accuracy": 1,
        "device_class": DEVICE_CLASS_TEMPERATURE,
    },
    "tomorrow_max": {
        "setter": "set_tomorrow_max_sensor",
        "unit": UNIT_CELSIUS,
        "icon": ICON_THERMOMETER,
        "accuracy": 1,
        "device_class": DEVICE_CLASS_TEMPERATURE,
    },
    "tomorrow_rain_chance": {
        "setter": "set_tomorrow_rain_chance_sensor",
        "unit": UNIT_PERCENT,
        "icon": ICON_RAIN_CHANCE,
        "accuracy": 0,
    },
    "tomorrow_rain_min": {
        "setter": "set_tomorrow_rain_min_sensor",
        "unit": "mm",
        "icon": ICON_RAIN_AMOUNT,
        "accuracy": 1,
    },
    "tomorrow_rain_max": {
        "setter": "set_tomorrow_rain_max_sensor",
        "unit": "mm",
        "icon": ICON_RAIN_AMOUNT,
        "accuracy": 1,
    },
}

CONFIG_SCHEMA = (
    sensor.sensor_schema()
    .extend(
        {
            cv.GenerateID(CONF_WEATHER_BOM_ID): cv.use_id(WeatherBOM),
            cv.Required(CONF_TYPE): cv.one_of(*SENSOR_TYPES, lower=True),
        }
    )
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_WEATHER_BOM_ID])
    sensor_type = config[CONF_TYPE]
    sensor_config = SENSOR_TYPES[sensor_type]

    var = await sensor.new_sensor(config)
    cg.add(getattr(hub, sensor_config["setter"])(var))
