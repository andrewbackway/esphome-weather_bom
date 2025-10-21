import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor

AUTO_LOAD = ["sensor", "text_sensor"]
CODEOWNERS = ["@andrew-b"]

ns = cg.esphome_ns.namespace("weather_bom")
WeatherBOM = ns.class_("WeatherBOM", cg.PollingComponent)

ICON_ALERT = "mdi:alert"
ICON_THERMOMETER = "mdi:thermometer"
ICON_RAIN_CHANCE = "mdi:umbrella-percent"
ICON_RAIN_AMOUNT = "mdi:weather-rainy"
ICON_WINDY = "mdi:weather-windy"
ICON_HUMIDITY = "mdi:water-percent"
ICON_CLOCK = "mdi:clock-outline"

# Inputs
CONF_GEOHASH = "geohash"
CONF_LATITUDE = "latitude"
CONF_LONGITUDE = "longitude"
CONF_LAT_SENSOR = "latitude_sensor"
CONF_LON_SENSOR = "longitude_sensor"

# Observations
CONF_TEMPERATURE = "temperature"
CONF_HUMIDITY = "humidity"
CONF_WIND_KMH = "wind_speed_kmh"

# Forecast Today
CONF_TODAY_MIN = "today_min"
CONF_TODAY_MAX = "today_max"
CONF_TODAY_RAIN_CHANCE = "today_rain_chance"
CONF_TODAY_RAIN_AMOUNT = "today_rain_amount"
CONF_TODAY_SUMMARY = "today_summary"
CONF_TODAY_ICON = "today_icon"

# Forecast Tomorrow
CONF_TOMORROW_MIN = "tomorrow_min"
CONF_TOMORROW_MAX = "tomorrow_max"
CONF_TOMORROW_RAIN_CHANCE = "tomorrow_rain_chance"
CONF_TOMORROW_RAIN_AMOUNT = "tomorrow_rain_amount"
CONF_TOMORROW_SUMMARY = "tomorrow_summary"
CONF_TOMORROW_ICON = "tomorrow_icon"

# Meta
CONF_WARNINGS_JSON = "warnings_json"
CONF_LOCATION_NAME = "location_name"
CONF_OUT_GEOHASH = "out_geohash"
CONF_LAST_UPDATE = "last_update"


def _validate_location(cfg):
    gh = cfg.get(CONF_GEOHASH)
    lat, lon = cfg.get(CONF_LATITUDE), cfg.get(CONF_LONGITUDE)
    lats, lons = cfg.get(CONF_LAT_SENSOR), cfg.get(CONF_LON_SENSOR)

    loc_methods = [
        bool(gh),
        (lat is not None and lon is not None),
        bool(lats and lons)
    ]
    provided_count = sum(loc_methods)

    if provided_count == 0:
        raise cv.Invalid(
            "Provide exactly one location method: geohash OR latitude+longitude OR latitude_sensor+longitude_sensor"
        )
    if provided_count > 1:
        raise cv.Invalid(
            "Provide exactly one location method: geohash OR latitude+longitude OR latitude_sensor+longitude_sensor"
        )

    if lat is not None:
        if not -90 <= lat <= 90:
            raise cv.Invalid(f"Latitude must be between -90 and 90, got {lat}")
    if lon is not None:
        if not -180 <= lon <= 180:
            raise cv.Invalid(f"Longitude must be between -180 and 180, got {lon}")

    return cfg


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WeatherBOM),
            cv.Optional(CONF_GEOHASH): cv.string,
            cv.Optional(CONF_LATITUDE): cv.float_,
            cv.Optional(CONF_LONGITUDE): cv.float_,
            cv.Optional(CONF_LAT_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_LON_SENSOR): cv.use_id(sensor.Sensor),

            # Observations
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement="°C",
                icon=ICON_THERMOMETER,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_HUMIDITY): sensor.sensor_schema(
                unit_of_measurement="%",
                icon=ICON_HUMIDITY,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_WIND_KMH): sensor.sensor_schema(
                unit_of_measurement="km/h",
                icon=ICON_WINDY,
                accuracy_decimals=0,
            ),

            # Today
            cv.Optional(CONF_TODAY_MIN): sensor.sensor_schema(
                unit_of_measurement="°C",
                icon=ICON_THERMOMETER,
                accuracy_decimals=1
            ),
            cv.Optional(CONF_TODAY_MAX): sensor.sensor_schema(
                unit_of_measurement="°C",
                icon=ICON_THERMOMETER,
                accuracy_decimals=1
            ),
            cv.Optional(CONF_TODAY_RAIN_CHANCE): sensor.sensor_schema(
                unit_of_measurement="%",
                icon=ICON_RAIN_CHANCE,
                accuracy_decimals=0
            ),
            cv.Optional(CONF_TODAY_RAIN_AMOUNT): text_sensor.text_sensor_schema(
                icon=ICON_RAIN_AMOUNT
            ),
            cv.Optional(CONF_TODAY_SUMMARY): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_TODAY_ICON): text_sensor.text_sensor_schema(),

            # Tomorrow
            cv.Optional(CONF_TOMORROW_MIN): sensor.sensor_schema(
                unit_of_measurement="°C",
                icon=ICON_THERMOMETER,
                accuracy_decimals=1
            ),
            cv.Optional(CONF_TOMORROW_MAX): sensor.sensor_schema(
                unit_of_measurement="°C",
                icon=ICON_THERMOMETER,
                accuracy_decimals=1
            ),
            cv.Optional(CONF_TOMORROW_RAIN_CHANCE): sensor.sensor_schema(
                unit_of_measurement="%",
                icon=ICON_RAIN_CHANCE,
                accuracy_decimals=0
            ),
            cv.Optional(CONF_TOMORROW_RAIN_AMOUNT): text_sensor.text_sensor_schema(
                icon=ICON_RAIN_AMOUNT
            ),
            cv.Optional(CONF_TOMORROW_SUMMARY): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_TOMORROW_ICON): text_sensor.text_sensor_schema(),

            # Meta
            cv.Optional(CONF_WARNINGS_JSON): text_sensor.text_sensor_schema(
                icon=ICON_ALERT
            ),
            cv.Optional(CONF_LOCATION_NAME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_OUT_GEOHASH): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_UPDATE): text_sensor.text_sensor_schema(
                icon=ICON_CLOCK
            ),
        }
    ).extend(cv.polling_component_schema("300s")),
    _validate_location,
)


