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
  static uint8_t checksum_(const uint8_t *data, size_t length);

  std::array<uint8_t, RX_BUFFER_SIZE> rx_buffer_{};
  size_t rx_length_{0};
  uint32_t last_rx_byte_ms_{0};

  std::deque<CommandFrame> tx_queue_;
  bool awaiting_ack_{false};
  uint8_t attempts_{0};
  uint32_t sent_at_ms_{0};
};

}  // namespace mill
}  // namespace esphome
