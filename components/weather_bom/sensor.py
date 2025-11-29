import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_TYPE,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_MILLIMETER,
)
from . import (
    CONF_WEATHER_BOM_ID,
    HUB_CHILD_SCHEMA,
)

DEPENDENCIES = ["weather_bom"]

ICON_THERMOMETER = "mdi:thermometer"
ICON_RAIN_CHANCE = "mdi:umbrella-percent"
ICON_RAIN_AMOUNT = "mdi:weather-rainy"
ICON_WINDY = "mdi:weather-windy"
ICON_HUMIDITY = "mdi:water-percent"

# Sensor types
SENSOR_TYPE_TEMPERATURE = "temperature"
SENSOR_TYPE_HUMIDITY = "humidity"
SENSOR_TYPE_WIND_SPEED_KMH = "wind_speed_kmh"
SENSOR_TYPE_RAIN_SINCE_9AM = "rain_since_9am"
SENSOR_TYPE_TODAY_MIN = "today_min"
SENSOR_TYPE_TODAY_MAX = "today_max"
SENSOR_TYPE_TODAY_RAIN_CHANCE = "today_rain_chance"
SENSOR_TYPE_TODAY_RAIN_MIN = "today_rain_min"
SENSOR_TYPE_TODAY_RAIN_MAX = "today_rain_max"
SENSOR_TYPE_TOMORROW_MIN = "tomorrow_min"
SENSOR_TYPE_TOMORROW_MAX = "tomorrow_max"
SENSOR_TYPE_TOMORROW_RAIN_CHANCE = "tomorrow_rain_chance"
SENSOR_TYPE_TOMORROW_RAIN_MIN = "tomorrow_rain_min"
SENSOR_TYPE_TOMORROW_RAIN_MAX = "tomorrow_rain_max"

# Sensor type configurations (unit, icon, accuracy_decimals)
SENSOR_TYPE_CONFIGS = {
    SENSOR_TYPE_TEMPERATURE: (UNIT_CELSIUS, ICON_THERMOMETER, 1),
    SENSOR_TYPE_HUMIDITY: (UNIT_PERCENT, ICON_HUMIDITY, 0),
    SENSOR_TYPE_WIND_SPEED_KMH: (UNIT_KILOMETER_PER_HOUR, ICON_WINDY, 0),
    SENSOR_TYPE_RAIN_SINCE_9AM: (UNIT_MILLIMETER, ICON_RAIN_AMOUNT, 1),
    SENSOR_TYPE_TODAY_MIN: (UNIT_CELSIUS, ICON_THERMOMETER, 1),
    SENSOR_TYPE_TODAY_MAX: (UNIT_CELSIUS, ICON_THERMOMETER, 1),
    SENSOR_TYPE_TODAY_RAIN_CHANCE: (UNIT_PERCENT, ICON_RAIN_CHANCE, 0),
    SENSOR_TYPE_TODAY_RAIN_MIN: (UNIT_MILLIMETER, ICON_RAIN_AMOUNT, 1),
    SENSOR_TYPE_TODAY_RAIN_MAX: (UNIT_MILLIMETER, ICON_RAIN_AMOUNT, 1),
    SENSOR_TYPE_TOMORROW_MIN: (UNIT_CELSIUS, ICON_THERMOMETER, 1),
    SENSOR_TYPE_TOMORROW_MAX: (UNIT_CELSIUS, ICON_THERMOMETER, 1),
    SENSOR_TYPE_TOMORROW_RAIN_CHANCE: (UNIT_PERCENT, ICON_RAIN_CHANCE, 0),
    SENSOR_TYPE_TOMORROW_RAIN_MIN: (UNIT_MILLIMETER, ICON_RAIN_AMOUNT, 1),
    SENSOR_TYPE_TOMORROW_RAIN_MAX: (UNIT_MILLIMETER, ICON_RAIN_AMOUNT, 1),
}


def _create_sensor_schema(sensor_type):
    """Create a sensor schema for a specific sensor type."""
    unit, icon, accuracy = SENSOR_TYPE_CONFIGS[sensor_type]
    return (
        sensor.sensor_schema(
            unit_of_measurement=unit,
            icon=icon,
            accuracy_decimals=accuracy,
        )
        .extend(HUB_CHILD_SCHEMA)
        .extend(
            {
                cv.Required(CONF_TYPE): cv.one_of(*SENSOR_TYPE_CONFIGS.keys(), lower=True),
            }
        )
    )


def _typed_schema(config):
    """Validate and apply the correct schema based on the sensor type."""
    sensor_type = config.get(CONF_TYPE)
    if sensor_type is None:
        raise cv.Invalid("Missing required key 'type'")
    
    sensor_type = cv.string_strict(sensor_type).lower()
    if sensor_type not in SENSOR_TYPE_CONFIGS:
        raise cv.Invalid(
            f"Invalid sensor type '{sensor_type}'. Must be one of: {', '.join(SENSOR_TYPE_CONFIGS.keys())}"
        )
    
    schema = _create_sensor_schema(sensor_type)
    return schema(config)


CONFIG_SCHEMA = _typed_schema


async def to_code(config):
    hub = await cg.get_variable(config[CONF_WEATHER_BOM_ID])
    var = await sensor.new_sensor(config)
    sensor_type = config[CONF_TYPE]

    # Map sensor type to the appropriate setter function
    setter_map = {
        SENSOR_TYPE_TEMPERATURE: "set_temperature_sensor",
        SENSOR_TYPE_HUMIDITY: "set_humidity_sensor",
        SENSOR_TYPE_WIND_SPEED_KMH: "set_wind_kmh_sensor",
        SENSOR_TYPE_RAIN_SINCE_9AM: "set_rain_since_9am_sensor",
        SENSOR_TYPE_TODAY_MIN: "set_today_min_sensor",
        SENSOR_TYPE_TODAY_MAX: "set_today_max_sensor",
        SENSOR_TYPE_TODAY_RAIN_CHANCE: "set_today_rain_chance_sensor",
        SENSOR_TYPE_TODAY_RAIN_MIN: "set_today_rain_min_sensor",
        SENSOR_TYPE_TODAY_RAIN_MAX: "set_today_rain_max_sensor",
        SENSOR_TYPE_TOMORROW_MIN: "set_tomorrow_min_sensor",
        SENSOR_TYPE_TOMORROW_MAX: "set_tomorrow_max_sensor",
        SENSOR_TYPE_TOMORROW_RAIN_CHANCE: "set_tomorrow_rain_chance_sensor",
        SENSOR_TYPE_TOMORROW_RAIN_MIN: "set_tomorrow_rain_min_sensor",
        SENSOR_TYPE_TOMORROW_RAIN_MAX: "set_tomorrow_rain_max_sensor",
    }

    setter = setter_map.get(sensor_type)
    if setter:
        cg.add(getattr(hub, setter)(var))
