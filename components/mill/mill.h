#include "esphome.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

#ifndef MILL_H
#define MILL_H

namespace esphome {
namespace mill {

class MillClimate : public Component, public climate::Climate, public uart::UARTDevice {
public:

    void setup() override;
    void loop() override;
    void control(const climate::ClimateCall &call) override;

protected:
    climate::ClimateTraits traits() override;

private:
    char receivedChars[15] = {0};
    char settPower[12] = {0x00, 0x10, 0x06, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    char settTemp[12] = {0x00, 0x10, 22, 0x00, 0x46, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00};
    bool newData = false;
//    float target_temperature;
//    float current_temperature;

    void recvWithStartEndMarkers();
    unsigned char checksum(char *buf, int len);
    void sendCmd(char* arrayen, int len, int command);
};

}  // namespace mill
}  // namespace esphome

#endif  // MILL_H
