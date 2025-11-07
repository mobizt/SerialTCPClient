#ifndef SERIAL_TCP_BRIDGE_ESP_H
#define SERIAL_TCP_BRIDGE_ESP_H

// --- Optimization Configuration ---
// These flags dictate the memory model of the underlying SSL client
// and should match the optimized build of ESP_SSLClient.h.
#define SSLCLIENT_INSECURE_ONLY

#include <ESP8266WiFi.h>
#include <ESP_SSLClient.h>
#include <malloc.h> // For standard malloc/free used in decoding
#include <string.h> // For strlen, strncpy, strcmp, etc.

// Helper to convert a single hex char (0-9, A-F, a-f) to its 4-bit value
uint8_t hexCharToByte(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

class SerialTCPBridgeESP
{
public:
    SerialTCPBridgeESP(Stream &serial) : serial(serial) {}

    void begin(unsigned long baud = 115200)
    {
        Serial.begin(baud);
        serial.println(F("READY"));
    }

    void loop()
    {
        handleSerialCommands();
        relayServerData();
    }
#if !defined(ENABLE_SIMULATING)
private:
#endif
    Stream &serial;
    WiFiClient baseClient;
    ESP_SSLClient sslClient;

    bool tlsStarted = false;
    bool netConnected = false;
    bool sslMode = false;
    char ssid[32] = "";
    char pass[32] = "";
    char host[64] = "";
    uint16_t port = 0;

    void handleSerialCommands()
    {
        static char cmdBuf[256];
        static size_t cmdLen = 0;

        while (serial.available())
        {
            char c = serial.read();
            if (c == '\n' || cmdLen >= sizeof(cmdBuf) - 1)
            {
                cmdBuf[cmdLen] = '\0';
                processCommand(cmdBuf);
                cmdLen = 0;
            }
            else
            {
                cmdBuf[cmdLen++] = c;
            }
        }
    }

    void processCommand(const char *cmd)
    {
        // --- SETWIFI Command ---
        if (strncmp(cmd, "SETWIFI ", 8) == 0)
        {
            const char *creds = cmd + 8;
            const char *sep = strchr(creds, ' ');
            if (sep)
            {
                size_t ssidLen = sep - creds;
                strncpy(ssid, creds, ssidLen);
                ssid[ssidLen] = '\0';
                strncpy(pass, sep + 1, sizeof(pass) - 1);
                pass[sizeof(pass) - 1] = '\0';
                serial.println(F("WIFICFGOK"));
            }
            else
            {
                serial.println(F("WIFICFGFAIL"));
            }
        }
        // --- CONNECTNET Command ---
        else if (strcmp(cmd, "CONNECTNET") == 0)
        {
            WiFi.begin(ssid, pass);
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
            {
                delay(500);
            }
            netConnected = WiFi.status() == WL_CONNECTED;
            serial.println(netConnected ? F("NETOK") : F("NETFAIL"));
        }
        // --- DISCONNECTNET Command ---
        else if (strcmp(cmd, "DISCONNECTNET") == 0)
        {
            WiFi.disconnect();
            netConnected = false;
            serial.println(F("NETDIS"));
        }
        // --- NETSTATUS Command ---
        else if (strcmp(cmd, "NETSTATUS") == 0)
        {
            serial.println(WiFi.status() == WL_CONNECTED ? F("NETOK") : F("NETFAIL"));
        }
        // --- CONNECT Command ---
        else if (strncmp(cmd, "CONNECT ", 8) == 0)
        {
            const char *args = cmd + 8;
            char mode[8] = "";
            int parsed = sscanf(args, "%63s %hu %7s", host, &port, mode);
            if (parsed >= 2)
            {
                sslMode = (parsed == 3 && strcmp(mode, "SSL") == 0);

                // Set the base client for both plain and SSL mode
                sslClient.setClient(&baseClient, sslMode);

                if (sslClient.connect(host, port))
                {
                    tlsStarted = sslMode;
                    serial.println(F("CONNECTED"));
                }
                else
                {
                    serial.println(F("ERROR"));
                }
            }
            else
            {
                serial.println(F("CONNECTFAIL"));
            }
        }
        // --- STARTTLS Command ---
        else if (strcmp(cmd, "STARTTLS") == 0)
        {
            if (!sslMode && sslClient.connectSSL())
            {
                tlsStarted = true;
                serial.println(F("TLSOK"));
            }
            else
            {
                serial.println(F("TLSFAIL"));
            }
        }
        // --- WRITE Command (Hex Decoding) ---
        else if (strncmp(cmd, "WRITE ", 6) == 0)
        {
            if (!sslClient.connected())
            {
                serial.println(F("NOTCONNECTED"));
                return;
            }

            const char *hexData = cmd + 6;
            size_t hexLen = strlen(hexData);

            size_t rawLen = hexLen / 2;

            if (rawLen == 0)
            {
                serial.println(F("WRITEOK"));
                return;
            }

            // Allocate memory for decoded data
            uint8_t *rawData = (uint8_t *)malloc(rawLen);
            if (!rawData)
            {
                serial.println(F("OOM"));
                return;
            }

            // Decode only the clean, adjusted portion of the hex string
            for (size_t i = 0; i < rawLen; i++)
            {
                uint8_t high = hexCharToByte(hexData[i * 2]);
                uint8_t low = hexCharToByte(hexData[i * 2 + 1]);
                rawData[i] = (high << 4) | low;
            }

            int sent = sslClient.write(rawData, rawLen);
            free(rawData); // Clean up temporary buffer

            if (sent == rawLen)
            {
                serial.println(F("WRITEOK"));
            }
            else
            {
                serial.println(F("WRITEFAIL"));
            }
        }
        // --- READRESP Command ---
        else if (strncmp(cmd, "READRESP ", 9) == 0)
        {
            unsigned long timeout = atoi(cmd + 9);
            unsigned long start = millis();
            while (millis() - start < timeout)
            {
                while (sslClient.available())
                {
                    int c = sslClient.read();
                    if (c != -1)
                    {
                        serial.write(c);
                        start = millis(); // reset timeout on activity
                    }
                }
                delay(10);
            }
        }
        // --- STATUS Command ---
        else if (strcmp(cmd, "STATUS") == 0)
        {
            serial.print(F("CONN:"));
            serial.print(sslClient.connected() ? "1" : "0");
            serial.print(F(" TLS:"));
            serial.print(tlsStarted ? "1" : "0");
            serial.print(F(" HOST:"));
            serial.print(host);
            serial.print(F(" PORT:"));
            serial.println(port);
        }
        // --- STOP Command ---
        else if (strcmp(cmd, "STOP") == 0)
        {
            if (sslClient.connected())
            {
                sslClient.stop();
                serial.println(F("STOPPED"));
            }
            else
            {
                serial.println(F("NOTCONNECTED"));
            }
            tlsStarted = false;
        }
        // --- UNKNOWNCMD ---
        else
        {
            serial.println(F("UNKNOWNCMD"));
        }
    }

    void relayServerData()
    {
        while (sslClient.available())
        {
            int c = sslClient.read();
            serial.write(c);
        }
    }
};

#endif