
// ESP32 code that runs as a client

// To show debug info
#define ENABLE_DEBUG_OUTPUT
#include "SerialTCPClient.h"

SerialTCPClient client(Serial2 /* Serial */, 0 /* slot */);

int ms = 0;

void setup()
{

  Serial.begin(115200);

  Serial2.begin(115200, SERIAL_8N1, 16 /* rx */, 17 /* tx */);

  // Or set it later
  // client.begin(Serial2);
  // client.setSlot(0 /* slot */);

  client.setWiFi("WIFI_SSID", "WIFI_PASSWORD");
  client.connectNetwork();
  client.setAutoReconnect(true);
  client.setAutoReconnect(true);
  client.setRetryLimit(3);
  client.setRetryDelay(7000);
  client.setDebugLevel(2);
}

void loop()
{

  if (ms == 0 || (millis() - ms > 60 * 5 * 1000))
  {
    ms = millis();

    if (client.connect("reqres.in", 443))
    {

      const char *request_part1 =
          "POST /api/users HTTP/1.1\r\n"
          "Host: reqres.in\r\n"
          "x-api-key: reqres-free-v1\r\n"
          "Content-Type: application/json\r\n";

      const char *request_part2 =
          "Content-Length: 34\r\n"
          "Connection: close\r\n"
          "\r\n"
          "{\"name\":\"morpheus\",\"job\":\"leader\"}";

      client.write((const uint8_t *)request_part1, strlen(request_part1));
      client.write((const uint8_t *)request_part2, strlen(request_part2));

      while (client.available() == 0)
        delay(0);

      while (client.available() > 0)
      {
        int c = client.read();
        if (c > 0)
        {
          Serial.print((char)c);
        }
      }

      Serial.println();
    }

    Serial.println();
    Serial.println("✅ Finished");
  }
}