#include "weather_bom.h"

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <ctime>

namespace esphome {
namespace weather_bom {

static const char* const TAG = "weather_bom";

void WeatherBOM::dump_config() {
  ESP_LOGCONFIG(TAG, "Weather BOM:");
  LOG_UPDATE_INTERVAL(this);

  if (!this->geohash_.empty()) {
    ESP_LOGCONFIG(TAG, "  Geohash: %s", this->geohash_.c_str());
  } else if (this->have_static_lat_ && this->have_static_lon_) {
    ESP_LOGCONFIG(TAG, "  Static Latitude: %.6f", this->static_lat_);
    ESP_LOGCONFIG(TAG, "  Static Longitude: %.6f", this->static_lon_);
  } else if (this->lat_sensor_ && this->lon_sensor_) {
    ESP_LOGCONFIG(TAG, "  Latitude Sensor: yes");
    ESP_LOGCONFIG(TAG, "  Longitude Sensor: yes");
  } else {
    ESP_LOGCONFIG(TAG, "  No location configured");
  }

  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
  LOG_SENSOR("  ", "Wind Speed KMH", this->wind_kmh_);
  LOG_SENSOR("  ", "Today Min", this->today_min_);
  LOG_SENSOR("  ", "Today Max", this->today_max_);
  LOG_SENSOR("  ", "Today Rain Chance", this->today_rain_chance_);
  LOG_TEXT_SENSOR("  ", "Today Summary", this->today_summary_);
  LOG_TEXT_SENSOR("  ", "Today Icon", this->today_icon_);

  LOG_SENSOR("  ", "Tomorrow Min", this->tomorrow_min_);
  LOG_SENSOR("  ", "Tomorrow Max", this->tomorrow_max_);
  LOG_SENSOR("  ", "Tomorrow Rain Chance", this->tomorrow_rain_chance_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Summary", this->tomorrow_summary_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Icon", this->tomorrow_icon_);

  LOG_TEXT_SENSOR("  ", "Warnings JSON", this->warnings_json_);
  LOG_TEXT_SENSOR("  ", "Location Name", this->location_name_);
  LOG_TEXT_SENSOR("  ", "Out Geohash", this->out_geohash_);
  LOG_TEXT_SENSOR("  ", "Last Update", this->last_update_);
}

void WeatherBOM::setup() {
  ESP_LOGD(TAG, "Setting up WeatherBOM...");

  // Dynamic GPS handling
  if (this->lat_sensor_) {
    this->lat_sensor_->add_on_state_callback([this](float v) {
      this->dynamic_lat_ = v;
      this->have_dynamic_ = !std::isnan(v) && !std::isnan(this->dynamic_lon_);
      if (this->have_dynamic_ && this->geohash_.empty()) this->update();
    });
  }

  if (this->lon_sensor_) {
    this->lon_sensor_->add_on_state_callback([this](float v) {
      this->dynamic_lon_ = v;
      this->have_dynamic_ = !std::isnan(this->dynamic_lat_) && !std::isnan(v);
      if (this->have_dynamic_ && this->geohash_.empty()) this->update();
    });
  }

  // Publish static geohash (if configured at startup)
  if (!this->geohash_.empty() && this->out_geohash_) {
    this->out_geohash_->publish_state(this->geohash_);
  }
}

void WeatherBOM::loop() {
  // Only run once after boot
  if (!this->initial_fetch_done_) {
    if (wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_connected()) {
      ESP_LOGD(TAG, "WiFi connected after boot — fetching weather now");
      this->initial_fetch_done_ = true;
      this->update();
    }
  }
}

void WeatherBOM::update() {
  if (this->running_) {
    ESP_LOGD(TAG, "Fetch already running, skipping...");
    return;
  }

  if (wifi::global_wifi_component == nullptr ||
      !wifi::global_wifi_component->is_connected()) {
    ESP_LOGW(TAG, "WiFi not connected, skipping fetch.");
    return;
  }

  this->running_ = true;
  xTaskCreate(fetch_task, "bom_fetch", 6144, this, 5, nullptr);
}

void WeatherBOM::fetch_task(void* pv) {
  auto* self = static_cast<WeatherBOM*>(pv);
  self->do_fetch();
  vTaskDelete(nullptr);
}

void WeatherBOM::do_fetch() {
  bool success_obs = false, success_fc = false, success_warn = false;

  // Resolve geohash first if needed
  if (this->geohash_.empty()) {
    if (!this->resolve_geohash_if_needed_()) {
      ESP_LOGW(TAG, "Could not resolve geohash (need lat/lon)");
      App.scheduler.set_timeout(this, "bom_reset", 0,
                                [this]() { this->running_ = false; });
      return;
    }
  }

  // Fetch each endpoint
  std::string url;

  url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ +
        "/observations";
  success_obs = this->fetch_url_(url, this->obs_body_);

  url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ +
        "/forecasts/daily";
  success_fc = this->fetch_url_(url, this->fc_body_);

  url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ +
        "/warnings";
  success_warn = this->fetch_url_(url, this->warn_body_);

  if (!success_obs && !success_fc && !success_warn) {
    ESP_LOGW(TAG, "All BOM fetches failed, not spawning process_task");
    this->running_ = false;
    return;
  }

  // Launch a new FreeRTOS task for processing (to avoid blocking main loop)
  xTaskCreatePinnedToCore(&WeatherBOM::process_task,  // Task function
                          "bom_process",              // Name
                          6144,                       // Stack size
                          this,                       // Parameter
                          5,                          // Priority
                          nullptr,        // Task handle (not needed)
                          tskNO_AFFINITY  // Any core
  );
}

void WeatherBOM::process_task(void* pv) {
  auto* self = static_cast<WeatherBOM*>(pv);

  // Parse and publish data (runs off main loop)
  self->process_data();

  // Only publish timestamp if any success
  self->publish_last_update_();

  // Cleanup memory
  self->obs_body_.clear();
  self->fc_body_.clear();
  self->warn_body_.clear();

  // Mark as finished
  self->running_ = false;

  // Kill this task
  vTaskDelete(nullptr);
}

void WeatherBOM::process_data() {
  this->parse_and_publish_observations_(this->obs_body_);
  this->parse_and_publish_forecast_(this->fc_body_);
  this->parse_and_publish_warnings_(this->warn_body_);
  // Free memory
  this->obs_body_.clear();
  this->fc_body_.clear();
  this->warn_body_.clear();
}

bool WeatherBOM::resolve_geohash_if_needed_() {
  float lat = NAN, lon = NAN;

  if (this->have_static_lat_ && this->have_static_lon_) {
    lat = this->static_lat_;
    lon = this->static_lon_;
    ESP_LOGD(TAG, "Using static lat/lon: %f, %f", lat, lon);
  } else if (this->have_dynamic_) {
    lat = this->dynamic_lat_;
    lon = this->dynamic_lon_;
    ESP_LOGD(TAG, "Using dynamic lat/lon: %f, %f", lat, lon);
  } else {
    ESP_LOGD(TAG, "No lat/lon available for geohash resolution");
    return false;
  }

  if (std::isnan(lat) || std::isnan(lon)) {
    ESP_LOGD(TAG, "Invalid lat/lon for geohash resolution");
    return false;
  }

  char q[128];
  snprintf(q, sizeof(q),
           "https://api.weather.bom.gov.au/v1/locations?search=%f,%f", lat,
           lon);
  ESP_LOGD(TAG, "Resolving geohash with URL: %s", q);

  std::string resp;
  if (!this->fetch_url_(q, resp)) {
    ESP_LOGW(TAG, "Failed to fetch geohash resolution response");
    return false;
  }
  ESP_LOGD(TAG, "Fetched %d bytes for geohash resolution: %.100s...",
           (int)resp.size(), resp.c_str());

  cJSON* root = cJSON_ParseWithLength(resp.c_str(), resp.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse geohash JSON");
    return false;
  }

  bool ok = false;
  cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (data && cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
    cJSON* first = cJSON_GetArrayItem(data, 0);
    cJSON* gh = cJSON_GetObjectItemCaseSensitive(first, "geohash");
    cJSON* nm = cJSON_GetObjectItemCaseSensitive(first, "name");

    if (cJSON_IsString(gh) && gh->valuestring != nullptr) {
      std::string full_geohash = gh->valuestring;

      // ✅ Truncate to first 6 chars for BOM compatibility
      if (full_geohash.length() > 6) {
        ESP_LOGW(TAG,
                 "Geohash '%s' too long (%d). Truncating to '%s' for BOM API.",
                 full_geohash.c_str(), full_geohash.length(),
                 full_geohash.substr(0, 6).c_str());
        this->geohash_ = full_geohash.substr(0, 6);
      } else {
        this->geohash_ = full_geohash;
      }

      ok = true;
      ESP_LOGD(TAG, "Using geohash: %s", this->geohash_.c_str());

      if (this->out_geohash_) {
        this->out_geohash_->publish_state(this->geohash_);
      }
    } else {
      ESP_LOGW(TAG, "No geohash in response");
    }

    // ✅ Publish location name only if available
    if (cJSON_IsString(nm) && nm->valuestring && this->location_name_) {
      this->location_name_->publish_state(nm->valuestring);
      ESP_LOGD(TAG, "Location name: %s", nm->valuestring);
    }

  } else {
    ESP_LOGW(TAG, "No 'data' array or response was empty");
  }

  cJSON_Delete(root);

  if (ok) {
    // Track the lat/lon used for this geohash to detect changes later
    this->last_lat_ =
        this->have_static_lat_ ? this->static_lat_ : this->dynamic_lat_;
    this->last_lon_ =
        this->have_static_lon_ ? this->static_lon_ : this->dynamic_lon_;
  }
  return ok;
}

bool WeatherBOM::fetch_url_(const std::string& url, std::string& out) {
  // Keep BOM payloads tiny – they’re only ~1 KB in practice.
  static constexpr size_t MAX_HTTP_BODY = 4096;  // 3 KB cap

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 10000;
  cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "esp_http_client_init failed for %s", url.c_str());
    return false;
  }

  esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_GET);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set_method failed: %s for %s", esp_err_to_name(err), url.c_str());
    esp_http_client_cleanup(client);
    return false;
  }

  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open failed: %s for %s", esp_err_to_name(err), url.c_str());
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  ESP_LOGD(TAG, "HTTP status: %d, content_length: %d for %s",
           status, content_length, url.c_str());

  if (status != 200) {
    ESP_LOGW(TAG, "Non-200 status %d for %s", status, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  // If server claims a huge body, don't even try.
  if (content_length > 0 && content_length > (int)MAX_HTTP_BODY) {
    ESP_LOGW(TAG,
             "Content-Length %d > MAX_HTTP_BODY (%d) for %s, skipping",
             content_length, (int)MAX_HTTP_BODY, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  out.clear();
  out.reserve(MAX_HTTP_BODY);  // avoid repeated reallocations

  char buf[1024];
  while (true) {
    int r = esp_http_client_read(client, buf, sizeof(buf));
    if (r < 0) {
      ESP_LOGE(TAG, "Read error: %d for %s", r, url.c_str());
      break;
    }
    if (r == 0) break;

    // Enforce cap before appending
    size_t remaining = MAX_HTTP_BODY - out.size();
    if (remaining == 0) {
      ESP_LOGW(TAG, "Response reached MAX_HTTP_BODY (%d) for %s, truncating",
               (int)MAX_HTTP_BODY, url.c_str());
      break;
    }
    if ((size_t)r > remaining) r = (int)remaining;

    out.append(buf, r);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  bool success = !out.empty();
  if (!success) ESP_LOGW(TAG, "Empty or truncated response for %s", url.c_str());
  return success;
}

void WeatherBOM::parse_and_publish_observations_(const std::string& json) {
  ESP_LOGD(TAG, "Parsing observations JSON: %.100s...", json.c_str());

  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse observations JSON");
    return;
  }

  cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data)) {
    // Temperature
    cJSON* temp = cJSON_GetObjectItemCaseSensitive(data, "temp");
    if (cJSON_IsNumber(temp)) {
      float val = (float)temp->valuedouble;
      ESP_LOGD(TAG, "Temperature: %f", val);
      if (this->temperature_) this->temperature_->publish_state(val);
    }

    // Rain since 9 AM
    cJSON* rain = cJSON_GetObjectItemCaseSensitive(data, "rain_since_9am");
    if (cJSON_IsNumber(rain)) {
      float val = (float)rain->valuedouble;
      ESP_LOGD(TAG, "Rain since 9AM: %f", val);
      if (this->rain_since_9am_) this->rain_since_9am_->publish_state(val);
    }

    //  Humidity
    cJSON* hum = cJSON_GetObjectItemCaseSensitive(data, "humidity");
    if (cJSON_IsNumber(hum)) {
      float val = (float)hum->valuedouble;
      if (this->humidity_) this->humidity_->publish_state(val);
    }

    // Wind data
    float wind_val = NAN;
    cJSON* wind = cJSON_GetObjectItemCaseSensitive(data, "wind");
    if (cJSON_IsObject(wind)) {
      cJSON* kmh = cJSON_GetObjectItemCaseSensitive(wind, "speed_kilometre");
      if (cJSON_IsNumber(kmh)) wind_val = (float)kmh->valuedouble;
    }
    if (!std::isnan(wind_val) && this->wind_kmh_)
      this->wind_kmh_->publish_state(wind_val);
  }

  cJSON_Delete(root);
}

// file-local helpers for forecast parsing
static float _wb_coalesce_number(cJSON* obj, const char* k1,
                                 const char* k2 = nullptr) {
  if (!obj) return NAN;
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, k1);
  if (cJSON_IsNumber(v)) return (float)v->valuedouble;
  if (k2 != nullptr) {
    v = cJSON_GetObjectItemCaseSensitive(obj, k2);
    if (cJSON_IsNumber(v)) return (float)v->valuedouble;
  }
  return NAN;
}

