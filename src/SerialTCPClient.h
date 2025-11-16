
#ifndef SERIAL_TCP_CLIENT_H
#define SERIAL_TCP_CLIENT_H

#include <Client.h>
#include <Arduino.h>
#include "SerialTCPHelper.h"

// CONSTANTS
#define MAX_FRAME_SIZE 256
#define RESPONSE_BUFFER_SIZE 256
#define MAX_CMD_ARGS_LEN 128
#define MAX_ENCODED_FRAME_SIZE 512
#define MAX_READ_BUFFER_SIZE 512

using namespace SerialTCPHelper_NS;

class SerialTCPClient : public Client
{
private:
  HardwareSerial &sink;
  int slot = -1;
  uint8_t cdata[MAX_READ_BUFFER_SIZE];
  size_t data_available = 0;
  size_t read_pos = 0;
  bool server_status = false;

  int sinkRead()
  {
    return sink.available() ? sink.read() : -1;
  }

  int sinkAvailable()
  {
    SerialTCPHelper::yield();
    return sink.available();
  }

  // Utility for sending framed commands with zero arguments
  size_t sendFramelessCommand(int slot, const char *cmd_prefix)
  {
    // Frame 0 bytes (empty string)
    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)"", 0, tx_frame_buffer, MAX_FRAME_SIZE);

    if (frame_len == 0)
      return 0;

    // Command + Space + Empty Frame
    if (slot > -1)
    {
      sink.print(slot);
      sink.print(' ');
    }
    sink.print(cmd_prefix);
    sink.print(' '); // Use char literal for space
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);
    return frame_len;
  }

  bool waitForResponse(const char *tag, unsigned long timeout = 2000)
  {
    char buffer[RESPONSE_BUFFER_SIZE];
    size_t idx = 0;
    int c = 0;
    unsigned long start = millis();

    while (millis() - start < timeout)
    {
      SerialTCPHelper::yield();

      if (sinkAvailable())
      {
        c = sinkRead();

        if (c == -1)
          continue;

        // Buffer Management
        if (idx >= RESPONSE_BUFFER_SIZE - 1)
        {
          return false; // Buffer overflow: Message too long.
        }
        buffer[idx++] = c;
        start = millis(); // Reset timeout on new data arrival

        // Check for Frame End (STOP_DELIMITER)
        if (c == STOP_DELIMITER)
        {
          buffer[idx] = '\0'; // Null-terminate the full frame

          // Call decodeData, which handles CRC validation and tag comparison.
          return decodeData(buffer, tag);
        }
      }
    }

    return false; // Timeout
  }

  bool isNumeric(const char *str)
  {
    if (*str == '\0')
      return false;
    while (*str)
    {
      if (!isdigit((unsigned char)*str))
      {
        return false;
      }
      str++;
    }
    return true;
  }

  void getStatus(const char *str, serial_bridge_status_context &status, bool &server_status)
  {
    const char *start = str;
    const char *pos;
    char buf[64];
    int count = 0;

    while ((pos = strchr(start, ' ')) != NULL)
    {
      size_t len = pos - start;
      strncpy(buf, start, len);
      buf[len] = '\0';

      if (count == 0)
      {
        status.status_length = atoi(buf) + strlen(buf) + 2;
      }
      else if (count == 1)
      {
        strcpy(status.result, buf);
      }
      else if (count == 2)
      {
        strcpy(status.caller, buf);
      }
      else if (count == 3)
        status.net_status = atoi(buf);
      else if (count == 4)
        server_status = atoi(buf);
      else if (count == 5)
        strcpy(status.ssid, buf);
      else if (count == 6)
        strcpy(status.pass, buf);
      start = pos + 1;
      count++;
    }
    strncpy(buf, start, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    strcpy(status.pass, buf);
  }

  bool decodeData(const char *encoded, const char *tag)
  {
    size_t decoded_len = 0;

    // Frame Validation (Integrity Check via CRC)
    uint8_t *decoded_status = SerialTCPHelper::deconstruct_and_validate_frame_only(encoded, &decoded_len);

    if (!decoded_status || decoded_len == 0)
    {
      return false; // Validation failed (CRC error, bad format, or empty)
    }

    serial_bridge_status_context status;
    getStatus((const char *)decoded_status, status, server_status);

    // Tag Content Match
    int ret = strncmp((char *)status.result, tag, strlen(tag)) == 0;

    // Cleanup
    free(decoded_status);

    return ret;
  }

public:
  SerialTCPClient(HardwareSerial &sink)
      : sink(sink) {}

  void begin(int slot, unsigned long baud = 115200)
  {
    this->slot = slot;
  }

  bool setWiFi(const char *ssid, const char *password)
  {

    DEBUG_STATUS("Setting WiFi (SETWIFI)... ");

    // Framing Logic
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%s %s", ssid, password);

    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
      return false;
    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)raw_args, args_len, tx_frame_buffer, MAX_FRAME_SIZE);
    if (frame_len == 0)
      return false;

    sink.print(F("SETWIFI "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (SETWIFI)\r\n" : "❌ Error (SETWIFI)\r\n");
    return ret;
  }

  bool connectNetwork()
  {

    DEBUG_STATUS("Connecting to network (CONNECTNET)... ");
    size_t sent = sendFramelessCommand(-1, "CONNECTNET");
    bool ret = sent > 0 ? waitForResponse("TRUE", 3 * 60 * 1000) : false;
    DEBUG_STATUS(ret ? "✅ Success (CONNECTNET)\r\n" : "❌ Error (CONNECTNET)\r\n");
    return ret;
  }

  bool disconnectNetwork()
  {

    DEBUG_STATUS("Disconnecting WiFi (DISCONNECTNET)... ");
    size_t sent = sendFramelessCommand(-1, "DISCONNECTNET");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
    DEBUG_STATUS(ret ? "✅ Success (DISCONNECTNET)\r\n" : "❌ Error (DISCONNECTNET)\r\n");
    return ret;
  }

  bool isNetworkConnected()
  {

    DEBUG_STATUS("Getting net status (NETSTATUS)... ");
    size_t sent = sendFramelessCommand(-1, "NETSTATUS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
    DEBUG_STATUS(ret ? "✅ Success (NETSTATUS)\r\n" : "❌ Error (NETSTATUS)\r\n");
    return ret;
  }

  bool setAutoReconnect(bool enable)
  {

    DEBUG_STATUS("Setting auto reconnect (AUTO_RECONNECT)... ");

    // Framing Logic
    const char *arg = enable ? "ON" : "OFF";
    int args_len = strlen(arg);

    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)arg, args_len, tx_frame_buffer, MAX_FRAME_SIZE);
    if (frame_len == 0)
      return false;

    sink.print(F("AUTO_RECONNECT "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (AUTO_RECONNECT)\r\n" : "❌ Error (AUTO_RECONNECT)\r\n");
    return ret;
  }

  bool setRetryLimit(int limit)
  {
    DEBUG_STATUS("Setting retry limit (RETRYLIMIT)... ");

    // Framing Logic
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%d", limit);
    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
      return false;

    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)raw_args, args_len, tx_frame_buffer, MAX_FRAME_SIZE);
    if (frame_len == 0)
      return false;

    sink.print(F("RETRYLIMIT "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (RETRYLIMIT)\r\n" : "❌ Error (RETRYLIMIT)\r\n");
    return ret;
  }

  bool setRetryDelay(unsigned long ms)
  {

    DEBUG_STATUS("Setting retry delay (RETRYDELAY)... ");

    // Framing Logic
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%lu", ms);
    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
      return false;

    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)raw_args, args_len, tx_frame_buffer, MAX_FRAME_SIZE);
    if (frame_len == 0)
      return false;

    sink.print(F("RETRYDELAY "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (RETRYDELAY)\r\n" : "❌ Error (RETRYDELAY)\r\n");
    return ret;
  }

  bool setDebugLevel(int level)
  {

    DEBUG_STATUS("Setting debug level (DEBUGLEVEL)... ");

    // Framing Logic
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%d", level);
    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
      return false;

    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)raw_args, args_len, tx_frame_buffer, MAX_FRAME_SIZE);
    if (frame_len == 0)
      return false;

    sink.print(F("DEBUGLEVEL "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (DEBUGLEVEL)\r\n" : "❌ Error (DEBUGLEVEL)\r\n");
    return ret;
  }

  int connect(const char *host, uint16_t port) override
  {

    DEBUG_STATUS("Connecting to server (CONNECT)... ");

    // Build Raw Command Arguments
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%s %u", host, port);

    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
    {
      DEBUG_STATUS("❌ Arg is too long (CONNECT)\r\n");
      return 0;
    }

    // Frame the Raw Arguments (CRC + Encoding + Delimiters)
    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame(
        (const uint8_t *)raw_args,
        args_len,
        tx_frame_buffer,
        MAX_FRAME_SIZE);

    if (frame_len == 0)
    {
      DEBUG_STATUS("❌ Frame construct (CONNECT)\r\n");
      return 0;
    }

    // Send the Full Command Packet
    sink.print(slot);
    sink.print(F(" CONNECT "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    // Wait for the framed ACK/FAIL Response
    bool ret = waitForResponse("TRUE", 30 * 1000);

    DEBUG_STATUS(ret ? "✅ Success (CONNECT)\r\n" : "❌ Error (CONNECT)\r\n");
    return ret;
  }

  int connect(IPAddress ip, uint16_t port) override
  {
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return connect(ipStr, port);
  }

  bool connect(const char *host, uint16_t port, bool use_ssl)
  {
    DEBUG_STATUS("Connecting to server (CONNECT)... ");

    // Framing Logic
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%s %u%s", host, port, use_ssl ? " SSL" : "");

    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
      return false;

    char tx_frame_buffer[MAX_FRAME_SIZE];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame((const uint8_t *)raw_args, args_len, tx_frame_buffer, MAX_FRAME_SIZE);

    if (frame_len == 0)
      return 0;

    sink.print(slot);
    sink.print(F(" CONNECT "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    bool ret = waitForResponse("TRUE", 30 * 1000);
    DEBUG_STATUS(ret ? "✅ Success (CONNECT)\r\n" : "❌ Error (CONNECT)\r\n");
    return ret;
  }

  bool startTLS(const char *tag)
  {
    DEBUG_STATUS("Starting TLS (STARTTLS)... ");
    size_t sent = sendFramelessCommand(slot, "STARTTLS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
    DEBUG_STATUS(ret ? "✅ Success (STARTTLS)\r\n" : "❌ Error (STARTTLS)\r\n");
    return ret;
  }

  size_t write(uint8_t b) override
  {
    return write(&b, 1);
  }

  size_t write(const uint8_t *buf, size_t size) override
  {

    if (size == 0)
    {
      return 0;
    }

    char tx_frame_buffer[MAX_FRAME_SIZE];

    // Construct the entire frame: < ENCODED_HEX_DATA + ENCODED_HEX_CRC >
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame(
        buf,
        size,
        tx_frame_buffer,
        MAX_FRAME_SIZE);

    if (frame_len == 0)
    {
      return 0;
    }

    DEBUG_STATUS("Writing server request (WRITE)...");

    // Transmit the command prefix: "WRITE "
    sink.print(slot);
    sink.print(F(" WRITE "));

    // Transmit the framed data string: <...>
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    // Wait for the framed ACK/FAIL Response
    int ret = waitForResponse("TRUE") ? size : 0;
    DEBUG_STATUS(ret > 0 ? "✅ Success (WRITE)\r\n" : "❌ Error (WRITE)\r\n");
    return ret;
  }

  int readResponse(uint8_t *buffer, int buffSize, unsigned long timeout = 1000)
  {

    // Buffer to hold the incoming ENCODED data frame (e.g., <AABBCCDD...>).
    static char encoded_frame_buffer[MAX_ENCODED_FRAME_SIZE];
    size_t idx = 0;

    // Send Framed Command: READRESP <ENCODED(timeout + CRC)>
    if (sinkAvailable() == 0)
    {

      DEBUG_STATUS("Reading server response (READRESP)...");

      // Build Raw Command Arguments (The timeout value)
      char raw_timeout[MAX_CMD_ARGS_LEN];
      int args_len = snprintf(raw_timeout, MAX_CMD_ARGS_LEN, "%lu", timeout);

      if (args_len <= 0)
      {
        DEBUG_STATUS("❌ Arg is too long (READRESP)\r\n");
        return -1;
      }

      // Frame the Raw Arguments
      char tx_frame_buffer[MAX_FRAME_SIZE];
      size_t frame_len = SerialTCPHelper::construct_and_encode_frame(
          (const uint8_t *)raw_timeout,
          args_len,
          tx_frame_buffer,
          MAX_FRAME_SIZE);

      if (frame_len == 0)
      {
        DEBUG_STATUS("❌ Frame construct (READRESP)\r\n");
        return -1;
      }

      // Send the Command Packet
      sink.print(slot);
      sink.print(F(" READRESP "));
      sink.write((const uint8_t *)tx_frame_buffer, frame_len);
    }

    // Block and Read Full Framed Response Payload with Timeout
    unsigned long startTime = millis();
    bool reading_frame = false;
    SerialTCPHelper::yield();

    while (millis() - startTime < timeout)
    {
      SerialTCPHelper::yield();

      if (sinkAvailable())
      {
        int c = sinkRead();
        if (c < 0)
          continue;

        // Start/Stop Delimiter Logic
        if (!reading_frame && c == START_DELIMITER)
        {
          reading_frame = true;
          idx = 0; // Reset buffer index for the new frame
        }

        if (reading_frame)
        {
          // Check for buffer overflow on the ENCODED buffer
          if (idx >= MAX_ENCODED_FRAME_SIZE - 1)
          {
            DEBUG_STATUS("❌ Buffer overflow (READRESP)\r\n");
            return -1;
          }

          encoded_frame_buffer[idx++] = c;
          startTime = millis(); // Reset timeout on new data arrival

          if (c == STOP_DELIMITER)
          {
            reading_frame = false;
            encoded_frame_buffer[idx] = '\0'; // Null-terminate
            break;
          }
        }
      }
    }

    // Check for Timeout or Incomplete Frame
    if (idx == 0 || encoded_frame_buffer[idx - 1] != STOP_DELIMITER)
    {
      DEBUG_STATUS("❌ Time out (READRESP)\r\n");
      return -1;
    }

    // Deconstruct and Validate (CRC Check)
    uint8_t *decoded_payload = nullptr;
    size_t data_len = 0;

    decoded_payload = SerialTCPHelper::deconstruct_and_validate_frame_only((const char *)encoded_frame_buffer, &data_len);

    serial_bridge_status_context status;
    getStatus((const char *)decoded_payload, status, server_status);

    if (status.status_length < data_len)
      data_len -= status.status_length;

    if (!decoded_payload || data_len == 0)
    {
      DEBUG_STATUS("❌ Unable to read response (READRESP)\r\n");
      return -1;
    }

    // Check for END signal
    if (strcmp(status.result, "END_DATA") == 0)
    {
      startTime = millis();
      free(decoded_payload);
      DEBUG_STATUS("✅ Success (READRESP)\r\n");
      return server_status ? 0 : -1; // All data streamed
    }

    // Copy Validated Data to Caller's Buffer
    if (data_len >= (size_t)buffSize)
    {
      free(decoded_payload);
      DEBUG_STATUS("❌ Buffer is too small (READRESP)\r\n");
      return -1;
    }

    memcpy(buffer, decoded_payload + status.status_length, data_len);
    buffer[data_len] = '\0';

    DEBUG_STATUS("✅ Success (READRESP)\r\n");

    // Cleanup and Return
    free(decoded_payload);
    return (int)data_len;
  }

  int read() override
  {
    uint8_t b;
    int n = read(&b, 1);
    if (n == 1)
      return b;
    return -1;
  }

  int read(uint8_t *buf, size_t size) override
  {

    if (data_available == 0)
    {
      int len = readResponse(cdata, MAX_READ_BUFFER_SIZE, 2000);
      if (len > 0)
      {
        data_available = len;
        read_pos = 0;
      }
    }

    if (data_available == 0)
      return 0;

    size_t to_copy = (size < data_available) ? size : data_available;
    memcpy(buf, cdata + read_pos, to_copy);

    read_pos += to_copy;
    data_available -= to_copy;

    return to_copy;
  }

  int available() override
  {
    SerialTCPHelper::yield();
    if (data_available == 0)
    {
      int len = readResponse(cdata, MAX_READ_BUFFER_SIZE);
      if (len > 0)
      {
        data_available = len;
        read_pos = 0;
      }
    }
    return data_available;
  }

  void stop() override
  {
    DEBUG_STATUS("Stopping connection... (STOP)");
    sendFramelessCommand(slot, "STOP");
    bool ret = waitForResponse("TRUE");
    DEBUG_STATUS(ret ? "✅ Success (STOP)\r\n" : "❌ Error (STOP)\r\n");
  }

  uint8_t connected() override
  {
    DEBUG_STATUS("Checking server status (SERVERSTATUS)... ");
    size_t sent = sendFramelessCommand(slot, "SERVERSTATUS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
    DEBUG_STATUS(ret ? "✅ Connected (SERVERSTATUS)\r\n" : "❌ Disconnected (SERVERSTATUS)\r\n");
    return ret;
  }

  operator bool() override
  {
    return connected() != 0;
  }

  void flush() override
  {
    // Clear the buffer
    data_available = 0;
    read_pos = 0;
  }

  int peek() override
  {

    if (data_available == 0)
    {
      int len = readResponse(cdata, MAX_READ_BUFFER_SIZE);
      if (len > 0)
      {
        data_available = len;
        read_pos = 0;
      }
    }

    if (data_available == 0)
      return -1;

    return cdata[read_pos];
  }

  int availableForWrite() override
  {
    return MAX_FRAME_SIZE - 45;
  }
};

#endif