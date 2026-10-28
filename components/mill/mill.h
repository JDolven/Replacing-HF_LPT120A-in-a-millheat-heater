#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace mill {

// Frame layout, see PROTOCOL.md:
//   5A | LEN_HI LEN_LO | MSGID | FLAG | CODE | data... | SUM | 5B
// LEN counts the whole frame including 5A and 5B. SUM is the 8-bit sum of
// every byte from LEN_HI up to and including the last data byte.
static const uint8_t FRAME_START = 0x5A;
static const uint8_t FRAME_END = 0x5B;
static const size_t FRAME_OVERHEAD = 8;  // 5A, LEN x2, MSGID, FLAG, CODE, SUM, 5B
// The MCU's registration frame (code 0x07) is 133 bytes; nothing known is longer.
static const size_t MAX_FRAME_LEN = 160;

// Byte offsets inside a frame (index 0 is the 5A start byte).
static const size_t OFFSET_LEN_HI = 1;
static const size_t OFFSET_LEN_LO = 2;
static const size_t OFFSET_MSGID = 3;
static const size_t OFFSET_FLAG = 4;
static const size_t OFFSET_CODE = 5;
static const size_t OFFSET_DATA = 6;

// Codes we understand.
static const uint8_t CODE_SET_SETPOINT = 0x46;
static const uint8_t CODE_SET_POWER = 0x47;
static const uint8_t CODE_STATUS = 0xC9;

// Wi-Fi icon on the heater display, sent by the original module as short frames.
enum WifiIcon : uint8_t {
  WIFI_ICON_UNKNOWN = 0x00,
  WIFI_ICON_GREEN_BLINK = 0x02,  // Wi-Fi connected
  WIFI_ICON_OFF = 0x03,          // Wi-Fi disconnected
  WIFI_ICON_GREEN = 0x04,        // cloud (here: Home Assistant) connected
  WIFI_ICON_RED = 0x05,          // cloud (here: Home Assistant) disconnected
};

class MillClimate : public Component, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_wifi_icon(bool wifi_icon) { this->wifi_icon_ = wifi_icon; }
  void set_sniffer(bool sniffer) { this->sniffer_ = sniffer; }
  void set_link_timeout(uint32_t link_timeout) { this->link_timeout_ = link_timeout; }

  /// False when link_timeout is set and no valid status frame has arrived within it.
  bool link_ok() const { return this->link_ok_; }

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void feed_byte_(uint8_t byte);
  void parse_buffer_();
  void drop_bytes_(size_t count);
  void skip_to_next_start_(size_t from);
  void handle_frame_(const uint8_t *frame, size_t len);
  void handle_status_(const uint8_t *frame, size_t len);
  void log_frame_(const char *direction, const uint8_t *frame, size_t len, bool force);
  void log_status_diff_(const uint8_t *frame, size_t len);

  void send_frame_(uint8_t msgid, uint8_t code, const uint8_t *data, size_t data_len);
  void send_command_(uint8_t code, uint8_t arg_index, uint8_t value);
  void update_wifi_icon_(uint32_t now);
  void check_link_(uint32_t now);

  // Receive state.
  uint8_t rx_buf_[MAX_FRAME_LEN];
  size_t rx_len_{0};
  uint32_t rx_last_byte_ms_{0};

  // Previous status frame (first 32 bytes), so the sniffer can show what changed.
  static constexpr size_t SNIFF_LEN = 32;
  uint8_t last_status_[SNIFF_LEN]{};
  size_t last_status_len_{0};

  uint8_t tx_msgid_{0};

  bool wifi_icon_{false};
  bool sniffer_{false};
  WifiIcon icon_sent_{WIFI_ICON_UNKNOWN};
  uint32_t icon_sent_ms_{0};
  bool ha_was_connected_{false};

  uint32_t link_timeout_{0};
  uint32_t last_status_ms_{0};
  bool link_ok_{true};
};

}  // namespace mill
}  // namespace esphome
