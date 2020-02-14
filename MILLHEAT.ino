#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

// ---- WiFi connection ---------
#define ssid          "SSID"
#define password       "WIFI PW"

//---- Set static IP ---- Uncommet to set staticIP
/*
IPAddress ip(192, 168, 12, 100); // where xx is the desired IP Address
IPAddress gateway(192, 168, 12, 1); // set gateway to match your network
IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network
*/
// ---- MQTT connection ---------
// TODO: Configurable MQTT settings
#define AIO_SERVER      "hassio.local"
#define AIO_SERVERPORT  1883  // use 8883 for SSL
#define AIO_USERNAME    "user"
#define AIO_KEY         "key"

#define location          "/Bedrom"  //Location of the heater
#define PREAMBLE          "/Heater"   
#define cmd_OnOff         "/OnOff"    
#define cmd_Setpoint        "/Setpoint"
#define status_Setpoint        "/act_Setpoint"
#define status_ProceccVal      "/PV"
#define debugg            "/debugg"
#define S_status          "/status"

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/
// Setup a feed called for publishing.
Adafruit_MQTT_Publish Heater_pv = Adafruit_MQTT_Publish(&mqtt, PREAMBLE location status_ProceccVal ); //Temperature of the heater
Adafruit_MQTT_Publish Heater_status = Adafruit_MQTT_Publish(&mqtt, PREAMBLE location S_status );      //Is it heating? 
Adafruit_MQTT_Publish Heater_Act_PV = Adafruit_MQTT_Publish(&mqtt, PREAMBLE location status_Setpoint ); //feedback of the setpoint, (changes from buttons is detected by HA)
Adafruit_MQTT_Publish Heater_debug = Adafruit_MQTT_Publish(&mqtt, PREAMBLE location debugg );     //Need some debuginng write to this :) 

// Setup a feed called for subscribing to changes.
Adafruit_MQTT_Subscribe Heater_OnOFF = Adafruit_MQTT_Subscribe(&mqtt, PREAMBLE location cmd_OnOff); // Turn the heater Off/On 
Adafruit_MQTT_Subscribe Heater_Sp = Adafruit_MQTT_Subscribe(&mqtt, PREAMBLE location cmd_Setpoint); // Setpoint


bool newData;
char receivedChars[15];
int numChars = 12;
char settpunkt, temperatur;
char power[10];
char aktiv[10];
char pre_aktiv;
char pre_temperatur;
char pre_settpunkt;

// mill kommandoer
char settPower[] = {0x00, 0x10, 0x06, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // Powertoggle er pos 5
char settTemp[] = {0x00, 0x10, 0x08, 0x00, 0x43, 0x04, 0x0A, 0x01, 0x12, 0x02}; //, 0x20, 0x03, 0x0A}; // temperatursetting er pos 8

void setup() {
  Serial.begin(9600);

  WiFi.mode(WIFI_STA);
  // Uncommet to set staticIP
  /*
  WiFi.config(ip, gateway, subnet);
  */
  WiFi.begin(ssid, password);
  
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    //Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

   ArduinoOTA.onStart([]() {
    //Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
   // Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
   // Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR); //Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR); //Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR);// Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR);// Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR);// Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
   
  mqtt.subscribe(&Heater_OnOFF);
  mqtt.subscribe(&Heater_Sp);
}

void loop() {
  ArduinoOTA.handle();
  MQTT_connect();

  // We'll use this to determine which subscription was received.
  // In this case we only have one subscription, so we don't really need it.
  // But if you have more than one its required so we keep it in here so you dont forget
  Adafruit_MQTT_Subscribe *subscription;
  
  recvWithStartEndMarkers(); //Read serial from startmark to finish mark


  if (newData == true) {
    newData = false;     
    if (receivedChars[4] == 0xC9 ) { // Filtrer ut unødig informasjon

      if (receivedChars[6] != 0 ) {
        settpunkt = receivedChars[6];
        Heater_Act_PV.publish(settpunkt);
      }
      if (receivedChars[7] != 0 ) {
        temperatur = receivedChars[7];
        Heater_pv.publish(temperatur);
      }

      if (receivedChars[11] == 0x00 ) {
        sprintf(aktiv, "off");
        Heater_status.publish(aktiv);
      } else if (receivedChars[11] == 0x01 ) {
        sprintf(aktiv, "heat");
        Heater_status.publish(aktiv);
      }


    }
  }


  //If the reader times out, the while loop will fail.
  //However, say we do get a valid non-zero return.
  // mqtt.readSubscription(timeInMilliseconds)
  while ((subscription = mqtt.readSubscription(500))) {
    // we only care about the love
    if (subscription == &Heater_OnOFF) {
      //The message is in feedobject.lastread, convert string to int = atoi
      String text = (char *)Heater_OnOFF.lastread;
      //Serial.println(text);

      if ( text == "heat" ) {
        //Serial.println("on");
        sendCmd(settPower, sizeof(settPower), 0x01);
        // Heater_debug.publish("On");
      }
      else if (text == "off") {
        //Serial.println("OFF");
        sendCmd(settPower, sizeof(settPower), 0x00);
        // Heater_debug.publish("Off");
      }
    }

    if (subscription == &Heater_Sp) {
      char *value = (char *)Heater_Sp.lastread;
      //Serial.println(value);
      int received = atoi(value);
      // Serial.println(received);
      //Heater_debug.publish(received);
      if (5 <= received <= 35) {
        sendCmd(settTemp, sizeof(settTemp), received);
        settpunkt =received;
      }
    }
  }
}


/*--- Seriedata inn funksjon --------------*/

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

/*--- Funksjon for summering av kontrollbyte ---*/

unsigned char checksum(char *buf, int len) {

  unsigned char chk = 0;
  for ( ; len != 0; len--) {
    chk += *buf++;
  }
  return chk;
}
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }
 // Serial.println("Connecting to MQTT... ");
  //uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
     //Serial.println(mqtt.connectErrorString(ret));
     //Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
  }
 // Serial.println("MQTT Connected!");
  Heater_debug.publish("Ready");
}
void serial_dot() {
  delay(1000);
  //Serial.print(".");
}