async def to_code(config):
    var = cg.new_Pvariable(config[cg.CONF_ID])
    await cg.register_component(var, config)

    if CONF_GEOHASH in config:
        cg.add(var.set_geohash(config[CONF_GEOHASH]))
    if CONF_LATITUDE in config:
        cg.add(var.set_static_lat(config[CONF_LATITUDE]))
    if CONF_LONGITUDE in config:
        cg.add(var.set_static_lon(config[CONF_LONGITUDE]))
    if CONF_LAT_SENSOR in config:
        lat_s = await cg.get_variable(config[CONF_LAT_SENSOR])
        cg.add(var.set_lat_sensor(lat_s))
    if CONF_LON_SENSOR in config:
        lon_s = await cg.get_variable(config[CONF_LON_SENSOR])
        cg.add(var.set_lon_sensor(lon_s))

    async def _reg(name, fn):
        if name in config:
            obj = await sensor.new_sensor(config[name])
            cg.add(getattr(var, fn)(obj))

    async def _reg_text(name, fn):
        if name in config:
            obj = await text_sensor.new_text_sensor(config[name])
            cg.add(getattr(var, fn)(obj))

    # Observations
    await _reg(CONF_TEMPERATURE, "set_temperature_sensor")
    await _reg(CONF_HUMIDITY, "set_humidity_sensor")
    await _reg(CONF_WIND_KMH, "set_wind_kmh_sensor")

    # Today
    await _reg(CONF_TODAY_MIN, "set_today_min_sensor")
    await _reg(CONF_TODAY_MAX, "set_today_max_sensor")
    await _reg(CONF_TODAY_RAIN_CHANCE, "set_today_rain_chance_sensor")
    await _reg_text(CONF_TODAY_RAIN_AMOUNT, "set_today_rain_amount_text")
    await _reg_text(CONF_TODAY_SUMMARY, "set_today_summary_text")
    await _reg_text(CONF_TODAY_ICON, "set_today_icon_text")

    # Tomorrow
    await _reg(CONF_TOMORROW_MIN, "set_tomorrow_min_sensor")
    await _reg(CONF_TOMORROW_MAX, "set_tomorrow_max_sensor")
    await _reg(CONF_TOMORROW_RAIN_CHANCE, "set_tomorrow_rain_chance_sensor")
    await _reg_text(CONF_TOMORROW_RAIN_AMOUNT, "set_tomorrow_rain_amount_text")
    await _reg_text(CONF_TOMORROW_SUMMARY, "set_tomorrow_summary_text")
    await _reg_text(CONF_TOMORROW_ICON, "set_tomorrow_icon_text")

    # Meta
    await _reg_text(CONF_WARNINGS_JSON, "set_warnings_json_text")
    await _reg_text(CONF_LOCATION_NAME, "set_location_name_text")
    await _reg_text(CONF_OUT_GEOHASH, "set_out_geohash_text")
    await _reg_text(CONF_LAST_UPDATE, "set_last_update_text")