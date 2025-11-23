
<p align="center">
  <img src="https://raw.githubusercontent.com/mobizt/SerialTCPClient/refs/heads/master/assets/logo.svg" width="600" alt="SerialTCPClient Logo">
</p>

<h1 align="center">SerialTCPClient [DEPRECATED]</h1>

<h2 align="center">⚠️ DEPRECATION WARNING ⚠️</h2>

<p align="center"><strong>This library is no longer maintained and has been superseded.</strong></p>

<p align="center">Please use the new <strong>SerialNetworkBridge</strong> library for all future projects and updates.</p>

<h3 align="center">👉 <a href="https://github.com/mobizt/SerialNetworkBridge">Go to SerialNetworkBridge</a> 👈</h3>

---

## ℹ️ Migration Notice

**SerialTCPClient** has been replaced by **SerialNetworkBridge**. The new library offers improved performance, better architecture, and continued support.

If you are currently using `SerialTCPClient`, we highly recommend migrating your project to `SerialNetworkBridge`. The documentation below is kept for **legacy reference only**.

---
---

# Legacy Documentation (Archived)

<p align="center">
  <a href="https://www.arduino.cc/">
    <img src="https://img.shields.io/badge/Arduino-Library-blue.svg" alt="Arduino Library">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License: MIT">
  </a>
</p>

<p align="center">
Bring secure TCP/UDP networking to any Arduino board via a simple serial bridge.
</p>

<p align="center">
<b>SerialTCPClient</b> provides a simple way to use TCP or UDP client functionality over a serial link, enabling boards without native networking to communicate through a WiFi‑capable device.</p>

<p align="center">It is designed for Arduino boards such as AVR, STM32, and Teensy that lack built‑in WiFi or Ethernet, offering a straightforward alternative to firmware‑based solutions available only on certain boards. By bridging communication through modules like ESP32, ESP8266, Raspberry Pi Pico W, or MKR WiFi 1010, the library makes network access broadly available. With support for SSL/TLS and protocol upgrades, SerialTCPClient enables secure communication without requiring firmware‑level certificate management, making it a practical and flexible option for embedded developers.
</p>
<p align="center">
  <img src="https://raw.githubusercontent.com/mobizt/SerialTCPClient/refs/heads/master/assets/diagram.svg" alt="SerialTCPClient communication flow" width="600"/>
</p>

---

## ✨ Features

- Bridge **TCP** and **UDP** client communication via serial interface.
- Designed for **Arduino boards** that lack built‑in WiFi/Ethernet.
- Compatible with WiFi‑capable modules (ESP32/ESP8266/Raspberry Pi Pico W, MKR WiFi 1010, etc) acting as a network bridge.
- Lightweight, header‑only design for embedded use.
- Support for **SSL/TLS** (HTTPS) and **STARTTLS** upgrades.
- Example sketches included for quick start.

---

## 📦 Installation

### Arduino IDE  
You can install **SerialTCPClient** directly from the **Library Manager**:  
1. Open Arduino IDE.  
2. Go to **Sketch → Include Library → Manage Libraries…** 3. Search for **SerialTCPClient**.  
4. Click **Install**.  

### PlatformIO  
Add **SerialTCPClient** via the PlatformIO Library Registry:  
1. Open your project’s `platformio.ini`.  
2. Add the library under `lib_deps`:  

lib_deps =
    mobizt/SerialTCPClient

3. Build your project — PlatformIO will automatically fetch and install the library.


---

## 🛠 Supported Platforms

- Arduino AVR boards (e.g., Uno, Mega2560)
- ESP32 / ESP8266 (as WiFi bridge)
- Raspberry Pi Pico W
- STM32 series
- Teensy boards
- MKR WiFi 1010, MKR 1000 WiFi, Arduino UNO WiFi Rev2
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

### TCP Client Example
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