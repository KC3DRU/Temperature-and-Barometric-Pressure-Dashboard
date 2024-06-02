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


// LoRa radio pin configura0tion...
const int csPin = 18;           // LoRa radio chip select
const int resetPin = 14;        // LoRa radio reset
const int irqPin = 26;          // Hardware interrupt pin

String outgoing;                // outgoing message

byte msgCount = 0;              // count of outgoing messages
byte localAddress = 0x42;       // address of this device
byte destination = 0x33;        // destination to send to
long lastSendTime = 0;          // last send time

// Variables for message pacing.  
const long millisPerHour = 3600000;   // Millisecs per hour...
unsigned long interval = 25000; // Initial - 5 minutes



//------------------------------------------------------------
// This is called at bootup to initialize the boards and radio. 
//------------------------------------------------------------
void setup() {
  Serial.begin(9600);                   // initialize serial
  while (!Serial);

  Serial.println("LoRa Pressure Monitor ---");

  // override the default checkSum, reset, and IRQ pins (optional)
  LoRa.setPins(csPin, resetPin, irqPin);   // set checkSum, reset, IRQ pin

  if (!LoRa.begin(915E6)) {                // initialize radio at 915 MHz
    Serial.println("LoRa init failed. Check your connections.");
    while (true);                          // if failed, do nothing
  }

  Serial.println("LoRa init succeeded.");

  pinMode(ONBOARD_LED, OUTPUT);

  if (!baro.begin()) {
    Serial.println("ERROR:  Altimeter not found!");
    Serial.println(baro.begin());
  }

}



//------------------------------------------------------------
// Main loop that is repeated.  Check to see if it's time to 
// send readings.  Then look for any inbound radio messages.
// Keep the loop as clean as possible to avoid missing inbound
// messages. 
//------------------------------------------------------------
void loop() {

  if (millis() - lastSendTime > interval) {
    digitalWrite(ONBOARD_LED, HIGH);
    Serial.println("Transmitting Results");
    readSensorsAndSend();
    lastSendTime = millis();            // timestamp the message
    digitalWrite(ONBOARD_LED, LOW);    
  }

  // Check to see if anything came in via LoRa.
  onReceive(LoRa.parsePacket());
  
}



//------------------------------------------------------------
// This function will read the sensor values and send the
// message package to the home board via LoRa. 
//------------------------------------------------------------
void readSensorsAndSend(){

  if (!baro.begin()) {
    Serial.println("ERROR:  Altimeter not found!");
    Serial.println(baro.begin());
  }
  
  // Read the values for the barometric pressure
  float pascals = baro.getPressure();
  float barometerInHg = (pascals / 3377);         // Convert Pascals to inches of mercury
  float barometerMB =  (pascals * 0.01);

  // Read the value for the altitude
  float altiMeters = baro.getAltitude();
  float altiFt = altiMeters * 3.2808;

  // Read the temperature
  float tempC = baro.getTemperature();
  float tempF = ((tempC * 1.8000) + 32.00);       // Convert Celcius to Fahrenheit
  

  // Json document to hold payload fields
  StaticJsonDocument<200> payload;
  StaticJsonDocument<300> doc;

  // This section adds tags and values to the Json
  // document for each of the sensors being monitored.
  payload["pressureInHg"] = barometerInHg;
  payload["pressureMB"] = barometerMB;
  payload["altitudeFt"] = altiFt;
  payload["temperatureF"] = tempF;
  payload["messagesPerHourPace"] = (millisPerHour / interval);

  // Define char array to hold serialized payload
  char serialPayload[200];

  // Convert the Json document to a character string
  // using the "serialize" function.
  serializeJson(payload, serialPayload);

  // This character array will hold the outgoing message 
  // data...  
  char messageBody[300] = "CRC Message startPayload>";

  // Concatenate the string version of the payload to the 
  // quasi header.  Copies based on the length of the payload.
  // strncat(messageBody, msgPayload, strlen(msgPayload));

  doc["payload"] = serialPayload;
   
  // Generate a cyclical redundancy check on the payload so that the receiver
  // can validate that a good transmission happened.
  uint32_t const crc32_res = crc32.calc((uint8_t const *)serialPayload, strlen(serialPayload));

  // Denote the end of the payload and start of 
  // CRC value
  strcat(messageBody, "<endPayload  CRC=");


  // Grab the CRC in uppercase hex
  char checkSum[10];
  sprintf(checkSum, "%X", crc32_res);

  doc["crc"] = checkSum;

  // Append the check sum to the message body
  strncat(messageBody, checkSum, strlen(checkSum));

  serializeJson(doc, messageBody);

  sendMessage(messageBody); 

  Serial.print("I sent --- ");
  Serial.println(messageBody);
}



//------------------------------------------------------------
// This funtion handles sending a message out to the remote 
// sensor board via LoRa radio.
//------------------------------------------------------------
void sendMessage(String outgoing) {
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
// This funtion handles receiving a message from the home 
// controller board via LoRa radio.
//------------------------------------------------------------
void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

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

  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();

  // The only message expected is one to set the interval for 
  // sending sensor readings.  The number sent is the number of
  // messages per hour.  Convert that to a millisecond interval
  // to count how long to wait before sending a message.
  long msgPerHour;
  msgPerHour = incoming.toInt();
  Serial.print("I received a command to set the interval to: ");
  Serial.print(msgPerHour);
  Serial.println(" messages per hour...");

  interval = millisPerHour / msgPerHour;
  
  Serial.print("New interval wait time is  ");
  Serial.print(interval);
  Serial.println(" milliseconds.");

  // After setting a new pacing value, send a sensor reading back
  // so that the home system can see the new pace setting.
  readSensorsAndSend();
  
}
