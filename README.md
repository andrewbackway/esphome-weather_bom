# WeatherBOM – Bureau of Meteorology Weather for ESPHome (ESP-IDF)

A fully self-contained ESPHome external component that connects **directly to the Australian Bureau of Meteorology (BoM)** to fetch current observations, forecasts, and weather warnings — without Home Assistant or cloud dependencies.

Works entirely on-device using **ESP-IDF** networking and TLS, ideal for custom weather stations, dashboards, or offline displays.

---

## ✨ Features

- ✅ **No Home Assistant required** — direct HTTPS access to BoM's public API  
- ✅ **ESP-IDF native** (`esp_http_client`, `esp_crt_bundle_attach`)  
- ✅ Auto-resolves **BoM geohash** from:
  - Static latitude/longitude  
  - Dynamic GPS sensors (`latitude_sensor` / `longitude_sensor`)  
- ✅ Publishes **flattened sensors** (no JSON parsing needed client-side)  
- ✅ Includes:
  - Current **temperature**, **humidity**, and **wind speed**  
  - **Today + tomorrow forecasts** (min/max temps, rain chance, rain amount, summary, icon)  
  - **Active warnings** (raw JSON string)  
  - **Location name & resolved geohash**  
  - **Last update timestamp (ISO-8601)**  
- ✅ **Selective fetching** — enable/disable observations, forecast, or warnings individually  
- ✅ Compatible with ESP32 / ESP32-S3 under ESPHome 2025.11+
- ✅ Uses ESPHome's **standard platform-based sensor pattern** for compatibility with ESPHome 2025.11+

---

## ⚙️ YAML Example

```yaml

esphome:
  name: WeatherBom
  friendly_name: BOM Weather
  
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

external_components:
  - source: github://andrewbackway/esphome-weather_bom@main
    refresh: 1s

logger:
  level: DEBUG

api:

wifi:
  - ssid: !secret wifi_ssid
    password: !secret wifi_password

captive_portal:

web_server:
  version: 3

time:
  - platform: sntp
    id: sntp_time
    timezone: Melbourne/Australia
    servers:
      - 0.au.pool.ntp.org
      - 1.au.pool.ntp.org
      - 2.au.pool.ntp.org

# Define the weather_bom hub
weather_bom:
  id: weather_bom_hub
  latitude_sensor: gps_lat
  longitude_sensor: gps_lon
  update_interval: 300s
  # Optional: Enable/disable specific data fetching (default: all enabled)
  enable_observations: true
  enable_forecast: true
  enable_warnings: true

sensor:
  # GPS sensors for location
  - platform: template
    id: gps_lat
    name: "GPS Latitude"
    lambda: |-
      return -37.8136;
  - platform: template
    id: gps_lon
    name: "GPS Longitude"
    lambda: |-
      return 144.9631;

  # Weather observations
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: temperature
    name: "Weather Temperature"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: humidity
    name: "Weather Humidity"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: wind_speed_kmh
    name: "Weather Wind Speed (km/h)"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: rain_since_9am
    name: "Weather Rain Since 9AM"

  # Today's forecast
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_min
    name: "Weather Today Min Temp"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_max
    name: "Weather Today Max Temp"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_rain_chance
    name: "Weather Today Rain Chance"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_rain_min
    name: "Today's Rain Min"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_rain_max
    name: "Today's Rain Max"

  # Tomorrow's forecast
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_min
    name: "Weather Tomorrow Min Temp"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_max
    name: "Weather Tomorrow Max Temp"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_rain_chance
    name: "Weather Tomorrow Rain Chance"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_rain_min
    name: "Tomorrow Rain Min"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_rain_max
    name: "Tomorrow Rain Max"

text_sensor:
  # Today's forecast text
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_summary
    name: "Weather Today Summary"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: today_icon
    name: "Weather Today Icon"

  # Tomorrow's forecast text
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_summary
    name: "Weather Tomorrow Summary"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: tomorrow_icon
    name: "Weather Tomorrow Icon"

  # Metadata
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: warnings_json
    name: "Weather Warnings (JSON)"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: location_name
    name: "Weather Location Name"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: out_geohash
    name: "Weather Resolved Geohash"
  - platform: weather_bom
    weather_bom_id: weather_bom_hub
    type: last_update
    name: "Weather Last Update Time"
```

---

## 🧩 Sensor Types

### Numeric Sensors (platform: weather_bom under sensor:)

| Type | Description | Unit |
|------|-------------|------|
| `temperature` | Current temperature | °C |
| `humidity` | Current humidity | % |
| `wind_speed_kmh` | Current wind speed | km/h |
| `rain_since_9am` | Rain since 9 AM | mm |
| `today_min` | Today's minimum temperature | °C |
| `today_max` | Today's maximum temperature | °C |
| `today_rain_chance` | Today's rain chance | % |
| `today_rain_min` | Today's minimum rain | mm |
| `today_rain_max` | Today's maximum rain | mm |
| `tomorrow_min` | Tomorrow's minimum temperature | °C |
| `tomorrow_max` | Tomorrow's maximum temperature | °C |
| `tomorrow_rain_chance` | Tomorrow's rain chance | % |
| `tomorrow_rain_min` | Tomorrow's minimum rain | mm |
| `tomorrow_rain_max` | Tomorrow's maximum rain | mm |

### Text Sensors (platform: weather_bom under text_sensor:)

| Type | Description |
|------|-------------|
| `today_summary` | Today's weather summary |
| `today_icon` | Today's weather icon descriptor |
| `tomorrow_summary` | Tomorrow's weather summary |
| `tomorrow_icon` | Tomorrow's weather icon descriptor |
| `warnings_json` | Active weather warnings (JSON) |
| `location_name` | Resolved location name |
| `out_geohash` | Resolved geohash |
| `last_update` | Last update timestamp (ISO-8601) |

---

## 🌐 Data Sources

- **BoM Weather API (unofficial)**  
  `https://api.weather.bom.gov.au/v1/locations/<geohash>/observations`  
  `https://api.weather.bom.gov.au/v1/locations/<geohash>/forecasts/daily`  
  `https://api.weather.bom.gov.au/v1/locations/<geohash>/warnings`

- **Geohash Lookup**  
  `https://api.weather.bom.gov.au/v1/locations?search=<lat>,<lon>`

*(All data remains property of the Bureau of Meteorology.)*

---

## ⚠️ Notes & Limitations

- ⚙️ Requires **ESP-IDF** framework (not Arduino).  
- 🌧️ API is **unofficial** — schema changes may occur; the component is defensive.  
- 🧠 Update interval default is 5 minutes (300 s).  
- 📶 Keep requests modest to avoid server throttling.  
- 🧩 All HTTPS handled using system CA bundle — ensure `esp_crt_bundle_attach` is available in your ESPHome build.

---

## 🧑‍💻 Author & License

MIT License — Free for personal and research use.
