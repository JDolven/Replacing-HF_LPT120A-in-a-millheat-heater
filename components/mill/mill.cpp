#include "mill.h"
#include "esphome/components/climate/climate.h"

using namespace esphome::uart;
using namespace esphome::climate;


namespace esphome {
namespace mill {

void MillClimate::setup() {
    ESP_LOGD("mill", "MillClimate initialized");
}

void MillClimate::loop() {
    recvWithStartEndMarkers();
    if (newData) {
        newData = false;
        if (receivedChars[4] == 0xC9) {
            if (receivedChars[6] != 0) this->target_temperature = (receivedChars[6]);
            if (receivedChars[7] != 0) this->current_temperature = (receivedChars[7]);
            if (receivedChars[9] == 0x00) this->mode = climate::CLIMATE_MODE_OFF;
            else if (receivedChars[9] == 0x01) this->mode = climate::CLIMATE_MODE_HEAT;
            if (receivedChars[11] == 0x01) this->action = climate::CLIMATE_ACTION_HEATING;
            else this->action = climate::CLIMATE_ACTION_IDLE;
            this->publish_state();
        }
    }
}

climate::ClimateTraits MillClimate::traits() {
    auto traits = climate::ClimateTraits();
    traits.set_supports_action(true);
    traits.set_supports_current_temperature(true);
    traits.set_visual_min_temperature(5);
    traits.set_visual_max_temperature(30);
    traits.set_visual_temperature_step(1);
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
    return traits;
}

void MillClimate::control(const climate::ClimateCall &call) {
    if (call.get_mode().has_value()) {
        switch (call.get_mode().value()) {
            case climate::CLIMATE_MODE_OFF:
                ESP_LOGD("mill", "Turning off heater");
                sendCmd(settPower, 11, 0x00);
                this->mode = climate::CLIMATE_MODE_OFF;
                this->action = climate::CLIMATE_ACTION_OFF;
                break;
            case climate::CLIMATE_MODE_HEAT:
                ESP_LOGD("mill", "Turning on heater");
                sendCmd(settPower, 11, 0x01);
                this->mode = climate::CLIMATE_MODE_HEAT;
                break;
            default: break;
        }
        this->publish_state();
    }
    if (call.get_target_temperature().has_value()) {
        int temp = static_cast<int>(*call.get_target_temperature());
        ESP_LOGD("mill", "Setting target temperature to %d", temp);
        sendCmd(settTemp, 11, temp);
        this->target_temperature = temp;
        this->publish_state();
    }
}

void MillClimate::recvWithStartEndMarkers() {
    static bool recv_in_progress = false;
    static uint8_t ndx = 0;
    char start_marker = 0x5A;
    char end_marker = 0x5B;
    char line_end = 0x0A;
    char rc;

    if (this->available() > 0) {
        rc = static_cast<char>(this->read());
        if (recv_in_progress) {
            if ((rc != end_marker) && (rc != line_end)) {
                receivedChars[ndx] = (char) rc;
                ndx++;
            } else {
                recv_in_progress = false;
                ndx = 0;
                newData = true;
            }
        } else if (rc == start_marker) {
            recv_in_progress = true;
        }
    }
}

unsigned char MillClimate::checksum(char *buf, int len) {
    unsigned char chk = 0;
    for (; len != 0; len--) chk += *buf++;
    return chk;
}

void MillClimate::sendCmd(char* arrayen, int len, int command) {
    if (arrayen[4] == 0x46) arrayen[7] = command;
    if (arrayen[4] == 0x47) {
        arrayen[5] = command;
        arrayen[len] = static_cast<char>(0x00);
    }
    unsigned char crc = checksum(arrayen, len + 1);
    this->write(static_cast<uint8_t>(0x5A));
    for (int i = 0; i < len + 1; i++) this->write(static_cast<uint8_t>(arrayen[i]));
    this->write(static_cast<uint8_t>(crc));
    this->write(static_cast<uint8_t>(0x5B));
}

}  // namespace mill
}  // namespace esphome
