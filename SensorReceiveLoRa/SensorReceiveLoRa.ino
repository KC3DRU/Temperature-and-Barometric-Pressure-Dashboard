// Libraries for LoRa chip
#include <SPI.h>         
#include <LoRa.h>

// Libraries for the MPL3115A2 Altimeter board...
#include <Wire.h>
#include <Adafruit_MPL3115A2.h>

// Library for the Cyclical Reduncancy Check...
#include <Arduino_CRC32.h>

// Library for JSON utilities...
#include <ArduinoJson.h>

// Power by connecting Vin to 3-5V, GND to GND
// Uses I2C - connect SCL to the SCL pin, SDA to SDA pin
// See the Wire tutorial for pinouts for each Arduino
// http://arduino.cc/en/reference/wire

Adafruit_MPL3115A2 baro = Adafruit_MPL3115A2();

// Reference to the CRC routines
Arduino_CRC32 crc32;

// To indicate when we xmit...
#define ONBOARD_LED  2


#include "EspMQTTClient.h"
#include "./localWiFiSpecs.h"

EspMQTTClient client(
  NetworkName,               // Network SSID
  NetworkPassword,           // Network Password
  MqttBrokerIp,              // MQTT Broker server ip
  MqttUserName,              // MQTTUserName   Can be omitted if not needed
  MqttPassword,              // MQTTPassword   Can be omitted if not needed
  MqttClientName,            // Client name that uniquely identify your device
  MqttPort                   // The MQTT port, default to 1883. this line can be omitted
);
//EspMQTTClient client(
//  "BlackBriar2G",               // Network SSID
//  "72B96FB9F89311531469C19BAC0A21",   // Network Password
//  "192.168.133.131",            // MQTT Broker server ip
//  "",                           // MQTTUserName   Can be omitted if not needed
//  "",                           // MQTTPassword   Can be omitted if not needed
//  "",                           // Client name that uniquely identify your device
//  1883                          // The MQTT port, default to 1883. this line can be omitted
//);

//------------------------------------------------------------
// MQTT Queue Names used in this remote sensor project...
//------------------------------------------------------------
char StatusQueueName[] = "TractorShedSensorStatus";
char PayloadQueueName[] = "TractorShedSensorData";
char SensorCmdQueueName[] = "TractorShedSensorCommands";


// LoRa radio pin configuration...
const int csPin = 18;          // LoRa radio chip select
const int resetPin = 14;       // LoRa radio reset
const int irqPin = 26;         // Hardware interrupt pin

String outgoing;               // outgoing message

byte msgCount = 0;             // count of outgoing messages
byte localAddress = 0x33;      // address of this device
byte destination = 0x42;       // destination to send to
long lastSendTime = 0;         // last send time
unsigned long interval = 3000; //300000;  // interval between sends - 5 minutes
unsigned long waitBetweenLoops  =  3000;  // Wait time before checking sensors


//------------------------------------------------------------
// This function is called when the board is booted.  Set up
// the LoRa radio, WiFi connection, and MQTT connections.
//------------------------------------------------------------
void setup() {
  Serial.begin(9600);                   // initialize serial

  Serial.println("LoRa Environment Receiver ---");

  // override the default checkSum, reset, and IRQ pins (optional)
  LoRa.setPins(csPin, resetPin, irqPin);   // set checkSum, reset, IRQ pin

  if (!LoRa.begin(915E6)) {                // initialize radio at 915 MHz
    Serial.println("LoRa init failed. Check your connections.");
    while (true);                          // if failed, do nothing
  }

  Serial.println("LoRa init succeeded.");

  Serial.println("Enabeling Blue LED");
  pinMode(ONBOARD_LED, OUTPUT);


  // Optionnal functionnalities of EspMQTTClient : 
  Serial.println("Enabling MQTT stuff...");
  client.enableDebuggingMessages();   // Enable debug ging messages sent to serial output
  client.enableHTTPWebUpdater();      // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overrited with enableHTTPWebUpdater("user", "password").
  client.enableLastWillMessage("Test1/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true
}



// This function is called once everything is connected (Wifi and MQTT)
// WARNING : YOU MUST IMPLEMENT IT IF YOU USE EspMQTTClient
void onConnectionEstablished()
{
  // Execute delayed instructions
  client.executeDelayed(5 * 1000, []() {
    client.publish(StatusQueueName, "MQTT Connection started...");
  });

  
  // Subscribe to the queue I'm using in MQTT to 
  // send commands back to the remote sensor.
  client.subscribe(SensorCmdQueueName, [](const String & topic, const String & payload) {
    Serial.println("topic: " + topic + ", payload: " + payload);
    sendMessage(payload);
  });  


}


void loop() {
  client.loop();

  // parse for a packet, and call onReceive with the result:
  onReceive(LoRa.parsePacket());
  
  //delay(waitBetweenLoops);  // Wait 30 seconds

}


//------------------------------------------------------------
// This funtion handles sending a message out to the remote 
// sensor board via LoRa radio.
//------------------------------------------------------------
void sendMessage(String outgoing) {
  Serial.print("Sending a LoRa message...");
  Serial.println(outgoing);
  LoRa.beginPacket();                   // start packet
  LoRa.write(destination);              // add destination address
  LoRa.write(localAddress);             // add sender address
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(outgoing.length());        // add payload length
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++;                           // increment message ID
}



//------------------------------------------------------------
// This function defines what to do when a message is 
// received via LoRa radio.  It should be from the remote
// sensor board with data values to be passed to MQTT for
// plotting and monitoring.
//------------------------------------------------------------
void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  digitalWrite(ONBOARD_LED, HIGH); 

  // read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != localAddress && recipient != 0xFF) {
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }


  StaticJsonDocument<300> recvDoc;      // This will hold the message body.

  DeserializationError ohCrap =  deserializeJson(recvDoc, incoming);

  if (ohCrap) {
    Serial.print(F("deserializationJson() failed:"));
    Serial.println(ohCrap.f_str());
    client.publish(StatusQueueName, "!!! Deserialization error on MessageBody"); 
  }

  const char* recvPayload = recvDoc["payload"];

  // Generate a cyclical redundancy check on the payload so that the receiver
  // can validate that a good transmission happened.
  uint32_t const crc32_res = crc32.calc((uint8_t const *)recvPayload, strlen(recvPayload));

  // Grab the CRC in uppercase hex
  char checkSum[10];
  sprintf(checkSum, "%X", crc32_res);

  const char* recvCheckSum = recvDoc["crc"];
   
  Serial.print("Calculated CRC32 = ");
  Serial.print(checkSum);
  Serial.print("  ::  Received CRC32 = ");
  Serial.println(recvCheckSum);
 

  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();


  // If the checksum computed here matches the checksum 
  // sent from the remote sensor, then forward the data to
  // MQTT, otherwise whine about it...
  if (!strcmp(checkSum, recvCheckSum)) {
    client.publish(PayloadQueueName, recvPayload); 
  }else {
    Serial.println("Bad CheckSum Values");
    client.publish(StatusQueueName, "!!! - Bad CheckSum Values");   
  }
 

  // Turn off the blue LED
  digitalWrite(ONBOARD_LED, LOW); 
}
