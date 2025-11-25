import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@andrew-b"]
MULTI_CONF = True

ns = cg.esphome_ns.namespace("weather_bom")
WeatherBOM = ns.class_("WeatherBOM", cg.PollingComponent)

# Configuration key for referencing the hub from sensor/text_sensor platforms
CONF_WEATHER_BOM_ID = "weather_bom_id"

# Inputs
CONF_GEOHASH = "geohash"
CONF_LATITUDE = "latitude"
CONF_LONGITUDE = "longitude"
CONF_LAT_SENSOR = "latitude_sensor"
CONF_LON_SENSOR = "longitude_sensor"


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
        }
    ).extend(cv.polling_component_schema("300s")),
    _validate_location,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
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
