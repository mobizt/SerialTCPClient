#include <Arduino.h>

#define ENABLE_SIMULATING
#include "SerialTCPBridgeESP.h"
SerialTCPBridgeESP bridge(Serial);

unsigned long ms = 0;
int step = 0;

void setup()
{
  bridge.begin();
}

void byteToHex(uint8_t b, char *hex)
{
  // Uses uppercase hex characters
  const char *hex_chars = "0123456789ABCDEF";
  hex[0] = hex_chars[(b >> 4) & 0x0F]; // High nibble
  hex[1] = hex_chars[b & 0x0F];        // Low nibble
}

void encodeWrite(const char *buf)
{
  size_t size = strlen(buf);
  if (size == 0)
    return;

  int index = 0;

  char *data = (char *)malloc(9 + size * 2);
  memcpy(data, "WRITE ", 6);
  index += 6;

  char hex[2];

  // Encode and send all data in one block
  for (size_t i = 0; i < size; ++i)
  {
    byteToHex(buf[i], hex);
    memcpy(data + index, hex, 2);
    index += 2;
  }

  memcpy(data + index, "\r\n\0", 3);

  bridge.processCommand(data);

  free(data);
}

void loop()
{
  bridge.loop();

  if (millis() - ms > 2000)
  {
    ms = millis();
    switch (step)
    {
    case 0:
      bridge.processCommand("SETWIFI ssid password");
      step++;
      break;
    case 1:
      bridge.processCommand("CONNECTNET");
      step++;
      break;
    case 2:
      bridge.processCommand("CONNECT reqres.in 443 SSL");
      step++;
      break;
    case 3:
      encodeWrite("POST /api/users HTTP/1.1\r\nHost: reqres.in\r\nx-api-key: reqres-free-v1\r\nContent-Type: application/json\r\nContent-Length: 34\r\nConnection: close\r\n\r\n{\"name\":\"morpheus\",\"job\":\"leader\"}");
      step++;
      break;
    case 7:
      bridge.processCommand("STOP");
      step++;
      break;
    case 12:
      bridge.processCommand("CONNECT api.github.com 443 SSL");
      step++;
      break;
    case 13:
      encodeWrite("GET /repos/mobizt/ESP_SSLClient/commits/main/status HTTP/1.1\r\nHost: api.github.com\r\nUser-Agent: ESP8266\r\nConnection: close\r\n\r\n");
      step++;
      break;
    case 17:
      bridge.processCommand("STOP");
      step++;
      break;
    default:
      step++;
      break;
    }

    if (step > 22)
      step = 2;
  }
}
