# SerialTCPClient

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

> **The Arduino bridge for TCP Client**  
> Provides a simple way to use TCP client functionality over a serial link, enabling boards without native networking to communicate through a WiFi‑capable device.

---

## ✨ Features
- Bridge TCP client communication via serial interface.
- Designed for **Arduino boards** that lack built‑in WiFi/Ethernet.
- Compatible with WiFi‑capable modules (ESP32/ESP8266) acting as a network bridge.
- Lightweight, header‑only design for embedded use.
- Example sketches included for quick start.

---

## 📦 Installation
1. Clone this repository into your Arduino `libraries` folder:
   git clone https://github.com/mobizt/SerialTCPClient.git

2. Or download the ZIP from GitHub and install via Arduino IDE:
   - Sketch → Include Library → Add .ZIP Library…

---

## 🛠 Supported Platforms
- Arduino AVR boards (e.g., Uno, Mega2560)
- ESP32 / ESP8266 (as WiFi bridge)
- Other boards with HardwareSerial support

---

## 🚀 Usage

### Client Example

```cpp
#define ENABLE_SERIALTCP_DEBUG // For debugging
#include <SerialTCPClient.h>

SerialTCPClient client(Serial2, 0 /* slot */); // Coresponding to Network client or SSL client slot 0 on the host

void setup()
{
  Serial.begin(115200);

  // The baud rate should be matched the host baud rate.
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  client.setLocalDebugLevel(1); // 0=None, 1=Enable

  if (client.connect("example.com", 443))
  {
    client.print("GET / HTTP/1.1\r\n");
    client.print("Host: example.com\r\n");
    client.print("Connection: close\r\n\r\n");

    while (client.available() == 0)
      delay(0);

    while (client.available())
    {
      int b = client.read();
      if (b >= 0)
      {
        Serial.print((char)b);
      }
    }

    client.stop();
  }
}

void loop() {}

```

### Host Example (ESP32)

```cpp
#include <WiFi.h>
#include <WiFiClientSecure.h>

#define ENABLE_SERIALTCP_DEBUG // For debugging
#include <SerialTCPHost.h>

const char* ssid     = "DEFAULT_WIFI_SSID";
const char* password = "DEFAULT_PASSWORD";

WiFiClientSecure ssl_client;
SerialTCPHost host(Serial2);

void setup() {
  Serial.begin(115200);

  // The baud rate should be matched the client baud rate.
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");

  ssl_client.setInsecure();
  ssl_client.setBufferSizes(2048, 1024);

  host.setClient(&ssl_client, 0 /* slot */); // Coresponding to slot 0 on client device

  // Notify the client that host is rebooted
  // Now the server connection was closed 
  host.notifyBoot();
}

void loop() {
  // Reqouirements for Host operation
  host.loop();
}
```
---

## 📚 API Highlights

- Constructor  
  SerialTCPClient(HardwareSerial &serial, int slot)
- connect(const char *host, uint16_t port)  
  Connect to a TCP server.
- available()  
  Returns number of bytes available to read.
- read() / read(uint8_t *buf, size_t size)  
  Read one or more bytes from the buffer.
- peek()  
  Look at the next byte without consuming it.
- flush()  
  Clear the internal buffer.
- availableForWrite()  
  Returns number of bytes that can be written.
- stop()  
  Close the connection.
- connected()  
  Check if still connected.

---

## 📂 Examples
See the examples folder for sketches demonstrating:
- Connecting to a TCP server
- Sending and receiving data
- Using with ESP32/ESP8266/Raspberry Pi Pico W as a network (WiFi) bridge

---

## ⚖️ License
This library is released under the MIT License.  
See LICENSE for details.

---

## 🙌 Contributing
Pull requests are welcome!  
Please open an issue first to discuss proposed changes or enhancements.

---

## 📧 Author
Developed and maintained by mobizt.
