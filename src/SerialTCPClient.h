
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

public:
  /**
   * @brief Constructor for SerialTCPClient.
   * @param sink The HardwareSerial interface (e.g., Serial1) used for communication.
   * @param slot The specific client slot ID to use on the serial bridge.
   */
  SerialTCPClient(HardwareSerial &sink, int slot)
      : sink(sink), slot(slot) {}

  /**
   * @brief Constructor for SerialTCPClient (unslotted).
   * @param sink The HardwareSerial interface (e.g., Serial1) used for communication.
   */
  SerialTCPClient(HardwareSerial &sink)
      : sink(sink) {}

  /**
   * @brief (Re)initializes the client with a specific HardwareSerial interface.
   * @param serial The HardwareSerial interface (e.g., Serial1) to bind to.
   */
  void begin(HardwareSerial &serial)
  {
    this->sink = serial;
  }

  /**
   * @brief Sets the client slot ID after initialization.
   * @param slot The client slot ID to use.
   */
  void setSlot(int slot)
  {
    this->slot = slot;
  }

  /**
   * @brief Sets the WiFi credentials (SSID and password) on the serial bridge device.
   * @param ssid The WiFi network name.
   * @param password The WiFi network password.
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool setWiFi(const char *ssid, const char *password)
  {

#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Setting WiFi (SETWIFI)... ");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (SETWIFI)\r\n" : "❌ Error (SETWIFI)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Commands the serial bridge to connect to the configured WiFi network.
   * @return true if the connection was acknowledged successfully, false otherwise.
   */
  bool connectNetwork()
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Connecting to network (CONNECTNET)... ");
#endif

    size_t sent = sendFramelessCommand(-1, "CONNECTNET");
    bool ret = sent > 0 ? waitForResponse("TRUE", 3 * 60 * 1000) : false;

