#include "weather_bom.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/wifi/wifi_component.h"

#ifdef USE_ESP_IDF
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include <cmath>
#include <ctime>

namespace esphome {
namespace weather_bom {

static const char *const TAG = "weather_bom";

void WeatherBOM::dump_config() {
  ESP_LOGCONFIG(TAG, "Weather BOM:");
  LOG_UPDATE_INTERVAL(this);
  if (!this->geohash_.empty()) {
    ESP_LOGCONFIG(TAG, "  Geohash: %s", this->geohash_.c_str());
  } else if (this->have_static_lat_ && this->have_static_lon_) {
    ESP_LOGCONFIG(TAG, "  Static Latitude: %.6f", this->static_lat_);
    ESP_LOGCONFIG(TAG, "  Static Longitude: %.6f", this->static_lon_);
  } else if (this->lat_sensor_ != nullptr && this->lon_sensor_ != nullptr) {
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
  LOG_TEXT_SENSOR("  ", "Today Rain Amount", this->today_rain_amount_);
  LOG_TEXT_SENSOR("  ", "Today Summary", this->today_summary_);
  LOG_TEXT_SENSOR("  ", "Today Icon", this->today_icon_);

  LOG_SENSOR("  ", "Tomorrow Min", this->tomorrow_min_);
  LOG_SENSOR("  ", "Tomorrow Max", this->tomorrow_max_);
  LOG_SENSOR("  ", "Tomorrow Rain Chance", this->tomorrow_rain_chance_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Rain Amount", this->tomorrow_rain_amount_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Summary", this->tomorrow_summary_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Icon", this->tomorrow_icon_);

  LOG_TEXT_SENSOR("  ", "Warnings JSON", this->warnings_json_);
  LOG_TEXT_SENSOR("  ", "Location Name", this->location_name_);
  LOG_TEXT_SENSOR("  ", "Out Geohash", this->out_geohash_);
  LOG_TEXT_SENSOR("  ", "Last Update", this->last_update_);
}

void WeatherBOM::setup() {
#ifndef USE_ESP_IDF
  ESP_LOGE(TAG, "This component requires ESP-IDF framework.");
  return;
#else
  ESP_LOGD(TAG, "Setting up WeatherBOM component");

  // Subscribe to lat/lon sensors
  if (this->lat_sensor_ != nullptr) {
    this->lat_sensor_->add_on_state_callback([this](float v) {
      ESP_LOGD(TAG, "Received latitude from sensor: %f", v);
      this->dynamic_lat_ = v;
      this->have_dynamic_ = !std::isnan(v) && !std::isnan(this->dynamic_lon_);
      ESP_LOGD(TAG, "Updated have_dynamic: %d", this->have_dynamic_);
      if (this->have_dynamic_ && this->geohash_.empty()) {
        ESP_LOGD(TAG, "First valid lat/lon received, triggering update");
        this->update();
      }
    });
  }
  if (this->lon_sensor_ != nullptr) {
    this->lon_sensor_->add_on_state_callback([this](float v) {
      ESP_LOGD(TAG, "Received longitude from sensor: %f", v);
      this->dynamic_lon_ = v;
      this->have_dynamic_ = !std::isnan(this->dynamic_lat_) && !std::isnan(v);
      ESP_LOGD(TAG, "Updated have_dynamic: %d", this->have_dynamic_);
      if (this->have_dynamic_ && this->geohash_.empty()) {
        ESP_LOGD(TAG, "First valid lat/lon received, triggering update");
        this->update();
      }
    });
  }

  if (!this->geohash_.empty() && this->out_geohash_ != nullptr) {
    this->out_geohash_->publish_state(this->geohash_);
    ESP_LOGD(TAG, "Published initial geohash: %s", this->geohash_.c_str());
  }

  // Register WiFi connect callback
  wifi::global_wifi_component->sta_connected_signal().add([this]() {
    ESP_LOGD(TAG, "WiFi connected, triggering update");
    this->update();
  });

  // Trigger initial update if already connected
  if (wifi::global_wifi_component->is_connected()) {
    ESP_LOGD(TAG, "WiFi already connected during setup, triggering initial update");
    this->update();
  }
#endif
}

void WeatherBOM::update() {
#ifndef USE_ESP_IDF
  return;
#else
  ESP_LOGD(TAG, "Starting update. Current geohash: '%s', have_static_lat: %d, have_static_lon: %d, have_dynamic: %d, dynamic_lat: %f, dynamic_lon: %f",
           this->geohash_.c_str(), this->have_static_lat_, this->have_static_lon_, this->have_dynamic_, this->dynamic_lat_, this->dynamic_lon_);

  if (this->running_) {
    ESP_LOGD(TAG, "Fetch task already running, skipping");
    return;
  }

  // For dynamic locations, check if coords changed
  if (this->lat_sensor_ != nullptr && this->lon_sensor_ != nullptr && this->have_dynamic_) {
    if (this->dynamic_lat_ != this->last_lat_ || this->dynamic_lon_ != this->last_lon_) {
      ESP_LOGD(TAG, "GPS coordinates changed, resetting geohash for re-resolution");
      this->geohash_ = "";
    }
  }

  this->running_ = true;
  xTaskCreate(fetch_task, "bom_fetch", 8192, this, 5, NULL);
#endif
}

void WeatherBOM::fetch_task(void *pvParameters) {
  WeatherBOM *self = static_cast<WeatherBOM *>(pvParameters);
  self->do_fetch();
  vTaskDelete(NULL);
}

void WeatherBOM::do_fetch() {
#ifndef USE_ESP_IDF
  return;
#else
  bool success_obs = false, success_fc = false, success_warn = false;

  if (this->geohash_.empty()) {
    if (!this->resolve_geohash_if_needed_()) {
      ESP_LOGW(TAG, "No valid geohash yet (need lat/lon).");
      App.schedule([this]() { this->running_ = false; });
      return;
    }
    ESP_LOGD(TAG, "Resolved geohash: %s", this->geohash_.c_str());
    this->last_lat_ = this->have_static_lat_ ? this->static_lat_ : this->dynamic_lat_;
    this->last_lon_ = this->have_static_lon_ ? this->static_lon_ : this->dynamic_lon_;
  }

  // Observations
  {
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/observations";
    ESP_LOGD(TAG, "Fetching observations from: %s", url.c_str());
    if (this->fetch_url_(url, this->obs_body_)) {
      ESP_LOGD(TAG, "Fetched %d bytes for observations", this->obs_body_.size());
      success_obs = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch observations");
    }
  }

  // Forecast
  {
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/forecasts/daily";
    ESP_LOGD(TAG, "Fetching forecast from: %s", url.c_str());
    if (this->fetch_url_(url, this->fc_body_)) {
      ESP_LOGD(TAG, "Fetched %d bytes for forecast", this->fc_body_.size());
      success_fc = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch forecast");
    }
  }

  // Warnings
  {
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/warnings";
    ESP_LOGD(TAG, "Fetching warnings from: %s", url.c_str());
    if (this->fetch_url_(url, this->warn_body_)) {
      ESP_LOGD(TAG, "Fetched %d bytes for warnings", this->warn_body_.size());
      success_warn = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch warnings");
    }
  }

  // Schedule processing on main thread
  App.schedule([this, success_obs, success_fc, success_warn]() {
    this->process_data();
    if (success_obs || success_fc || success_warn) {
      this->publish_last_update_();
    } else {
      ESP_LOGW(TAG, "No successful fetches, skipping last_update");
    }
    this->running_ = false;
  });
#endif
}

void WeatherBOM::process_data() {
  this->parse_and_publish_observations_(this->obs_body_);
  this->parse_and_publish_forecast_(this->fc_body_);
  this->parse_and_publish_warnings_(this->warn_body_);
  // Clear bodies to free memory
  this->obs_body_.clear();
  this->fc_body_.clear();
  this->warn_body_.clear();
}

bool WeatherBOM::resolve_geohash_if_needed_() {
#ifndef USE_ESP_IDF
  return false;
#else
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
  snprintf(q, sizeof(q), "https://api.weather.bom.gov.au/v1/locations?search=%f,%f", lat, lon);
  ESP_LOGD(TAG, "Resolving geohash with URL: %s", q);

  std::string resp;
  if (!this->fetch_url_(q, resp)) {
    ESP_LOGW(TAG, "Failed to fetch geohash resolution response");
    return false;
  }
  ESP_LOGD(TAG, "Fetched %d bytes for geohash resolution: %.100s...", resp.size(), resp.c_str());

  cJSON *root = cJSON_ParseWithLength(resp.c_str(), resp.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse geohash JSON");
    return false;
  }

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (data && cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
    cJSON *first = cJSON_GetArrayItem(data, 0);
    cJSON *gh = cJSON_GetObjectItemCaseSensitive(first, "geohash");
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(first, "name");
    if (cJSON_IsString(gh) && gh->valuestring != nullptr) {
      this->geohash_ = gh->valuestring;
      ESP_LOGD(TAG, "Resolved geohash: %s", this->geohash_.c_str());
      if (this->out_geohash_) {
        this->out_geohash_->publish_state(this->geohash_);
        ESP_LOGD(TAG, "Published resolved geohash: %s", this->geohash_.c_str());
      }
    } else {
      ESP_LOGW(TAG, "No geohash in response");
    }
    if (cJSON_IsString(nm) && nm->valuestring != nullptr && this->location_name_) {
      this->location_name_->publish_state(nm->valuestring);
      ESP_LOGD(TAG, "Published location name: %s", nm->valuestring);
    }
  } else {
    ESP_LOGW(TAG, "No data array or empty in geohash response");
  }
  cJSON_Delete(root);

  return !this->geohash_.empty();
#endif
}

bool WeatherBOM::fetch_url_(const std::string &url, std::string &out) {
#ifndef USE_ESP_IDF
  return false;
#else
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 10000;
  cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;  // Increased buffer size
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
  ESP_LOGD(TAG, "HTTP status: %d, content_length: %d for %s", status, content_length, url.c_str());

  if (status != 200) {
    ESP_LOGW(TAG, "Non-200 status %d for %s", status, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  out.clear();
  char buf[1024];
  while (true) {
    int r = esp_http_client_read(client, buf, sizeof(buf));
    if (r < 0) {
      ESP_LOGE(TAG, "Read error: %d for %s", r, url.c_str());
      break;
    }
    if (r == 0) break;
    out.append(buf, r);
    if (out.size() > 512 * 1024) {
      ESP_LOGW(TAG, "Response too large, truncating for %s", url.c_str());
      break;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  bool success = !out.empty();
  if (!success) {
    ESP_LOGW(TAG, "Empty response for %s", url.c_str());
  }
  return success;
#endif
}

void WeatherBOM::parse_and_publish_observations_(const std::string &json) {
#ifndef USE_ESP_IDF
  return;
#else
  ESP_LOGD(TAG, "Parsing observations JSON: %.100s...", json.c_str());

  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse observations JSON");
    return;
  }

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data)) {
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(data, "temp");
    if (cJSON_IsNumber(temp)) {
      float val = (float) temp->valuedouble;
      ESP_LOGD(TAG, "Extracted temperature: %f", val);
      if (this->temperature_) this->temperature_->publish_state(val);
    } else {
      ESP_LOGD(TAG, "No temperature found");
    }

    cJSON *hum = cJSON_GetObjectItemCaseSensitive(data, "humidity");
    if (cJSON_IsNumber(hum)) {
      float val = (float) hum->valuedouble;
      ESP_LOGD(TAG, "Extracted humidity: %f", val);
      if (this->humidity_) this->humidity_->publish_state(val);
    } else {
      ESP_LOGD(TAG, "No humidity found");
    }

    float wind_val = NAN;
    cJSON *wind = cJSON_GetObjectItemCaseSensitive(data, "wind");
    if (cJSON_IsNumber(wind)) {
      wind_val = (float) wind->valuedouble;
    } else if (cJSON_IsObject(wind)) {
      cJSON *kmh = cJSON_GetObjectItemCaseSensitive(wind, "speed_kilometre");
      if (cJSON_IsNumber(kmh)) wind_val = (float) kmh->valuedouble;
    }
    if (!std::isnan(wind_val)) {
      ESP_LOGD(TAG, "Extracted wind_kmh: %f", wind_val);
      if (this->wind_kmh_) this->wind_kmh_->publish_state(wind_val);
    } else {
      ESP_LOGD(TAG, "No wind speed found");
    }
  } else {
    ESP_LOGW(TAG, "No 'data' object in observations");
  }

  cJSON_Delete(root);
#endif
}

static float _coalesce_number(cJSON *obj, const char *k1, const char *k2 = nullptr) {
  if (!obj) return NAN;
  cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, k1);
  if (cJSON_IsNumber(v)) return (float) v->valuedouble;
  if (k2 != nullptr) {
    v = cJSON_GetObjectItemCaseSensitive(obj, k2);
    if (cJSON_IsNumber(v)) return (float) v->valuedouble;
  }
  return NAN;
}

static std::string _coalesce_string(cJSON *obj, const char *k1, const char *k2 = nullptr) {
  if (!obj) return {};
  cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, k1);
  if (cJSON_IsString(v) && v->valuestring) return std::string(v->valuestring);
  if (k2 != nullptr) {
    v = cJSON_GetObjectItemCaseSensitive(obj, k2);
    if (cJSON_IsString(v) && v->valuestring) return std::string(v->valuestring);
  }
  return {};
}

void WeatherBOM::parse_and_publish_forecast_(const std::string &json) {
#ifndef USE_ESP_IDF
  return;
#else
  ESP_LOGD(TAG, "Parsing forecast JSON: %.100s...", json.c_str());

  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse forecast JSON");
    return;
  }

  cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsArray(arr)) {
    arr = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    ESP_LOGD(TAG, "Using 'forecast' instead of 'data'");
  }