static std::string _wb_coalesce_string(cJSON* obj, const char* k1,
                                       const char* k2 = nullptr) {
  if (!obj) return {};
  cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, k1);
  if (cJSON_IsString(v) && v->valuestring) return std::string(v->valuestring);
  if (k2 != nullptr) {
    v = cJSON_GetObjectItemCaseSensitive(obj, k2);
    if (cJSON_IsString(v) && v->valuestring) return std::string(v->valuestring);
  }
  return {};
}

void WeatherBOM::parse_and_publish_forecast_(const std::string& json) {
  if (json.empty()) {
    ESP_LOGD(TAG, "No forecast JSON to parse");
    return;
  }

  ESP_LOGD(TAG, "Parsing forecast JSON: %.100s...", json.c_str());
  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse forecast JSON");
    return;
  }

  cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsArray(arr)) {
    arr = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    if (cJSON_IsArray(arr)) ESP_LOGD(TAG, "Using 'forecast' instead of 'data'");
  }

  if (cJSON_IsArray(arr)) {
    cJSON* day0 = cJSON_GetArrayItem(arr, 0);
    cJSON* day1 = cJSON_GetArrayItem(arr, 1);

    auto handle_day = [&](cJSON* day, bool is_today) {
      if (!day) return;

      // --- 1. Extract values into variables first ---

      float tmin = _wb_coalesce_number(day, "temp_min", "temperature_min");
      float tmax = _wb_coalesce_number(day, "temp_max", "temperature_max");

      float rain_min = NAN;
      float rain_max = NAN;
      float rain_chance = NAN;
      std::string sunrise, sunset;
      std::string summary = _wb_coalesce_string(day, "short_text", "summary");
      std::string icon = _wb_coalesce_string(day, "icon_descriptor", "icon");

      // Rain values
      cJSON* rain = cJSON_GetObjectItemCaseSensitive(day, "rain");
      if (rain) {
        rain_chance = _wb_coalesce_number(rain, "chance");

        cJSON* amount = cJSON_GetObjectItemCaseSensitive(rain, "amount");
        if (amount) {
          rain_min = _wb_coalesce_number(amount, "min");
          rain_max = _wb_coalesce_number(amount, "max");
        }
      }

      // Sunrise/Sunset
      cJSON* astro = cJSON_GetObjectItemCaseSensitive(day, "astronomical");
      if (astro) {
        sunrise = _wb_coalesce_string(astro, "sunrise_time");
        sunset = _wb_coalesce_string(astro, "sunset_time");
      }

      // --- 2. Now publish in grouped style (consistent with your code) ---

      if (is_today) {
        if (!std::isnan(tmin) && this->today_min_)
          this->today_min_->publish_state(tmin);
        if (!std::isnan(tmax) && this->today_max_)
          this->today_max_->publish_state(tmax);

        if (!std::isnan(rain_chance) && this->today_rain_chance_)
          this->today_rain_chance_->publish_state(rain_chance);
        if (!std::isnan(rain_min) && this->today_rain_min_)
          this->today_rain_min_->publish_state(rain_min);
        if (!std::isnan(rain_max) && this->today_rain_max_)
          this->today_rain_max_->publish_state(rain_max);

        if (!sunrise.empty() && this->today_sunrise_)
          this->today_sunrise_->publish_state(sunrise);
        if (!sunset.empty() && this->today_sunset_)
          this->today_sunset_->publish_state(sunset);

        if (!summary.empty() && this->today_summary_)
          this->today_summary_->publish_state(summary);
        if (!icon.empty() && this->today_icon_)
          this->today_icon_->publish_state(icon);

      } else {
        if (!std::isnan(tmin) && this->tomorrow_min_)
          this->tomorrow_min_->publish_state(tmin);
        if (!std::isnan(tmax) && this->tomorrow_max_)
          this->tomorrow_max_->publish_state(tmax);

        if (!std::isnan(rain_chance) && this->tomorrow_rain_chance_)
          this->tomorrow_rain_chance_->publish_state(rain_chance);
        if (!std::isnan(rain_min) && this->tomorrow_rain_min_)
          this->tomorrow_rain_min_->publish_state(rain_min);
        if (!std::isnan(rain_max) && this->tomorrow_rain_max_)
          this->tomorrow_rain_max_->publish_state(rain_max);

        if (!sunrise.empty() && this->tomorrow_sunrise_)
          this->tomorrow_sunrise_->publish_state(sunrise);
        if (!sunset.empty() && this->tomorrow_sunset_)
          this->tomorrow_sunset_->publish_state(sunset);

        if (!summary.empty() && this->tomorrow_summary_)
          this->tomorrow_summary_->publish_state(summary);
        if (!icon.empty() && this->tomorrow_icon_)
          this->tomorrow_icon_->publish_state(icon);
      }
    };

    handle_day(day0, true);
    handle_day(day1, false);
  } else {
    ESP_LOGW(TAG, "No forecast array found");
  }

  cJSON_Delete(root);
}

void WeatherBOM::parse_and_publish_warnings_(const std::string& json) {
  if (!this->warnings_json_) return;

  if (json.empty()) {
    this->warnings_json_->publish_state("[]");
    return;
  }

  ESP_LOGD(TAG, "Parsing warnings JSON: %.100s...", json.c_str());
  cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse warnings JSON, publishing empty");
    this->warnings_json_->publish_state("[]");
    return;
  }

  cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON* to_emit = data ? data : root;

  char* printed = cJSON_PrintUnformatted(to_emit);
  if (printed) {
    this->warnings_json_->publish_state(printed);
    cJSON_free(printed);
  } else {
    this->warnings_json_->publish_state("[]");
  }

  cJSON_Delete(root);
}

void WeatherBOM::publish_last_update_() {
  if (!this->last_update_) return;

  time_t now;
  time(&now);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  this->last_update_->publish_state(buf);
}

}  // namespace weather_bom
}  // namespace esphome
