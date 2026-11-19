#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace mill {

// Protokollen er beskrevet i PROTOCOL.md i roten av repoet.
class MillClimate : public Component, public climate::Climate, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void control(const climate::ClimateCall &call) override;

  void set_wifi_icon(bool wifi_icon) { this->wifi_icon_ = wifi_icon; }
  void set_sniffer(bool sniffer) { this->sniffer_ = sniffer; }
  void set_link_timeout(uint32_t link_timeout) { this->link_timeout_ = link_timeout; }

  // false når link_timeout er satt og ovnen ikke har sendt noen gyldig ramme innen tiden
  bool link_ok() const { return this->link_ok_; }

 protected:
  climate::ClimateTraits traits() override;

 private:
  static constexpr uint8_t START_MARKER = 0x5A;
  static constexpr uint8_t END_MARKER = 0x5B;

  // Kommandoer ESP -> ovn
  static constexpr uint8_t CMD_REQUEST_STATUS = 0x41;
  static constexpr uint8_t CMD_SET_TEMPERATURE = 0x46;
  static constexpr uint8_t CMD_SET_POWER = 0x47;

  // Meldinger ovn -> ESP
  static constexpr uint8_t MSG_STATUS = 0xC9;
  static constexpr uint8_t MSG_ROOM_TEMPERATURE = 0xCA;
  static constexpr uint8_t MSG_PERIODIC = 0xCB;

  // WiFi-ikonet: korte rammer 5A 00 0A 01 00 KODE X Y SUM 5B, slik den originale modulen sendte dem
  static constexpr uint8_t ICON_GREEN_BLINK = 0x02;  // WiFi tilkoblet
  static constexpr uint8_t ICON_OFF = 0x03;          // WiFi frakoblet
  static constexpr uint8_t ICON_GREEN = 0x04;        // sky (her: Home Assistant) tilkoblet
  static constexpr uint8_t ICON_RED = 0x05;          // sky (her: Home Assistant) frakoblet
  static constexpr size_t ICON_FRAME_LENGTH = 10;
  static constexpr uint32_t ICON_REFRESH_MS = 60000;  // send ikonet på nytt i tilfelle ovnen har startet på nytt

  static constexpr size_t COMMAND_FRAME_LENGTH = 16;
  static constexpr size_t STATUS_FRAME_LENGTH = 17;
  static constexpr size_t MIN_FRAME_LENGTH = 8;
  static constexpr size_t RX_BUFFER_SIZE = 32;

  static constexpr uint32_t RX_TIMEOUT_MS = 100;    // maks pause mellom bytes i en ramme
  static constexpr uint32_t TX_IDLE_MS = 30;        // linja må være stille så lenge før vi sender
  static constexpr uint32_t ACK_TIMEOUT_MS = 1500;  // ovnen svarer normalt innen 100 ms
  static constexpr uint8_t MAX_ATTEMPTS = 3;

  static constexpr uint8_t MIN_TARGET_TEMPERATURE = 5;
  static constexpr uint8_t MAX_TARGET_TEMPERATURE = 35;

  using CommandFrame = std::array<uint8_t, COMMAND_FRAME_LENGTH>;

  void read_uart_();
  void handle_frame_(const uint8_t *frame, size_t length);
  void handle_status_(const uint8_t *frame, size_t length);
  void queue_command_(uint8_t command, std::initializer_list<uint8_t> data);
  void process_queue_();
  bool line_idle_(uint32_t now) const;
  void update_wifi_icon_(uint32_t now);
  void check_link_(uint32_t now);
  void log_frame_(const char *direction, const uint8_t *frame, size_t length);
  void log_status_diff_(const uint8_t *frame, size_t length);
  static uint8_t checksum_(const uint8_t *data, size_t length);

  std::array<uint8_t, RX_BUFFER_SIZE> rx_buffer_{};
  size_t rx_length_{0};
  uint32_t last_rx_byte_ms_{0};

  std::deque<CommandFrame> tx_queue_;
  bool awaiting_ack_{false};
  uint8_t attempts_{0};
  uint32_t sent_at_ms_{0};

  bool sniffer_{false};
  std::array<uint8_t, STATUS_FRAME_LENGTH> last_status_{};
  bool have_last_status_{false};

  bool wifi_icon_{false};
  uint8_t icon_sent_{0};
  uint32_t icon_sent_ms_{0};
  bool ha_was_connected_{false};

  uint32_t link_timeout_{0};
  uint32_t last_valid_rx_ms_{0};
  bool link_ok_{true};
};

}  // namespace mill
}  // namespace esphome
