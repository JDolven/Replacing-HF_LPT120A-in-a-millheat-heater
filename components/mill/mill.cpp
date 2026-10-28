#include "mill.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome {
namespace mill {

static const char *const TAG = "mill";

static const float MIN_SETPOINT = 5.0f;
static const float MAX_SETPOINT = 30.0f;
// A frame whose bytes stop arriving for this long is abandoned (one byte takes ~1 ms at 9600 baud).
static const uint32_t RX_INTER_BYTE_TIMEOUT_MS = 100;
// The icon is re-sent this often in case the heater MCU restarted.
static const uint32_t WIFI_ICON_REFRESH_MS = 60000;
// The original module always used message ID 01 for its Wi-Fi icon frames.
static const uint8_t WIFI_ICON_MSGID = 0x01;

static uint8_t frame_checksum(const uint8_t *frame, size_t len) {
  // Sum of every byte from LEN_HI up to the last data byte (excludes 5A, SUM and 5B).
  uint8_t sum = 0;
  for (size_t i = OFFSET_LEN_HI; i + 2 < len; i++)
    sum += frame[i];
  return sum;
}

void MillClimate::setup() {
  // Nothing is sent at boot: the heater keeps its own state and the first status frame tells us what it is.
  this->last_status_ms_ = millis();
}

void MillClimate::dump_config() {
  LOG_CLIMATE("", "Mill Climate", this);
  ESP_LOGCONFIG(TAG, "  Wi-Fi icon: %s", YESNO(this->wifi_icon_));
  ESP_LOGCONFIG(TAG, "  Sniffer: %s", YESNO(this->sniffer_));
  if (this->link_timeout_ > 0) {
    ESP_LOGCONFIG(TAG, "  Link timeout: %" PRIu32 " ms", this->link_timeout_);
  }
}

void MillClimate::loop() {
  const uint32_t now = millis();
  uint8_t byte;
  while (this->available() > 0 && this->read_byte(&byte)) {
    this->rx_last_byte_ms_ = now;
    this->feed_byte_(byte);
  }
  if (this->rx_len_ > 0 && now - this->rx_last_byte_ms_ > RX_INTER_BYTE_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Incomplete frame dropped (%u bytes)", (unsigned) this->rx_len_);
    this->log_frame_("RX partial", this->rx_buf_, this->rx_len_, true);
    this->skip_to_next_start_(1);
    this->parse_buffer_();
  }
  if (this->wifi_icon_)
    this->update_wifi_icon_(now);
  this->check_link_(now);
}

void MillClimate::feed_byte_(uint8_t byte) {
  if (this->rx_len_ == 0 && byte != FRAME_START)
    return;  // between frames: wait for a start byte
  if (this->rx_len_ >= MAX_FRAME_LEN)
    this->skip_to_next_start_(1);  // cannot happen with a valid length check, but never overflow
  this->rx_buf_[this->rx_len_++] = byte;
  this->parse_buffer_();
}

void MillClimate::drop_bytes_(size_t count) {
  if (count >= this->rx_len_) {
    this->rx_len_ = 0;
    return;
  }
  std::memmove(this->rx_buf_, this->rx_buf_ + count, this->rx_len_ - count);
  this->rx_len_ -= count;
}

void MillClimate::skip_to_next_start_(size_t from) {
  size_t i = from;
  while (i < this->rx_len_ && this->rx_buf_[i] != FRAME_START)
    i++;
  this->drop_bytes_(i);
}

void MillClimate::parse_buffer_() {
  // Frames are delimited by their length field, not by scanning for 5B: 0x0A (10 degC) and 0x5B can both
  // appear as data. A bad header or checksum means we synced on a 5A inside data; retry from the next 5A.
  while (this->rx_len_ > 0) {
    if (this->rx_buf_[0] != FRAME_START) {
      this->skip_to_next_start_(0);
      continue;
    }
    if (this->rx_len_ <= OFFSET_LEN_LO)
      return;  // need the length field
    const size_t expected = (size_t(this->rx_buf_[OFFSET_LEN_HI]) << 8) | this->rx_buf_[OFFSET_LEN_LO];
    if (expected >= FRAME_OVERHEAD && expected <= MAX_FRAME_LEN) {
      if (this->rx_len_ < expected)
        return;  // wait for the rest
      const uint8_t sum = frame_checksum(this->rx_buf_, expected);
      if (this->rx_buf_[expected - 1] == FRAME_END && this->rx_buf_[expected - 2] == sum) {
        this->handle_frame_(this->rx_buf_, expected);
        this->drop_bytes_(expected);
        continue;
      }
      ESP_LOGW(TAG, "Bad frame (end 0x%02X, checksum 0x%02X, expected 0x%02X)", this->rx_buf_[expected - 1],
               this->rx_buf_[expected - 2], sum);
      this->log_frame_("RX bad", this->rx_buf_, expected, true);
    }
    this->skip_to_next_start_(1);
  }
}

void MillClimate::handle_frame_(const uint8_t *frame, size_t len) {
  const uint8_t code = frame[OFFSET_CODE];
  if (code == CODE_STATUS) {
    this->handle_status_(frame, len);
    return;
  }
  // Codes the original Wi-Fi module handled itself (registration, module ID, button combinations, ...).
  // Their meaning is not fully known yet, so only log them. See PROTOCOL.md.
  ESP_LOGI(TAG, "Unhandled frame code 0x%02X (%u bytes)", code, (unsigned) len);
  this->log_frame_("RX", frame, len, true);
}

void MillClimate::handle_status_(const uint8_t *frame, size_t len) {
  // Status data bytes: [1] setpoint, [2] room temperature, [4] mode, [6] heating element.
  static const size_t SETPOINT = OFFSET_DATA + 1, ROOM = OFFSET_DATA + 2, MODE = OFFSET_DATA + 4,
                      ELEMENT = OFFSET_DATA + 6;
  if (len < ELEMENT + 3) {
    ESP_LOGW(TAG, "Status frame too short (%u bytes)", (unsigned) len);
    this->log_frame_("RX", frame, len, true);
    return;
  }
  this->log_frame_("RX", frame, len, false);
  if (this->sniffer_)
    this->log_status_diff_(frame, len);

  this->last_status_ms_ = millis();
  if (!this->link_ok_) {
    ESP_LOGI(TAG, "Status frames are arriving again");
    this->link_ok_ = true;
    this->status_clear_warning();
  }

  bool changed = false;
  // 0 means "no reading" in this protocol.
  if (frame[SETPOINT] != 0 && this->target_temperature != frame[SETPOINT]) {
    this->target_temperature = frame[SETPOINT];
    changed = true;
  }
  if (frame[ROOM] != 0 && this->current_temperature != frame[ROOM]) {
    this->current_temperature = frame[ROOM];
    changed = true;
  }

  climate::ClimateMode mode = this->mode;
  if (frame[MODE] == 0x00) {
    mode = climate::CLIMATE_MODE_OFF;
  } else if (frame[MODE] == 0x01) {
    mode = climate::CLIMATE_MODE_HEAT;
  } else {
    ESP_LOGW(TAG, "Unknown mode byte 0x%02X, keeping previous mode", frame[MODE]);
  }

  climate::ClimateAction action;
  if (mode == climate::CLIMATE_MODE_OFF) {
    action = climate::CLIMATE_ACTION_OFF;
  } else if (frame[ELEMENT] == 0x00) {
    action = climate::CLIMATE_ACTION_IDLE;
  } else {
    if (frame[ELEMENT] != 0x01)
      ESP_LOGD(TAG, "Element byte 0x%02X treated as heating", frame[ELEMENT]);
    action = climate::CLIMATE_ACTION_HEATING;
  }

  if (mode != this->mode || action != this->action) {
    this->mode = mode;
    this->action = action;
    changed = true;
  }
  if (changed)
    this->publish_state();
}

void MillClimate::log_frame_(const char *direction, const uint8_t *frame, size_t len, bool force) {
  const bool info = this->sniffer_ || force;
#if ESPHOME_LOG_LEVEL < ESPHOME_LOG_LEVEL_VERBOSE
  if (!info)
    return;
#endif
  char hex[MAX_FRAME_LEN * 3 + 1];
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 4 <= sizeof(hex); i++)
    pos += snprintf(hex + pos, sizeof(hex) - pos, i == 0 ? "%02X" : " %02X", frame[i]);
  hex[pos] = '\0';
  if (info) {
    ESP_LOGI(TAG, "%s %s", direction, hex);
  } else {
    ESP_LOGV(TAG, "%s %s", direction, hex);
  }
}

