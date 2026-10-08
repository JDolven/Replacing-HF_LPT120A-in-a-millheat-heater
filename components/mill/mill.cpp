#include "mill.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mill {

static const char *const TAG = "mill";

void MillClimate::setup() {
  // Be om status med en gang i stedet for å vente på neste periodiske C9 (~60 s)
  this->queue_command_(CMD_REQUEST_STATUS, {});
}

void MillClimate::loop() {
  this->read_uart_();
  this->process_queue_();
}

void MillClimate::dump_config() {
  LOG_CLIMATE("", "Mill Heater", this);
}

climate::ClimateTraits MillClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.set_visual_min_temperature(MIN_TARGET_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_TARGET_TEMPERATURE);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

void MillClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF || mode == climate::CLIMATE_MODE_HEAT) {
      const bool on = mode == climate::CLIMATE_MODE_HEAT;
      ESP_LOGD(TAG, "Turning heater %s", on ? "on" : "off");
      this->queue_command_(CMD_SET_POWER, {static_cast<uint8_t>(on ? 0x01 : 0x00)});
      this->mode = mode;
      if (!on)
        this->action = climate::CLIMATE_ACTION_OFF;
    }
  }

  if (call.get_target_temperature().has_value()) {
    const float requested = *call.get_target_temperature();
    if (std::isfinite(requested) && requested >= MIN_TARGET_TEMPERATURE && requested <= MAX_TARGET_TEMPERATURE) {
      const auto temperature = static_cast<uint8_t>(roundf(requested));
      ESP_LOGD(TAG, "Setting target temperature to %u", temperature);
      this->queue_command_(CMD_SET_TEMPERATURE, {0x01, 0x00, temperature});
      this->target_temperature = temperature;
    } else {
      ESP_LOGW(TAG, "Ignoring target temperature %.1f outside %u-%u", requested, MIN_TARGET_TEMPERATURE,
               MAX_TARGET_TEMPERATURE);
    }
  }

  // Optimistisk oppdatering; neste C9 fra ovnen bekrefter eller korrigerer
  this->publish_state();
}

// Leser rammer basert på lengdebyten (byte 2), slik at 0x0A/0x5B i data
// eller sjekksum ikke avslutter rammen for tidlig.
void MillClimate::read_uart_() {
  const uint32_t now = millis();
  if (this->rx_length_ > 0 && now - this->last_rx_byte_ms_ > RX_TIMEOUT_MS) {
    ESP_LOGD(TAG, "Discarding incomplete frame: %s", format_hex_pretty(this->rx_buffer_.data(), this->rx_length_).c_str());
    this->rx_length_ = 0;
  }

  uint8_t byte;
  while (this->available() > 0 && this->read_byte(&byte)) {
    this->last_rx_byte_ms_ = now;

    if (this->rx_length_ == 0) {
      if (byte == START_MARKER)
        this->rx_buffer_[this->rx_length_++] = byte;
      continue;
    }

    this->rx_buffer_[this->rx_length_++] = byte;

    if (this->rx_length_ < 3)
      continue;

    const size_t frame_length = this->rx_buffer_[2];
    if (frame_length < MIN_FRAME_LENGTH || frame_length > RX_BUFFER_SIZE) {
      ESP_LOGW(TAG, "Discarding frame with invalid length %u", static_cast<unsigned>(frame_length));
      this->rx_length_ = 0;
      continue;
    }

    if (this->rx_length_ == frame_length) {
      this->handle_frame_(this->rx_buffer_.data(), frame_length);
      this->rx_length_ = 0;
    }
  }
}

void MillClimate::handle_frame_(const uint8_t *frame, size_t length) {
  if (frame[length - 1] != END_MARKER) {
    ESP_LOGW(TAG, "Discarding frame without end marker: %s", format_hex_pretty(frame, length).c_str());
    return;
  }
  const uint8_t expected = checksum_(frame + 1, length - 3);
  if (frame[length - 2] != expected) {
    ESP_LOGW(TAG, "Discarding frame with checksum 0x%02X, expected 0x%02X: %s", frame[length - 2], expected,
             format_hex_pretty(frame, length).c_str());
    return;
  }

  const uint8_t command = frame[5];
  ESP_LOGV(TAG, "RX %s", format_hex_pretty(frame, length).c_str());

  // Ovnen kvitterer en kommando ved å sende en ramme med samme kommandobyte tilbake
  if (this->awaiting_ack_ && !this->tx_queue_.empty() && command == this->tx_queue_.front()[5]) {
    ESP_LOGD(TAG, "Command 0x%02X acknowledged", command);
    this->tx_queue_.pop_front();
    this->awaiting_ack_ = false;
    this->attempts_ = 0;
    return;
  }

  switch (command) {
    case MSG_STATUS:
      this->handle_status_(frame, length);
      break;
    case MSG_ROOM_TEMPERATURE:
      if (frame[6] != 0) {
        this->current_temperature = frame[6];
        this->publish_state();
      }
      break;
    case MSG_PERIODIC:
      break;
    default:
      ESP_LOGD(TAG, "Unhandled frame 0x%02X: %s", command, format_hex_pretty(frame, length).c_str());
      break;
  }
}

