
<p align="center">
  <img src="assets/logo.svg" width="600" alt="SerialTCPClient Logo">
</p>

<h1 align="center">SerialTCPClient</h1>

<p align="center">
  <a href="https://www.arduino.cc/">
    <img src="https://img.shields.io/badge/Arduino-Library-blue.svg" alt="Arduino Library">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT">
  </a>
</p>

<p align="center">
  <b>The Arduino bridge for TCP Client</b><br>
  Provides a simple way to use TCP client functionality over a serial link, enabling boards without native networking to communicate through a WiFi‑capable device.
</p>

---

## ✨ Features

- Bridge TCP client communication via serial interface.
- Designed for **Arduino boards** that lack built‑in WiFi/Ethernet.
- Compatible with WiFi‑capable modules (ESP32/ESP8266/Raspberry Pi Pico W, MKR WiFi 1010, etc) acting as a network bridge.
- Lightweight, header‑only design for embedded use.
- Example sketches included for quick start.

---

## 📦 Installation

1. Clone this repository into your Arduino `libraries` folder:
   ```bash
   git clone https://github.com/mobizt/SerialTCPClient.git
   ```

2. Or download the ZIP from GitHub and install via Arduino IDE:
   - Sketch → Include Library → Add .ZIP Library…

---

## 🛠 Supported Platforms

- Arduino AVR boards (e.g., Uno, Mega2560)
- ESP32 / ESP8266 (as WiFi bridge)
- Other boards with HardwareSerial support

---

## 🔌 Getting Started & Wiring

### Logic Level Warning ⚠️

Most Arduino AVR boards (Uno, Mega) operate at **5V**, while ESP32/ESP8266 modules operate at **3.3V**.

* **Arduino TX (5V) → ESP32 RX (3.3V):** You **MUST** use a logic level converter or a voltage divider (e.g., 10kΩ + 20kΩ resistors) to step down 5V to 3.3V. Connecting 5V directly to an ESP32 RX pin may damage it.
* **ESP32 TX (3.3V) → Arduino RX (5V):** This is usually safe directly, as 3.3V is high enough to be read as HIGH by 5V logic.

### Wiring Diagram (Arduino Uno ↔ ESP32)

| Arduino Uno (Client) | Connection | ESP32 (Host) | Note |
| :--- | :---: | :--- | :--- |
| **Pin 2 (RX)** | ← | **Pin 17 (TX)** | Direct connection usually OK |
| **Pin 3 (TX)** | → | **Pin 16 (RX)** | **Use Level Shifter (5V to 3.3V)** |
| **GND** | ↔ | **GND** | Common Ground is required |

---

## 📊 Memory Usage & Benchmarks

The following benchmarks were collected running the **Basic HTTP GET** example with `ENABLE_SERIALTCP_DEBUG` disabled.

| Platform | RAM Usage | % Used | Flash Usage | % Used |
| :--- | :--- | :--- | :--- | :--- |
| **Arduino UNO (SoftwareSerial)** | 1050 bytes | 51.3% | 8912 bytes | 27.6% |
| **Arduino Mega 2560** | 1090 bytes | 13.3% | 8060 bytes | 3.2% |
| **Raspberry Pi Pico W** | 72064 bytes | 27.5% | 97136 bytes | 9.3% |
| **ESP32** | 22684 bytes | 6.9% | 320175 bytes | 24.4% |
| **ESP8266 (SoftwareSerial)** | 30512 bytes | 37.2% | 276555 bytes | 26.5% |
| **STM32F103C8T6** | 2948 bytes | 14.4% | 14820 bytes | 11.3% |
| **Arduino MKR 1000 WiFi** | 4672 bytes | 14.3% | 31180 bytes | 11.9% |
| **Arduino UNO WiFi REV2** | 1051 bytes | 17.1% | 9331 bytes | 19.2% |
| **MKR WiFi 1010** | 5268 bytes | 16.1% | 32372 bytes | 12.3% |
| **Teensy 4.1** | 21344 bytes\* | \~4% | 41160 bytes | \~0.5% |
| **Teensy 3.6** | 6576 bytes | 2.5% | 45884 bytes | 4.4% |

*\*Teensy 4.1 RAM usage combines RAM1 and RAM2 variables.*

---


## 🚀 Usage

### Client Example

```cpp
#define ENABLE_SERIALTCP_DEBUG // For debugging
#include <SerialTCPClient.h>

SerialTCPClient client(Serial2, 0 /* slot */); // Corresponding to Network client 
                                               // or SSL client slot 0 on the host

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

void loop()
{
}
```

### Arduino UNO Client Example

```cpp
// For debugging
// Remove ENABLE_SERIALTCP_DEBUG for AVR 
// to save ram and flash usage on production
#define ENABLE_SERIALTCP_DEBUG 

#include <SoftwareSerial.h>
#include <SerialTCPClient.h>

// RX on Pin 2, TX on Pin 3
SoftwareSerial softSerial(2, 3);

SerialTCPClient client(softSerial, 0); // Corresponding to Network client
                                       // or SSL client slot 0 on the host

const char server[] = "httpbin.org";
const int port = 443; // HTTPS Port

void setup()
{

  Serial.begin(115200);
  while (!Serial)
  {
    ; // wait for serial port to connect
  }

  softSerial.begin(9600);

  Serial.print(F("Connecting to "));
  Serial.print(server);
  Serial.print(F(":"));
  Serial.println(port);

  if (client.connect(server, port))
  {
    Serial.println(F("Connected securely! Sending request..."));

    client.println(F("GET /get HTTP/1.1"));
    client.println(F("Host: httpbin.org"));
    client.println(F("Connection: close"));
    client.println(); // Empty line to end headers

    Serial.println(F("Request sent. Waiting for response..."));

    // Read Response (Inside Setup)
    // Loop until connection closes or data is processed
    while (client.available())
    {
      if (client.available())
      {
        char c = client.read();
        Serial.write(c);
      }
    }

    Serial.println();
    Serial.println(F("Server disconnected."));
    client.stop();
  }
  else
  {
    Serial.println(F("Connection failed!"));
  }

  Serial.println(F("Test Complete."));
}

void loop()
{
}
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

  host.setClient(&ssl_client, 0 /* slot */); // Corresponding to slot 0 on client device

  // Notify the client that host is rebooted
  // Now the server connection was closed 
  host.notifyBoot();
}

void loop() {
  // Requirements for Host operation
  host.loop();
}
```

---

## 📚 API Highlights

- Constructor  
  `SerialTCPClient(Stream &serial, int slot)`
- `connect(const char *host, uint16_t port)`  
  Connect to a TCP server.
- `available()`  
  Returns number of bytes available to read.
- `read()` / `read(uint8_t *buf, size_t size)`  
  Read one or more bytes from the buffer.
- `peek()`  
  Look at the next byte without consuming it.
- `flush()`  
  Clear the internal buffer.
- `availableForWrite()`  
  Returns number of bytes that can be written.
- `stop()`  
  Close the connection.
- `connected()`  
  Check if still connected.

---

## 📂 Examples

See the `examples` folder for full sketches:
- **Basics/Client/HTTP GET:** Simple HTTP GET request.
- **Basics/Client/MQTT:** Using `ArduinoMqttClient` over SerialTCPClient.
- **Basics/Host:** Host example for the host device.

---

## ⚖️ License

This library is released under the MIT License.  
See [LICENSE](LICENSE) for details.

---

## 🙌 Contributing

Pull requests are welcome!  
Please open an issue first to discuss proposed changes or enhancements.

---

## 📧 Author

Developed and maintained by mobizt.