void MillClimate::log_status_diff_(const uint8_t *frame, size_t len) {
  const size_t n = std::min(len, SNIFF_LEN);
  if (this->last_status_len_ > 0) {
    char diff[SNIFF_LEN * 14 + 32];
    size_t pos = 0;
    if (this->last_status_len_ != len)
      pos += snprintf(diff + pos, sizeof(diff) - pos, " length %u->%u", (unsigned) this->last_status_len_,
                      (unsigned) len);
    // Skip the message ID (changes every frame) and the checksum (follows every other change).
    for (size_t i = 0; i < n && i < this->last_status_len_ && pos + 14 < sizeof(diff); i++) {
      if (i == OFFSET_MSGID || i + 2 == len || frame[i] == this->last_status_[i])
        continue;
      pos += snprintf(diff + pos, sizeof(diff) - pos, " b%u %02X->%02X", (unsigned) i, this->last_status_[i],
                      frame[i]);
    }
    diff[pos] = '\0';
    if (pos > 0)
      ESP_LOGI(TAG, "Status changed:%s", diff);
  }
  std::memcpy(this->last_status_, frame, n);
  this->last_status_len_ = len;
}

void MillClimate::send_frame_(uint8_t msgid, uint8_t code, const uint8_t *data, size_t data_len) {
  const size_t len = data_len + FRAME_OVERHEAD;
  uint8_t frame[MAX_FRAME_LEN];
  if (len > sizeof(frame))
    return;
  frame[0] = FRAME_START;
  frame[OFFSET_LEN_HI] = uint8_t(len >> 8);
  frame[OFFSET_LEN_LO] = uint8_t(len & 0xFF);
  frame[OFFSET_MSGID] = msgid;
  frame[OFFSET_FLAG] = 0x00;
  frame[OFFSET_CODE] = code;
  std::memcpy(frame + OFFSET_DATA, data, data_len);
  frame[len - 2] = frame_checksum(frame, len);
  frame[len - 1] = FRAME_END;
  this->write_array(frame, len);
  this->log_frame_("TX", frame, len, false);
}

