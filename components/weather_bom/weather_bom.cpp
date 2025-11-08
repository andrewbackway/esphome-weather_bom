#include "weather_bom.h"

#include <cmath>
#include <ctime>
#include <cstdlib>
#include <string.h>

#include "jsmn.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace weather_bom {

static const char* const TAG = "weather_bom";

static int find_key(const char* json, jsmntok_t* tokens, int num_tokens, int parent, const char* key) {
  int key_len = strlen(key);
  int i = parent + 1;
  while (i < num_tokens && tokens[i].start < tokens[parent].end) {
    if (tokens[i].type == JSMN_STRING && (tokens[i].end - tokens[i].start) == key_len &&
        strncmp(json + tokens[i].start, key, key_len) == 0) {
      return i + 1;
    }
    i += tokens[i].size + 1;
  }
  return -1;
}

static float _wb_coalesce_number(const char* json, jsmntok_t* tokens, int num_tokens, int parent, const char* k1, const char* k2 = nullptr) {
  int tok = find_key(json, tokens, num_tokens, parent, k1);
  if (tok < 0 && k2 != nullptr) {
    tok = find_key(json, tokens, num_tokens, parent, k2);
  }
  if (tok < 0 || tokens[tok].type != JSMN_PRIMITIVE) return NAN;
  char* endptr;
  float val = strtof(json + tokens[tok].start, &endptr);
  if (endptr != json + tokens[tok].end) return NAN;
  return val;
}

