#pragma once
#include <string>
#include <cstring>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace weather_bom {

class WeatherBOM : public PollingComponent {
 public:
  // Input setters
  void set_geohash(const std::string &g) {
    strncpy(geohash_, g.c_str(), sizeof(geohash_) - 1);
    geohash_[sizeof(geohash_) - 1] = '\0';
  }
  void set_static_lat(float v) {
    static_lat_ = v;
    have_static_lat_ = true;
  }
  void set_static_lon(float v) {
    static_lon_ = v;
    have_static_lon_ = true;
  }
  void set_lat_sensor(sensor::Sensor *s) { lat_sensor_ = s; }
  void set_lon_sensor(sensor::Sensor *s) { lon_sensor_ = s; }

  // Observations
  void set_temperature_sensor(sensor::Sensor *s) { temperature_ = s; }
  void set_humidity_sensor(sensor::Sensor *s) { humidity_ = s; }
  void set_wind_kmh_sensor(sensor::Sensor *s) { wind_kmh_ = s; }
  void set_rain_since_9am_sensor(sensor::Sensor *s) { rain_since_9am_ = s; }

  // Forecast today
  void set_today_min_sensor(sensor::Sensor *s) { today_min_ = s; }
  void set_today_max_sensor(sensor::Sensor *s) { today_max_ = s; }
  void set_today_rain_chance_sensor(sensor::Sensor *s) {
    today_rain_chance_ = s;
  }
  void set_today_rain_min_sensor(sensor::Sensor *s) { today_rain_min_ = s; }
  void set_today_rain_max_sensor(sensor::Sensor *s) { today_rain_max_ = s; }
  void set_today_summary_text(text_sensor::TextSensor *t) {
    today_summary_ = t;
  }
  void set_today_icon_text(text_sensor::TextSensor *t) { today_icon_ = t; }

  // Forecast tomorrow
  void set_tomorrow_min_sensor(sensor::Sensor *s) { tomorrow_min_ = s; }
  void set_tomorrow_max_sensor(sensor::Sensor *s) { tomorrow_max_ = s; }
  void set_tomorrow_rain_chance_sensor(sensor::Sensor *s) {
    tomorrow_rain_chance_ = s;
  }
  void set_tomorrow_rain_min_sensor(sensor::Sensor *s) {
    tomorrow_rain_min_ = s;
  }
  void set_tomorrow_rain_max_sensor(sensor::Sensor *s) {
    tomorrow_rain_max_ = s;
  }
  void set_tomorrow_summary_text(text_sensor::TextSensor *t) {
    tomorrow_summary_ = t;
  }
  void set_tomorrow_icon_text(text_sensor::TextSensor *t) {
    tomorrow_icon_ = t;
  }

  // Meta
  void set_warnings_json_text(text_sensor::TextSensor *t) {
    warnings_json_ = t;
  }
  void set_location_name_text(text_sensor::TextSensor *t) {
    location_name_ = t;
  }
  void set_out_geohash_text(text_sensor::TextSensor *t) { out_geohash_ = t; }
  void set_last_update_text(text_sensor::TextSensor *t) {
    last_update_ = t;
  }

  // Enable/disable fetching
  void set_enable_observations(bool v) { enable_observations_ = v; }
  void set_enable_forecast(bool v) { enable_forecast_ = v; }
  void set_enable_warnings(bool v) { enable_warnings_ = v; }

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

 protected:
  bool initial_fetch_done_ = false;
  bool delay_applied_ = false;

  char geohash_[8] = {0};
  char shared_buffer_[8192] = {0};
  char url_buffer_[128] = {0};
  bool have_static_lat_{false}, have_static_lon_{false};
  float static_lat_{0}, static_lon_{0};
  sensor::Sensor *lat_sensor_{nullptr};
  sensor::Sensor *lon_sensor_{nullptr};
  float dynamic_lat_{NAN}, dynamic_lon_{NAN};
  float last_lat_{NAN}, last_lon_{NAN};
  bool have_dynamic_{false};
  bool running_{false};

  // Observations
  sensor::Sensor *temperature_{nullptr};
  sensor::Sensor *humidity_{nullptr};
  sensor::Sensor *wind_kmh_{nullptr};
  sensor::Sensor *rain_since_9am_{nullptr};

  // Today forecast
  sensor::Sensor *today_min_{nullptr};
  sensor::Sensor *today_max_{nullptr};
  sensor::Sensor *today_rain_chance_{nullptr};
  sensor::Sensor *today_rain_min_{nullptr};
  sensor::Sensor *today_rain_max_{nullptr};
  text_sensor::TextSensor *today_summary_{nullptr};
  text_sensor::TextSensor *today_icon_{nullptr};

  // Tomorrow forecast
  sensor::Sensor *tomorrow_min_{nullptr};
  sensor::Sensor *tomorrow_max_{nullptr};
  sensor::Sensor *tomorrow_rain_chance_{nullptr};
  sensor::Sensor *tomorrow_rain_min_{nullptr};
  sensor::Sensor *tomorrow_rain_max_{nullptr};
  text_sensor::TextSensor *tomorrow_summary_{nullptr};
  text_sensor::TextSensor *tomorrow_icon_{nullptr};

  // Meta
  text_sensor::TextSensor *warnings_json_{nullptr};
  text_sensor::TextSensor *location_name_{nullptr};
  text_sensor::TextSensor *out_geohash_{nullptr};
  text_sensor::TextSensor *last_update_{nullptr};

  // Enable/disable fetching
  bool enable_observations_{true};
  bool enable_forecast_{true};
  bool enable_warnings_{true};

  bool resolve_geohash_if_needed_();
  bool fetch_url_(const char *url, char *out, size_t max_len);
  void parse_and_publish_observations_(const char *json);
  void parse_and_publish_forecast_(const char *json);
  void parse_and_publish_warnings_(const char *json);
  void publish_last_update_();
  void do_fetch();

  static void fetch_task(void *pv);
};

}  // namespace weather_bom
}  // namespace esphome