#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (CONNECTNET)\r\n" : "❌ Error (CONNECTNET)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Commands the serial bridge to disconnect from the WiFi network.
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool disconnectNetwork()
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Disconnecting WiFi (DISCONNECTNET)... ");
#endif
    size_t sent = sendFramelessCommand(-1, "DISCONNECTNET");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (DISCONNECTNET)\r\n" : "❌ Error (DISCONNECTNET)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Polls the serial bridge to check its WiFi network connection status.
   * @return true if the bridge reports it is connected, false otherwise.
   */
  bool isNetworkConnected()
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Getting net status (NETSTATUS)... ");
#endif
    size_t sent = sendFramelessCommand(-1, "NETSTATUS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (NETSTATUS)\r\n" : "❌ Error (NETSTATUS)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Enables or disables the WiFi auto-reconnect feature on the serial bridge.
   * @param enable Set to true to enable, false to disable.
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool setAutoReconnect(bool enable)
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Setting auto reconnect (AUTO_RECONNECT)... ");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (AUTO_RECONNECT)\r\n" : "❌ Error (AUTO_RECONNECT)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Sets the connection retry limit on the serial bridge.
   * @param limit The number of retry attempts.
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool setRetryLimit(int limit)
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Setting retry limit (RETRYLIMIT)... ");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (RETRYLIMIT)\r\n" : "❌ Error (RETRYLIMIT)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Sets the delay (in milliseconds) between connection retries.
   * @param ms The delay in milliseconds.
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool setRetryDelay(unsigned long ms)
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Setting retry delay (RETRYDELAY)... ");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (RETRYDELAY)\r\n" : "❌ Error (RETRYDELAY)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Sets the debug verbosity level on the serial bridge device.
   * @param level The debug level (e.g., 0=None, 1=info, 2=verbose).
   * @return true if the command was acknowledged successfully, false otherwise.
   */
  bool setDebugLevel(int level)
  {
    debug_level = level;
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Setting debug level (DEBUGLEVEL)... ");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (DEBUGLEVEL)\r\n" : "❌ Error (DEBUGLEVEL)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Connects to a remote host (TCP).
   * @param host The hostname or IP address of the server.
   * @param port The port number of the server.
   * @return 1 if the connection succeeds, 0 otherwise.
   */
  int connect(const char *host, uint16_t port) override
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Connecting to server (CONNECT)... ");
#endif

    // Build Raw Command Arguments
    char raw_args[MAX_CMD_ARGS_LEN];
    int args_len = snprintf(raw_args, MAX_CMD_ARGS_LEN, "%s %u", host, port);

    if (args_len <= 0 || args_len >= MAX_CMD_ARGS_LEN)
    {
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 0, "❌ Arg is too long (CONNECT)\r\n");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 0, "❌ Frame construct (CONNECT)\r\n");
#endif
      return 0;
    }

    // Send the Full Command Packet
    sink.print(slot);
    sink.print(F(" CONNECT "));
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    // Wait for the framed ACK/FAIL Response
    bool ret = waitForResponse("TRUE", 30 * 1000);
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (CONNECT)\r\n" : "❌ Error (CONNECT)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Connects to a remote host (TCP) using an IPAddress.
   * @param ip The IPAddress object of the server.
   * @param port The port number of the server.
   * @return 1 if the connection succeeds, 0 otherwise.
   */
  int connect(IPAddress ip, uint16_t port) override
  {
    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return connect(ipStr, port);
  }

  /**
   * @brief Connects to a remote host with an option for SSL/TLS.
   * @param host The hostname or IP address of the server.
   * @param port The port number of the server.
   * @param use_ssl Set to true to establish a secure (SSL/TLS) connection.
   * @return true if the connection succeeds, false otherwise.
   */
  bool connect(const char *host, uint16_t port, bool use_ssl)
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Connecting to server (CONNECT)... ");
#endif

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
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (CONNECT)\r\n" : "❌ Error (CONNECT)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Initiates a secure (TLS) handshake on an existing connection.
   * @param tag A tag (often unused, but part of the protocol) for the command.
   * @return true if the TLS handshake was acknowledged, false otherwise.
   */
  bool startTLS(const char *tag)
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Starting TLS (STARTTLS)... ");
#endif

    size_t sent = sendFramelessCommand(slot, "STARTTLS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (STARTTLS)\r\n" : "❌ Error (STARTTLS)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Writes a single byte to the TCP stream.
   * @param b The byte to write.
   * @return 1 if the byte was written successfully, 0 otherwise.
   */
  size_t write(uint8_t b) override
  {
    return write(&b, 1);
  }

  /**
   * @brief Writes a buffer of data to the TCP stream.
   * @param buf Pointer to the data buffer.
   * @param size The number of bytes to write from the buffer.
   * @return The number of bytes successfully written (0 on failure).
   */
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

#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 1, "Writing server request (WRITE)...");
#endif

    // Transmit the command prefix: "WRITE "
    sink.print(slot);
    sink.print(F(" WRITE "));

    // Transmit the framed data string: <...>
    sink.write((const uint8_t *)tx_frame_buffer, frame_len);

    // Wait for the framed ACK/FAIL Response
    int ret = waitForResponse("TRUE") ? size : 0;
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 1, ret > 0 ? "✅ Success (WRITE)\r\n" : "❌ Error (WRITE)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Reads a single byte from the receive buffer.
   * @return The byte read, or -1 if no data is available.
   */
  int read() override
  {
    uint8_t b;
    int n = read(&b, 1);
    if (n == 1)
      return b;
    return -1;
  }

  /**
   * @brief Reads data from the receive buffer into a provided buffer.
   * @param buf Pointer to the destination buffer.
   * @param size The maximum number of bytes to read.
   * @return The number of bytes actually read (0 if none).
   */
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

  /**
   * @brief Gets the number of bytes available in the receive buffer.
   * If the buffer is empty, this function polls the bridge for new data.
   * @return The number of bytes available to read.
   */
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

  /**
   * @brief Disconnects the current TCP connection for this slot.
   */
  void stop() override
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Stopping connection... (STOP)");
#endif

    sendFramelessCommand(slot, "STOP");
    bool ret = waitForResponse("TRUE");

#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Success (STOP)\r\n" : "❌ Error (STOP)\r\n");
#endif
  }

  /**
   * @brief Checks the status of the TCP connection.
   * @return 1 if connected, 0 otherwise.
   */
  uint8_t connected() override
  {
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, "Checking server status (SERVERSTATUS)... ");
#endif

    size_t sent = sendFramelessCommand(slot, "SERVERSTATUS");
    bool ret = sent > 0 ? waitForResponse("TRUE") : false;