void MillClimate::handle_status_(const uint8_t *frame, size_t length) {
  if (length != STATUS_FRAME_LENGTH) {
    ESP_LOGW(TAG, "Ignoring status frame with length %u", static_cast<unsigned>(length));
    return;
  }
  const uint8_t target = frame[7];
  const uint8_t room = frame[8];
  const uint8_t power = frame[10];
  const uint8_t heating = frame[12];
  if (target < MIN_TARGET_TEMPERATURE || target > MAX_TARGET_TEMPERATURE || power > 1 || heating > 1) {
    ESP_LOGW(TAG, "Ignoring implausible status frame: %s", format_hex_pretty(frame, length).c_str());
    return;
  }

  // En kommando er på vei: denne statusen kan være eldre enn kommandoen og
  // ville i så fall overskrevet den optimistiske tilstanden. Vent på ny status.
  if (!this->tx_queue_.empty()) {
    ESP_LOGV(TAG, "Ignoring status while a command is pending");
    return;
  }

  this->target_temperature = target;
  if (room != 0)
    this->current_temperature = room;
  if (power == 0) {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->action = climate::CLIMATE_ACTION_OFF;
  } else {
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->action = heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
  }
  this->publish_state();
}

// Rammeformat: 5A 00 LEN SEQ 00 CMD d0..d7 SUM 5B
void MillClimate::queue_command_(uint8_t command, std::initializer_list<uint8_t> data) {
  CommandFrame frame{};
  frame[0] = START_MARKER;
  frame[2] = COMMAND_FRAME_LENGTH;
  frame[5] = command;
  size_t i = 6;
  for (uint8_t value : data) {
    if (i >= COMMAND_FRAME_LENGTH - 2)
      break;
    frame[i++] = value;
  }
  frame[COMMAND_FRAME_LENGTH - 2] = checksum_(frame.data() + 1, COMMAND_FRAME_LENGTH - 3);
  frame[COMMAND_FRAME_LENGTH - 1] = END_MARKER;

  // Erstatt en ventende (ikke sendt) kommando av samme type, så raske klikk
  // i HA ikke bygger opp en kø
  const size_t first_unsent = this->awaiting_ack_ ? 1 : 0;
  for (size_t j = first_unsent; j < this->tx_queue_.size(); j++) {
    if (this->tx_queue_[j][5] == command) {
      this->tx_queue_[j] = frame;
      return;
    }
  }
  this->tx_queue_.push_back(frame);
}

void MillClimate::process_queue_() {
  if (this->tx_queue_.empty())
    return;

  const uint32_t now = millis();
  if (this->awaiting_ack_) {
    if (now - this->sent_at_ms_ < ACK_TIMEOUT_MS)
      return;
    if (this->attempts_ >= MAX_ATTEMPTS) {
      ESP_LOGW(TAG, "Command 0x%02X not acknowledged after %u attempts, giving up", this->tx_queue_.front()[5],
               this->attempts_);
      this->tx_queue_.pop_front();
      this->awaiting_ack_ = false;
      this->attempts_ = 0;
      return;
    }
    ESP_LOGD(TAG, "No ack for command 0x%02X, retrying", this->tx_queue_.front()[5]);
    this->awaiting_ack_ = false;
  }

  // Ikke send midt i en innkommende ramme; det var slik kommandoer gikk tapt før
  if (this->rx_length_ > 0 || now - this->last_rx_byte_ms_ < TX_IDLE_MS)
    return;

  const auto &frame = this->tx_queue_.front();
  ESP_LOGV(TAG, "TX %s", format_hex_pretty(frame.data(), frame.size()).c_str());
  this->write_array(frame.data(), frame.size());
  this->awaiting_ack_ = true;
  this->sent_at_ms_ = now;
  this->attempts_++;
}

uint8_t MillClimate::checksum_(const uint8_t *data, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 0; i < length; i++)
    sum += data[i];
  return sum;
}

}  // namespace mill
}  // namespace esphome
