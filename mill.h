#include "esphome.h"

bool newData;
char receivedChars[15];


// mill kommandoer
char settPower[] = {0x00, 0x10, 0x06, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Powertoggle er pos 5
char settTemp[] = {0x00, 0x10, 0x08, 0x00, 0x43, 0x04, 0x0A, 0x01, 0x12, 0x02}; //, 0x20, 0x03, 0x0A}; // temperatursetting er pos 8

class MyCustomClimate : public Component, public Climate {
public:
  void setup() override {
    // This will be called by App.setup()
     // Serial.begin(9600);
  }

  void loop() override {
      recvWithStartEndMarkers();
      
      if (newData == true) {
        newData = false;     
      if (receivedChars[4] == 0xC9 ) { // Filtrer ut unødig informasjon

          if (receivedChars[6] != 0 ) {
          this->target_temperature= receivedChars[6];
          }

          if (receivedChars[7] != 0 ) {
            this->current_temperature = receivedChars[7];
          }

          if (receivedChars[11] == 0x00 ) {
          this->action= climate::CLIMATE_ACTION_IDLE;
          
          } else if (receivedChars[11] == 0x01 ) {
          this->action= climate::CLIMATE_ACTION_HEATING ;
          }
          this->publish_state();
    }
  }
}

  void control(const ClimateCall &call) override {
    if (call.get_mode().has_value()) {

        switch (call.get_mode().value()) {
                case CLIMATE_MODE_OFF:
                  sendCmd(settPower, sizeof(settPower), 0x00);
                    break;
                case CLIMATE_MODE_HEAT:
                  sendCmd(settPower, sizeof(settPower), 0x01);
                    break;
        }

      ClimateMode mode = *call.get_mode();

      this->mode = mode;
      this->publish_state();
        }

    if (call.get_target_temperature().has_value()) {
      // User requested target temperature change
      int temp = *call.get_target_temperature();
      sendCmd(settTemp, sizeof(settTemp), temp);
      // ...
      this->target_temperature = temp;
      this->publish_state();

    }
    }

  

  ClimateTraits traits() override {
    // The capabilities of the climate device
    auto traits = climate::ClimateTraits();
    traits.set_supports_current_temperature(true);
    traits.set_supports_heat_mode(true);
    traits.set_supports_auto_mode(false);
    traits.set_supports_cool_mode(false);
    traits.set_supports_two_point_target_temperature(false);
    traits.set_visual_min_temperature(5);
    traits.set_visual_max_temperature(30);
    return traits;
  }

void recvWithStartEndMarkers() {
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = 0x5A;
  char endMarker = 0x5B;
  char lineend = 0x0A;
  char rc;

  if (Serial.available() > 0) {
    rc = Serial.read();
    if (recvInProgress == true) {
      if ((rc != endMarker) && (rc != lineend)) {
        // Heater_debug.publish("innhold");
        receivedChars[ndx] = (char) rc;
        // Heater_debug.publish(receivedChars[ndx]);
        ndx++;
      }
      else {
        // Heater_debug.publish("Avslutting");
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }

    else if (rc == startMarker) {
      // Heater_debug.publish("Start mark");
      recvInProgress = true;
    }
  }
}



/*--- Funksjon for summering av kontrollbyte ---*/
unsigned char checksum(char *buf, int len) {

  unsigned char chk = 0;
  for ( ; len != 0; len--) {
    chk += *buf++;
  }
  return chk;
}
/* Seriedata ut til mill mikrokontroller ---*/
void sendCmd(char* arrayen, int len, int kommando) {

  if (arrayen[4] == 0x43) { // Temperatur
    arrayen[8] = kommando;
  }
  if (arrayen[4] == 0x47) { // Power av/på
    arrayen[5] = kommando;
    arrayen[len] = (byte)0x00;  // Padding..
  }
  char crc = checksum(arrayen, len + 1);
  Serial.write((byte)0x5A); // Startbyte
  for (int i = 0; i < len + 1; i++) { // Beskjed
    Serial.write((byte)arrayen[i]);
  }
  Serial.write((byte)crc); // Kontrollbyte
  Serial.write((byte)0x5B); // Stoppbyte
}



};
