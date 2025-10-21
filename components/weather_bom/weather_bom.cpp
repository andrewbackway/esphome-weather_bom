#include "weather_bom.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#endif

#include <cmath>
#include <ctime>

namespace esphome {
namespace weather_bom {

static const char *const TAG = "weather_bom";

void WeatherBOM::setup() {
#ifndef USE_ESP_IDF
  ESP_LOGE(TAG, "This component requires ESP-IDF framework.");
  return;
#else
  ESP_LOGI(TAG, "Initialising WeatherBOM component");

  // Subscribe to lat/lon sensors
  if (this->lat_sensor_ != nullptr) {
    this->lat_sensor_->add_on_state_callback([this](float v) {
      this->dynamic_lat_ = v;
      this->have_dynamic_ = !std::isnan(v) && !std::isnan(this->dynamic_lon_);
    });
  }
  if (this->lon_sensor_ != nullptr) {
    this->lon_sensor_->add_on_state_callback([this](float v) {
      this->dynamic_lon_ = v;
      this->have_dynamic_ = !std::isnan(this->dynamic_lat_) && !std::isnan(v);
    });
  }

  if (!this->geohash_.empty() && this->out_geohash_ != nullptr) {
    this->out_geohash_->publish_state(this->geohash_);
  }
#endif
}

void WeatherBOM::update() {
#ifndef USE_ESP_IDF
  return;
#else
  if (this->geohash_.empty()) {
    if (!this->resolve_geohash_if_needed_()) {
      ESP_LOGW(TAG, "No valid geohash yet (need lat/lon).");
      return;
    }
  }

  bool success_obs = false, success_fc = false, success_warn = false;

  // Observations
  {
    std::string body;
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/observations";
    if (this->fetch_url_(url, body)) {
      this->parse_and_publish_observations_(body);
      success_obs = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch observations");
    }
  }

  // Forecast
  {
    std::string body;
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/forecasts/daily";
    if (this->fetch_url_(url, body)) {
      this->parse_and_publish_forecast_(body);
      success_fc = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch forecast");
    }
  }

  // Warnings
  {
    std::string body;
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" + this->geohash_ + "/warnings";
    if (this->fetch_url_(url, body)) {
      this->parse_and_publish_warnings_(body);
      success_warn = true;
    } else {
      ESP_LOGW(TAG, "Failed to fetch warnings");
    }
  }

  // Update timestamp only if all succeeded
  if (success_obs || success_fc || success_warn) {
    this->publish_last_update_();
  }
#endif
}

bool WeatherBOM::resolve_geohash_if_needed_() {
#ifndef USE_ESP_IDF
  return false;
#else
  float lat = NAN, lon = NAN;
  if (this->have_static_lat_ && this->have_static_lon_) {
    lat = this->static_lat_;
    lon = this->static_lon_;
  } else if (this->have_dynamic_) {
    lat = this->dynamic_lat_;
    lon = this->dynamic_lon_;
  } else {
    return false;
  }

  if (std::isnan(lat) || std::isnan(lon)) return false;

  char q[128];
  snprintf(q, sizeof(q), "https://api.weather.bom.gov.au/v1/locations?search=%f,%f", lat, lon);

  std::string resp;
  if (!this->fetch_url_(q, resp)) return false;

  cJSON *root = cJSON_ParseWithLength(resp.c_str(), resp.size());
  if (!root) return false;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (data && cJSON_IsArray(data) && cJSON_GetArraySize(data) > 0) {
    cJSON *first = cJSON_GetArrayItem(data, 0);
    cJSON *gh = cJSON_GetObjectItemCaseSensitive(first, "geohash");
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(first, "name");
    if (cJSON_IsString(gh)) {
      this->geohash_ = gh->valuestring;
      if (this->out_geohash_) this->out_geohash_->publish_state(this->geohash_);
    }
    if (cJSON_IsString(nm) && this->location_name_) {
      this->location_name_->publish_state(nm->valuestring);
    }
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
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "esp_http_client_init failed for %s", url.c_str());
    return false;
  }

  esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_GET);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set_method failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open failed: %s (%s)", esp_err_to_name(err), url.c_str());
    esp_http_client_cleanup(client);
    return false;
  }

  out.clear();
  char buf[1024];
  while (true) {
    int r = esp_http_client_read(client, buf, sizeof(buf));
    if (r <= 0) break;
    out.append(buf, r);
    if (out.size() > 512 * 1024) break;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  return !out.empty();
#endif
}

void WeatherBOM::parse_and_publish_observations_(const std::string &json) {
#ifndef USE_ESP_IDF
  return;
#else
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) return;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data)) {
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(data, "temp");
    if (cJSON_IsNumber(temp) && this->temperature_) this->temperature_->publish_state((float) temp->valuedouble);

    cJSON *hum = cJSON_GetObjectItemCaseSensitive(data, "humidity");
    if (cJSON_IsNumber(hum) && this->humidity_) this->humidity_->publish_state((float) hum->valuedouble);

    float wind_val = NAN;
    cJSON *wind = cJSON_GetObjectItemCaseSensitive(data, "wind");
    if (cJSON_IsNumber(wind)) wind_val = (float) wind->valuedouble;
    if (cJSON_IsObject(wind)) {
      cJSON *kmh = cJSON_GetObjectItemCaseSensitive(wind, "speed_kilometre");
      if (cJSON_IsNumber(kmh)) wind_val = (float) kmh->valuedouble;
    }
    if (!std::isnan(wind_val) && this->wind_kmh_) this->wind_kmh_->publish_state(wind_val);
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
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) return;

  cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsArray(arr))
    arr = cJSON_GetObjectItemCaseSensitive(root, "forecast");

  if (cJSON_IsArray(arr)) {
    cJSON *day0 = cJSON_GetArrayItem(arr, 0);
    cJSON *day1 = cJSON_GetArrayItem(arr, 1);

    auto handle_day = [&](cJSON *day, bool is_today) {
      if (!day) return;
      float tmin = _coalesce_number(day, "temp_min", "temperature_min");
      float tmax = _coalesce_number(day, "temp_max", "temperature_max");
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

      std::string summary = _coalesce_string(day, "short_text", "summary");
      std::string icon = _coalesce_string(day, "icon_descriptor", "icon");

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
  }

  cJSON_Delete(root);
#endif
}

void WeatherBOM::parse_and_publish_warnings_(const std::string &json) {
#ifndef USE_ESP_IDF
  return;
#else
  if (!this->warnings_json_) return;

  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    this->warnings_json_->publish_state("[]");
    return;
  }

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *to_emit = data ? data : root;

  char *printed = cJSON_PrintUnformatted(to_emit);
  if (printed) {
    this->warnings_json_->publish_state(printed);
    cJSON_free(printed);
  } else {
    this->warnings_json_->publish_state("[]");
  }

  cJSON_Delete(root);
#endif
}

void WeatherBOM::publish_last_update_() {
#ifdef USE_ESP_IDF
  if (!last_update_) return;
  time_t now;
  time(&now);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  last_update_->publish_state(buf);
#endif
}

}  // namespace weather_bom
}  // namespace esphome