  if (cJSON_IsArray(arr)) {
    int size = cJSON_GetArraySize(arr);
    ESP_LOGD(TAG, "Forecast array size: %d", size);

    cJSON *day0 = cJSON_GetArrayItem(arr, 0);
    cJSON *day1 = cJSON_GetArrayItem(arr, 1);

    auto handle_day = [&](cJSON *day, bool is_today) {
      if (!day) {
        ESP_LOGD(TAG, "No day data for %s", is_today ? "today" : "tomorrow");
        return;
      }
      float tmin = _coalesce_number(day, "temp_min", "temperature_min");
      ESP_LOGD(TAG, "%s min temp: %f", is_today ? "Today" : "Tomorrow", tmin);
      float tmax = _coalesce_number(day, "temp_max", "temperature_max");
      ESP_LOGD(TAG, "%s max temp: %f", is_today ? "Today" : "Tomorrow", tmax);
      float chance = NAN;
      std::string amount;
      cJSON *rain = cJSON_GetObjectItemCaseSensitive(day, "rain");
      if (rain && cJSON_IsObject(rain)) {
        chance = _coalesce_number(rain, "chance");
        amount = _coalesce_string(rain, "amount", "amount_text");
      } else {
        chance = _coalesce_number(day, "chance_of_rain");
        amount = _coalesce_string(day, "rain_amount");
      }
      ESP_LOGD(TAG, "%s rain chance: %f, amount: '%s'", is_today ? "Today" : "Tomorrow", chance, amount.c_str());

      std::string summary = _coalesce_string(day, "short_text", "summary");
      ESP_LOGD(TAG, "%s summary: '%s'", is_today ? "Today" : "Tomorrow", summary.c_str());
      std::string icon = _coalesce_string(day, "icon_descriptor", "icon");
      ESP_LOGD(TAG, "%s icon: '%s'", is_today ? "Today" : "Tomorrow", icon.c_str());

      if (is_today) {
        if (!std::isnan(tmin) && this->today_min_) this->today_min_->publish_state(tmin);
        if (!std::isnan(tmax) && this->today_max_) this->today_max_->publish_state(tmax);
        if (!std::isnan(chance) && this->today_rain_chance_) this->today_rain_chance_->publish_state(chance);
        if (!amount.empty() && this->today_rain_amount_) this->today_rain_amount_->publish_state(amount);
        if (!summary.empty() && this->today_summary_) this->today_summary_->publish_state(summary);
        if (!icon.empty() && this->today_icon_) this->today_icon_->publish_state(icon);
      } else {
        if (!std::isnan(tmin) && this->tomorrow_min_) this->tomorrow_min_->publish_state(tmin);
        if (!std::isnan(tmax) && this->tomorrow_max_) this->tomorrow_max_->publish_state(tmax);
        if (!std::isnan(chance) && this->tomorrow_rain_chance_) this->tomorrow_rain_chance_->publish_state(chance);
        if (!amount.empty() && this->tomorrow_rain_amount_) this->tomorrow_rain_amount_->publish_state(amount);
        if (!summary.empty() && this->tomorrow_summary_) this->tomorrow_summary_->publish_state(summary);
        if (!icon.empty() && this->tomorrow_icon_) this->tomorrow_icon_->publish_state(icon);
      }
    };

    handle_day(day0, true);
    handle_day(day1, false);
  } else {
    ESP_LOGW(TAG, "No forecast array found");
  }

