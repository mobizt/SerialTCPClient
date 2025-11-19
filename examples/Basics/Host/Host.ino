/**
 * ===============================================
 * Basic Host
 * ===============================================
 * Runs on: A device with network access (e.g., ESP8266, ESP32, Raspberry Pi Pico W).
 * Purpose: This is the default, pre-configured host sketch.
 *
 * Users must run this sketch on their host device
 * to use the simple Client examples in the
 * "Basics" folder.
 */

#include <WiFi.h> // Use WiFi.h (ESP32, Raspberry Pi Pico W) or WiFiS3.h (Uno R4), etc.
#include <WiFiClientSecure.h>

#define ENABLE_SERIALTCP_DEBUG // Enable debug prints for SerialTCPHost
#include <SerialTCPHost.h>

// Network Config
const char *ssid = "DEFAULT_WIFI_SSID";
const char *password = "DEFAULT_WIFI_PASSWORD";

// Serial TCP Host Config
const int CLIENT_SLOT = 0;       // Coresponding to Network client or SSL client slot 0 on the host
const long SERIAL_BAUD = 115200; // Coresponding to the baud rate used in the client Serial

WiFiClientSecure ssl_client; // Or WiFiClient for plain text

SerialTCPHost host(Serial2); // Use Serial2 for communication

void setup()
{
  // Start local Serial for debugging
  Serial.begin(115200);
  delay(1000);

  // Start Serial2 for communication with the client device
  Serial2.begin(SERIAL_BAUD);

  // Connect to the WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Bind the network or SSL client to slot 0.
  // Now, any commands from the client device for slot 0
  // will be routed to `ssl_client`.
  host.setClient(&ssl_client, CLIENT_SLOT);

  host.setLocalDebugLevel(1); // Enable debug prints

  // Notify the client that host is rebooted
  // Now the server connection was closed
  host.notifyBoot();

  Serial.println("Host is ready. Listening for client commands...");
}

void loop()
{
  // Reqouirements for Host operation
  host.loop();
}