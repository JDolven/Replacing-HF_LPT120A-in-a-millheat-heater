#include "esphome.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

#ifndef MILL_H
#define MILL_H

namespace esphome {
namespace mill {

class MillClimate : public Component, public climate::Climate {
public:
    void set_uart(uart::UARTComponent* uart) { this->uart_ = uart; }
    void setup() override;
    void loop() override;
    climate::ClimateTraits traits() override;
    void control(const climate::ClimateCall &call) override;

protected:
    uart::UARTComponent* uart_{nullptr};
    bool newData = false;
    char receivedChars[15] = {0};
    char settPower[12] = {0x00, 0x10, 0x06, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    char settTemp[12] = {0x00, 0x10, 22, 0x00, 0x46, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00};
    float target_temperature;
    float current_temperature;
    climate::ClimateMode mode;
    climate::ClimateAction action;

    void recvWithStartEndMarkers();
    unsigned char checksum(char *buf, int len);
    void sendCmd(char* arrayen, int len, int command);
};

}  // namespace mill
}  // namespace esphome

#endif  // MILL_H
