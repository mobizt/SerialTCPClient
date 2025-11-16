// ESP32 code that runs as a host

#if defined(ESP32)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif

// To show debug info
#define ENABLE_DEBUG_OUTPUT
#include "SerialTCPHost.h"

WiFiClientSecure ssl_client;

SerialTCPHost host(Serial2);

void setup()
{
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16 /* rx */, 17 /* tx */);
  ssl_client.setInsecure();
  ssl_client.setBufferSizes(2048, 1024);

  host.setClient(&ssl_client, 0/* slot */);
}

void loop()
{
  host.loop();
}