void MillClimate::send_command_(uint8_t code, uint8_t arg_index, uint8_t value) {
  // Commands carry 8 data bytes (16-byte frames), like the original Wi-Fi module sent them:
  // set setpoint 01 00 TT 00 00 00 00 00, power PP 00 00 00 00 00 00 00.
  uint8_t data[8] = {0};
  if (code == CODE_SET_SETPOINT)
    data[0] = 0x01;
  data[arg_index] = value;
  this->send_frame_(++this->tx_msgid_, code, data, sizeof(data));
}

climate::ClimateTraits MillClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.set_visual_min_temperature(MIN_SETPOINT);
  traits.set_visual_max_temperature(MAX_SETPOINT);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

void MillClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const climate::ClimateMode mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      ESP_LOGD(TAG, "Turning heater off");
      this->send_command_(CODE_SET_POWER, 0, 0x00);
      this->mode = climate::CLIMATE_MODE_OFF;
      this->action = climate::CLIMATE_ACTION_OFF;
    } else if (mode == climate::CLIMATE_MODE_HEAT) {
      ESP_LOGD(TAG, "Turning heater on");
      this->send_command_(CODE_SET_POWER, 0, 0x01);
      this->mode = climate::CLIMATE_MODE_HEAT;
      if (this->action == climate::CLIMATE_ACTION_OFF)
        this->action = climate::CLIMATE_ACTION_IDLE;
    }
  }
  if (call.get_target_temperature().has_value()) {
    // ESPHome does not enforce the visual limits, so clamp here, and round instead of truncating.
    const float requested = *call.get_target_temperature();
    const uint8_t temp = uint8_t(lroundf(clamp(requested, MIN_SETPOINT, MAX_SETPOINT)));
    ESP_LOGD(TAG, "Setting target temperature to %u (requested %.1f)", temp, requested);
    this->send_command_(CODE_SET_SETPOINT, 2, temp);
    this->target_temperature = temp;
  }
  this->publish_state();
}

void MillClimate::update_wifi_icon_(uint32_t now) {
  WifiIcon icon = WIFI_ICON_OFF;
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected())
    icon = WIFI_ICON_GREEN_BLINK;
#endif
#ifdef USE_API
  if (icon != WIFI_ICON_OFF && api::global_api_server != nullptr) {
    if (api::global_api_server->is_connected()) {
      icon = WIFI_ICON_GREEN;
      this->ha_was_connected_ = true;
    } else if (this->ha_was_connected_) {
      icon = WIFI_ICON_RED;
    }
  }
#endif
  if (icon == this->icon_sent_ && now - this->icon_sent_ms_ < WIFI_ICON_REFRESH_MS)
    return;
  // 10-byte frame: 5A 00 0A 01 00 CODE X Y SUM 5B, with X Y = F0 0F, F1 1F, F2 2F or F3 3F for codes 02..05.
  const uint8_t n = uint8_t(icon - WIFI_ICON_GREEN_BLINK);
  const uint8_t data[2] = {uint8_t(0xF0 + n), uint8_t((n << 4) | 0x0F)};
  this->send_frame_(WIFI_ICON_MSGID, icon, data, sizeof(data));
  this->icon_sent_ = icon;
  this->icon_sent_ms_ = now;
}

void MillClimate::check_link_(uint32_t now) {
  if (this->link_timeout_ == 0 || !this->link_ok_)
    return;
  if (now - this->last_status_ms_ > this->link_timeout_) {
    ESP_LOGW(TAG, "No status frame from the heater for %" PRIu32 " ms", now - this->last_status_ms_);
    this->link_ok_ = false;
    this->status_set_warning("No status from heater");
  }
}

}  // namespace mill
}  // namespace esphome