  cJSON_Delete(root);
#endif
}

void WeatherBOM::parse_and_publish_warnings_(const std::string &json) {
#ifndef USE_ESP_IDF
  return;
#else
  if (!this->warnings_json_) {
    ESP_LOGD(TAG, "No warnings_json sensor configured, skipping");
    return;
  }

  ESP_LOGD(TAG, "Parsing warnings JSON: %.100s...", json.c_str());

  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse warnings JSON, publishing empty");
    this->warnings_json_->publish_state("[]");
    return;
  }

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *to_emit = data ? data : root;
  int size = cJSON_GetArraySize(to_emit);
  ESP_LOGD(TAG, "Warnings data size: %d", size);

  char *printed = cJSON_PrintUnformatted(to_emit);
  if (printed) {
    this->warnings_json_->publish_state(printed);
    ESP_LOGD(TAG, "Published warnings JSON: %.100s...", printed);
    cJSON_free(printed);
  } else {
    ESP_LOGW(TAG, "Failed to print warnings JSON, publishing empty");
    this->warnings_json_->publish_state("[]");
  }

  cJSON_Delete(root);
#endif
}

void WeatherBOM::publish_last_update_() {
#ifdef USE_ESP_IDF
  if (!last_update_) {
    ESP_LOGD(TAG, "No last_update sensor configured");
    return;
  }
  time_t now;
  time(&now);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  last_update_->publish_state(buf);
  ESP_LOGD(TAG, "Published last_update: %s", buf);
#endif
}

}  // namespace weather_bom
}  // namespace esphome