static std::string _wb_coalesce_string(const char* json, jsmntok_t* tokens, int num_tokens, int parent, const char* k1, const char* k2 = nullptr) {
  int tok = find_key(json, tokens, num_tokens, parent, k1);
  if (tok < 0 && k2 != nullptr) {
    tok = find_key(json, tokens, num_tokens, parent, k2);
  }
  if (tok < 0 || tokens[tok].type != JSMN_STRING) return "";
  return std::string(json + tokens[tok].start, tokens[tok].end - tokens[tok].start);
}

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
  LOG_SENSOR("  ", "Today Rain Min", this->today_rain_min_);
  LOG_SENSOR("  ", "Today Rain Max", this->today_rain_max_);
  LOG_TEXT_SENSOR("  ", "Today Summary", this->today_summary_);
  LOG_TEXT_SENSOR("  ", "Today Icon", this->today_icon_);
  LOG_TEXT_SENSOR("  ", "Today Sunrise", this->today_sunrise_);
  LOG_TEXT_SENSOR("  ", "Today Sunset", this->today_sunset_);

  LOG_SENSOR("  ", "Tomorrow Min", this->tomorrow_min_);
  LOG_SENSOR("  ", "Tomorrow Max", this->tomorrow_max_);
  LOG_SENSOR("  ", "Tomorrow Rain Chance", this->tomorrow_rain_chance_);
  LOG_SENSOR("  ", "Tomorrow Rain Min", this->tomorrow_rain_min_);
  LOG_SENSOR("  ", "Tomorrow Rain Max", this->tomorrow_rain_max_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Summary", this->tomorrow_summary_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Icon", this->tomorrow_icon_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Sunrise", this->tomorrow_sunrise_);
  LOG_TEXT_SENSOR("  ", "Tomorrow Sunset", this->tomorrow_sunset_);

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
      if (this->have_dynamic_) {
        if (!this->geohash_.empty() && (fabs(v - this->last_lat_) > 0.01 || fabs(this->dynamic_lon_ - this->last_lon_) > 0.01)) {
          this->geohash_ = "";
        }
        if (this->geohash_.empty()) this->update();
      }
    });
  }

  if (this->lon_sensor_) {
    this->lon_sensor_->add_on_state_callback([this](float v) {
      this->dynamic_lon_ = v;
      this->have_dynamic_ = !std::isnan(this->dynamic_lat_) && !std::isnan(v);
      if (this->have_dynamic_) {
        if (!this->geohash_.empty() && (fabs(this->dynamic_lat_ - this->last_lat_) > 0.01 || fabs(v - this->last_lon_) > 0.01)) {
          this->geohash_ = "";
        }
        if (this->geohash_.empty()) this->update();
      }
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
  time_t now;
  time(&now);
  if (now - this->last_attempt < this->update_interval_sec) {
    ESP_LOGD(TAG, "Update throttled, skipping...");
    return;
  }
  this->last_attempt = now;

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

  BaseType_t res = xTaskCreate(&WeatherBOM::fetch_task,  // Task function
                               "bom_fetch",              // Name
                               8192,                     // Stack size (words)
                               this,                     // Parameter
                               3,                        // Priority
                               nullptr);                 // Task handle

  if (res != pdPASS) {
    ESP_LOGE(TAG, "Failed to create bom_fetch task (err=%ld)", (long)res);
    this->running_ = false;  // recover so future updates can try again
  }
}

// FreeRTOS task entry
void WeatherBOM::fetch_task(void* pv) {
  auto* self = static_cast<WeatherBOM*>(pv);
  self->do_fetch();
  self->running_ = false;
  vTaskDelete(nullptr);
}

// Main fetch routine: fetch -> process -> free, for each endpoint in turn
void WeatherBOM::do_fetch() {
  if (wifi::global_wifi_component == nullptr ||
      !wifi::global_wifi_component->is_connected()) {
    ESP_LOGW(TAG, "WiFi lost before fetch, aborting.");
    this->update_interval_sec = 60;
    return;
  }

  bool success_any = false;

  // Resolve geohash first if needed
  if (this->geohash_.empty()) {
    if (!this->resolve_geohash_if_needed_()) {
      ESP_LOGW(TAG, "Could not resolve geohash (need lat/lon)");
      this->update_interval_sec = 60;
      return;
    }
  }

  std::string body;

  // ---------------------------------------------------------------------------
  // 1) Observations
  // ---------------------------------------------------------------------------
  {
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" +
                      this->geohash_ + "/observations";

    ESP_LOGD(TAG, "Fetching observations: %s", url.c_str());
    if (this->fetch_url_(url, body)) {
      this->parse_and_publish_observations_(body);
      success_any = true;
    }

    // Free body buffer before next large fetch
    body.clear();
    std::string().swap(body);
  }

  // ---------------------------------------------------------------------------
  // 2) Daily forecast
  // ---------------------------------------------------------------------------
  {
    std::string url = "https://api.weather.bom.gov.au/v1/locations/" +
                      this->geohash_ + "/forecasts/daily";

    ESP_LOGD(TAG, "Fetching forecast: %s", url.c_str());
    if (this->fetch_url_(url, body)) {
      this->parse_and_publish_forecast_(body);
      success_any = true;
    }

    body.clear();
    std::string().swap(body);
  }

  // ---------------------------------------------------------------------------
  // 3) Warnings
  // ---------------------------------------------------------------------------
  {
    if (this->fetch_warnings) {
      std::string url = "https://api.weather.bom.gov.au/v1/locations/" +
                        this->geohash_ + "/warnings";

      ESP_LOGD(TAG, "Fetching warnings: %s", url.c_str());
      if (this->fetch_url_(url, body)) {
        this->parse_and_publish_warnings_(body);
        success_any = true;
        this->warnings_skip_count = 0;
      } else {
        // If fetch fails, don't change fetch_warnings
      }
      body.clear();
      std::string().swap(body);
    } else {
      this->warnings_skip_count++;
      if (this->warnings_skip_count >= 5) {
        this->fetch_warnings = true;
        this->warnings_skip_count = 0;
      }
    }
  }

  if (success_any) {
    this->publish_last_update_();
    this->update_interval_sec = 900;
  } else {
    ESP_LOGW(TAG, "All BOM fetches failed");
    this->update_interval_sec = 60;
  }
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

  const char* json = resp.c_str();
  jsmn_parser parser;
  jsmn_init(&parser);
  const int MAX_TOKENS = 64;
  jsmntok_t tokens[MAX_TOKENS];
  int num_tokens = jsmn_parse(&parser, json, resp.size(), tokens, MAX_TOKENS);
  if (num_tokens < 0) {
    ESP_LOGW(TAG, "Failed to parse geohash JSON");
    return false;
  }

  bool ok = false;
  int data_tok = find_key(json, tokens, num_tokens, 0, "data");
  if (data_tok >= 0 && tokens[data_tok].type == JSMN_ARRAY && tokens[data_tok].size > 0) {
    int first_tok = data_tok + 1;
    if (tokens[first_tok].type == JSMN_OBJECT) {
      int gh_tok = find_key(json, tokens, num_tokens, first_tok, "geohash");
      if (gh_tok >= 0 && tokens[gh_tok].type == JSMN_STRING) {
        std::string full_geohash = std::string(json + tokens[gh_tok].start, tokens[gh_tok].end - tokens[gh_tok].start);

        // Truncate to first 6 chars for BOM compatibility
        if (full_geohash.length() > 6) {
          ESP_LOGW(TAG,
                   "Geohash '%s' too long (%d). Truncating to '%s' for BOM API.",
                   full_geohash.c_str(), (int)full_geohash.length(),
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

      // Publish location name only if available
      int nm_tok = find_key(json, tokens, num_tokens, first_tok, "name");
      if (nm_tok >= 0 && tokens[nm_tok].type == JSMN_STRING && this->location_name_) {
        std::string name = std::string(json + tokens[nm_tok].start, tokens[nm_tok].end - tokens[nm_tok].start);
        this->location_name_->publish_state(name);
        ESP_LOGD(TAG, "Location name: %s", name.c_str());
      }
    }
  } else {
    ESP_LOGW(TAG, "No 'data' array or response was empty");
  }

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
  // Keep BOM payloads small; cap at 32 KB to bound memory.
  static constexpr size_t MAX_HTTP_BODY = 8192;

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 5000;
  cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "esp_http_client_init failed for %s", url.c_str());
    return false;
  }

  esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_GET);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set_method failed: %s for %s", esp_err_to_name(err),
             url.c_str());
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
  ESP_LOGD(TAG, "HTTP status: %d, content_length: %d for %s", status,
           content_length, url.c_str());

  if (status != 200) {
    ESP_LOGW(TAG, "Non-200 status %d for %s", status, url.c_str());
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  // If server claims a huge body, don't even try.
  if (content_length > 0 && content_length > (int)MAX_HTTP_BODY) {
    ESP_LOGW(TAG, "Content-Length %d > MAX_HTTP_BODY (%d) for %s, skipping",
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
  if (!success)
    ESP_LOGW(TAG, "Empty or truncated response for %s", url.c_str());
  return success;
}

void WeatherBOM::parse_and_publish_observations_(const std::string& json_str) {
  if (json_str.empty()) {
    ESP_LOGD(TAG, "No observations JSON to parse");
    return;
  }

  ESP_LOGD(TAG, "Parsing observations JSON: %.100s...", json_str.c_str());
  const char* json = json_str.c_str();
  jsmn_parser parser;
  jsmn_init(&parser);
  const int MAX_TOKENS = 128;
  jsmntok_t tokens[MAX_TOKENS];
  int num_tokens = jsmn_parse(&parser, json, json_str.size(), tokens, MAX_TOKENS);
  if (num_tokens < 0) {
    ESP_LOGW(TAG, "Failed to parse observations JSON");
    return;
  }

  int data_tok = find_key(json, tokens, num_tokens, 0, "data");
  if (data_tok < 0 || tokens[data_tok].type != JSMN_OBJECT) {
    ESP_LOGW(TAG, "No valid 'data' object in observations");
    return;
  }

  float temp_val = _wb_coalesce_number(json, tokens, num_tokens, data_tok, "temp");
  if (!std::isnan(temp_val) && this->temperature_) {
    ESP_LOGD(TAG, "Temperature: %f", temp_val);
    this->temperature_->publish_state(temp_val);
  }

  float rain_val = _wb_coalesce_number(json, tokens, num_tokens, data_tok, "rain_since_9am");
  if (!std::isnan(rain_val) && this->rain_since_9am_) {
    ESP_LOGD(TAG, "Rain since 9AM: %f", rain_val);
    this->rain_since_9am_->publish_state(rain_val);
  }

  float hum_val = _wb_coalesce_number(json, tokens, num_tokens, data_tok, "humidity");
  if (!std::isnan(hum_val) && this->humidity_) {
    this->humidity_->publish_state(hum_val);
  }

  float wind_val = NAN;
  int wind_tok = find_key(json, tokens, num_tokens, data_tok, "wind");
  if (wind_tok >= 0 && tokens[wind_tok].type == JSMN_OBJECT) {
    wind_val = _wb_coalesce_number(json, tokens, num_tokens, wind_tok, "speed_kilometre");
  }
  if (!std::isnan(wind_val) && this->wind_kmh_) {
    this->wind_kmh_->publish_state(wind_val);
  }
}

void WeatherBOM::parse_and_publish_forecast_(const std::string& json_str) {
  if (json_str.empty()) {
    ESP_LOGD(TAG, "No forecast JSON to parse");
    return;
  }

  ESP_LOGD(TAG, "Parsing forecast JSON: %.100s...", json_str.c_str());
  const char* json = json_str.c_str();
  jsmn_parser parser;
  jsmn_init(&parser);
  const int MAX_TOKENS = 512;
  jsmntok_t tokens[MAX_TOKENS];
  int num_tokens = jsmn_parse(&parser, json, json_str.size(), tokens, MAX_TOKENS);
  if (num_tokens < 0) {
    ESP_LOGW(TAG, "Failed to parse forecast JSON");
    return;
  }

  int arr_tok = find_key(json, tokens, num_tokens, 0, "data");
  if (arr_tok < 0) {
    arr_tok = find_key(json, tokens, num_tokens, 0, "forecast");
    if (arr_tok >= 0) ESP_LOGD(TAG, "Using 'forecast' instead of 'data'");
  }

  if (arr_tok < 0 || tokens[arr_tok].type != JSMN_ARRAY) {
    ESP_LOGW(TAG, "No forecast array found");
    return;
  }

  auto handle_day = [&](int day_tok, bool is_today) {
    if (day_tok < 0 || tokens[day_tok].type != JSMN_OBJECT) return;

    // 1. Extract values into variables first
    float tmin = _wb_coalesce_number(json, tokens, num_tokens, day_tok, "temp_min", "temperature_min");
    float tmax = _wb_coalesce_number(json, tokens, num_tokens, day_tok, "temp_max", "temperature_max");

    float rain_min = NAN;
    float rain_max = NAN;
    float rain_chance = NAN;
    std::string sunrise, sunset;
    std::string summary = _wb_coalesce_string(json, tokens, num_tokens, day_tok, "short_text", "summary");
    std::string icon = _wb_coalesce_string(json, tokens, num_tokens, day_tok, "icon_descriptor", "icon");

    // Rain values
    int rain_tok = find_key(json, tokens, num_tokens, day_tok, "rain");
    if (rain_tok >= 0 && tokens[rain_tok].type == JSMN_OBJECT) {
      rain_chance = _wb_coalesce_number(json, tokens, num_tokens, rain_tok, "chance");

      int amount_tok = find_key(json, tokens, num_tokens, rain_tok, "amount");
      if (amount_tok >= 0 && tokens[amount_tok].type == JSMN_OBJECT) {
        rain_min = _wb_coalesce_number(json, tokens, num_tokens, amount_tok, "min");
        rain_max = _wb_coalesce_number(json, tokens, num_tokens, amount_tok, "max");
      }
    }

    // Sunrise/Sunset
    int astro_tok = find_key(json, tokens, num_tokens, day_tok, "astronomical");
    if (astro_tok >= 0 && tokens[astro_tok].type == JSMN_OBJECT) {
      sunrise = _wb_coalesce_string(json, tokens, num_tokens, astro_tok, "sunrise_time");
      sunset = _wb_coalesce_string(json, tokens, num_tokens, astro_tok, "sunset_time");
    }

    // 2. Publish grouped style
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

  int i = arr_tok + 1;
  int day0_tok = -1;
  if (i < num_tokens) {
    day0_tok = i;
    i += tokens[i].size + 1;
  }
  int day1_tok = -1;
  if (i < num_tokens) {
    day1_tok = i;
  }

  handle_day(day0_tok, true);
  handle_day(day1_tok, false);
}

void WeatherBOM::parse_and_publish_warnings_(const std::string& json_str) {
  if (!this->warnings_json_) return;

  if (json_str.empty()) {
    this->warnings_json_->publish_state("[]");
    return;
  }

  ESP_LOGD(TAG, "Parsing warnings JSON: %.100s...", json_str.c_str());
  const char* json = json_str.c_str();
  jsmn_parser parser;
  jsmn_init(&parser);
  const int MAX_TOKENS = 256;
  jsmntok_t tokens[MAX_TOKENS];
  int num_tokens = jsmn_parse(&parser, json, json_str.size(), tokens, MAX_TOKENS);
  if (num_tokens < 0) {
    ESP_LOGW(TAG, "Failed to parse warnings JSON, publishing empty");
    this->warnings_json_->publish_state("[]");
    return;
  }

  int data_tok = find_key(json, tokens, num_tokens, 0, "data");
  if (data_tok < 0) {
    data_tok = 0;  // fallback to root if no 'data'
  }

  std::string warnings_json;
  if (tokens[data_tok].type == JSMN_ARRAY && tokens[data_tok].size == 0) {
    warnings_json = "[]";
    this->fetch_warnings = false;
  } else {
    warnings_json = std::string(json + tokens[data_tok].start, tokens[data_tok].end - tokens[data_tok].start);
    this->fetch_warnings = true;
  }

  static constexpr size_t MAX_WARNINGS_JSON = 2048;
  if (warnings_json.length() > MAX_WARNINGS_JSON) {
    ESP_LOGW(TAG, "Warnings JSON %u bytes > %u, truncating for publish",
             (unsigned)warnings_json.length(), (unsigned)MAX_WARNINGS_JSON);
    warnings_json = warnings_json.substr(0, MAX_WARNINGS_JSON);
  }
  this->warnings_json_->publish_state(warnings_json);
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