#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 0, ret ? "✅ Connected (SERVERSTATUS)\r\n" : "❌ Disconnected (SERVERSTATUS)\r\n");
#endif
    return ret;
  }

  /**
   * @brief Boolean operator to check connection status.
   *  @return true if connected, false otherwise.
   */
  operator bool() override
  {
    return connected() != 0;
  }

  /**
   *  @brief Clears the local receive buffer.
   */
  void flush() override
  {
    // Clear the buffer
    data_available = 0;
    read_pos = 0;
  }

  /**
   * @brief Peeks at the next byte in the receive buffer without consuming it.
   * @return The next byte, or -1 if no data is available.
   */
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

  /**
   * @brief Gets the available space for a single write operation.
   * This is limited by the underlying serial frame size.
   * @return The number of bytes that can be written in one packet.
   */
  int availableForWrite() override
  {
    return MAX_FRAME_SIZE - 45;
  }

private:
  HardwareSerial &sink;
  int slot = -1;
  uint8_t cdata[MAX_READ_BUFFER_SIZE];
  size_t data_available = 0;
  size_t read_pos = 0;
  bool server_status = false;
  int debug_level = 0;

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

  int readResponse(uint8_t *buffer, int buffSize, unsigned long timeout = 1000)
  {

    // Buffer to hold the incoming ENCODED data frame (e.g., <AABBCCDD...>).
    static char encoded_frame_buffer[MAX_ENCODED_FRAME_SIZE];
    size_t idx = 0;

    // Send Framed Command: READRESP <ENCODED(timeout + CRC)>
    if (sinkAvailable() == 0)
    {
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 1, "Reading server response (READRESP)...");
#endif

      // Build Raw Command Arguments (The timeout value)
      char raw_timeout[MAX_CMD_ARGS_LEN];
      int args_len = snprintf(raw_timeout, MAX_CMD_ARGS_LEN, "%lu", timeout);

      if (args_len <= 0)
      {
#if defined(ENABLE_DEBUG_OUTPUT)
        DEBUG_STATUS(debug_level > 1, "❌ Arg is too long (READRESP)\r\n");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
        DEBUG_STATUS(debug_level > 1, "❌ Frame construct (READRESP)\r\n");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
            DEBUG_STATUS(debug_level > 1, "❌ Buffer overflow (READRESP)\r\n");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 1, "❌ Time out (READRESP)\r\n");
#endif
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
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 1, "❌ Unable to read response (READRESP)\r\n");
#endif
      return -1;
    }

    // Check for END signal
    if (strcmp(status.result, "END_DATA") == 0)
    {
      startTime = millis();
      free(decoded_payload);
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 1, "✅ Success (READRESP)\r\n");
#endif
      return server_status ? 0 : -1; // All data streamed
    }

    // Copy Validated Data to Caller's Buffer
    if (data_len >= (size_t)buffSize)
    {
      free(decoded_payload);
#if defined(ENABLE_DEBUG_OUTPUT)
      DEBUG_STATUS(debug_level > 1, "❌ Buffer is too small (READRESP)\r\n");
#endif
      return -1;
    }

    memcpy(buffer, decoded_payload + status.status_length, data_len);
    buffer[data_len] = '\0';
#if defined(ENABLE_DEBUG_OUTPUT)
    DEBUG_STATUS(debug_level > 1, "✅ Success (READRESP)\r\n");
#endif

    // Cleanup and Return
    free(decoded_payload);
    return (int)data_len;
  }
};

#endif