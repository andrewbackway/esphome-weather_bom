# WeatherBOM – Bureau of Meteorology Weather for ESPHome (ESP-IDF)

A fully self-contained ESPHome external component that connects **directly to the Australian Bureau of Meteorology (BoM)** to fetch current observations, forecasts, and weather warnings — without Home Assistant or cloud dependencies.

Works entirely on-device using **ESP-IDF** networking and TLS, ideal for custom weather stations, dashboards, or offline displays.

---

## ✨ Features

- ✅ **No Home Assistant required** — direct HTTPS access to BoM’s public API  
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
- ✅ Compatible with ESP32 / ESP32-S3 under ESPHome 2025.10+

---

## ⚙️ YAML Example

```yaml
esphome:
  name: bom_direct
  friendly_name: BOM Direct Weather

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

# Wi-Fi and time configuration (required for timestamps)
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

time:
  - platform: sntp
    id: sntp_time

external_components:
  - source: /config/external_components
    components: [weather_bom]

sensor:
  - platform: template
    id: gps_lat
    name: "GPS Latitude"
    lambda: |-
      return -37.8136;  # Melbourne
  - platform: template
    id: gps_lon
    name: "GPS Longitude"
    lambda: |-
      return 144.9631;

weather_bom:
  latitude_sensor: gps_lat
  longitude_sensor: gps_lon
  update_interval: 300s

  temperature:
    name: "Temperature"
  humidity:
    name: "Humidity"
  wind_speed_kmh:
    name: "Wind Speed (km/h)"

  today_min:
    name: "Today Min Temp"
  today_max:
    name: "Today Max Temp"
  today_rain_chance:
    name: "Today Rain Chance"
  today_rain_amount:
    name: "Today Rain Amount"
  today_summary:
    name: "Today Summary"
  today_icon:
    name: "Today Icon"

  tomorrow_min:
    name: "Tomorrow Min Temp"
  tomorrow_max:
    name: "Tomorrow Max Temp"
  tomorrow_rain_chance:
    name: "Tomorrow Rain Chance"
  tomorrow_rain_amount:
    name: "Tomorrow Rain Amount"
  tomorrow_summary:
    name: "Tomorrow Summary"
  tomorrow_icon:
    name: "Tomorrow Icon"

  warnings_json:
    name: "Weather Warnings (JSON)"
  location_name:
    name: "Location Name"
  out_geohash:
    name: "Resolved Geohash"
  last_update:
    name: "Last Update Time"
```

---

## 🧩 Published Entities

| Category | ID | Type | Description |
|-----------|----|------|-------------|
| **Observations** | `temperature`, `humidity`, `wind_speed_kmh` | Sensor | Current BoM observations |
| **Forecast (Today)** | `today_min`, `today_max`, `today_rain_chance`, `today_rain_amount`, `today_summary`, `today_icon` | Sensor/Text | Current day forecast |
| **Forecast (Tomorrow)** | `tomorrow_min`, `tomorrow_max`, `tomorrow_rain_chance`, `tomorrow_rain_amount`, `tomorrow_summary`, `tomorrow_icon` | Sensor/Text | Next day forecast |
| **Metadata** | `warnings_json`, `location_name`, `out_geohash`, `last_update` | TextSensor | JSON warnings, location info, update time |

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
- 🕒 Requires a valid `time:` platform for timestamps.  
- 🌧️ API is **unofficial** — schema changes may occur; the component is defensive.  
- 🧠 Update interval default is 5 minutes (300 s).  
- 📶 Keep requests modest to avoid server throttling.  
- 🧩 All HTTPS handled using system CA bundle — ensure `esp_crt_bundle_attach` is available in your ESPHome build.

---

## 🧑‍💻 Author & License

MIT License — Free for personal and research use.
