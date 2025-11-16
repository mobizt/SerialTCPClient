
#ifndef SERIAL_TCP_HOST_H
#define SERIAL_TCP_HOST_H

#ifndef MAX_TCP_CLIENTS
#define MAX_TCP_CLIENTS 2
#endif
#define MAX_SPLIT_TOKENS 10
#define MAX_SPLIT_TOKEN_LEN 50
#define MAX_CMD_BUF 512
#define MAX_KEYWORD_LEN 16
#define DATA_CHUNK_SIZE 128 // Limit raw data sent per frame

#include <Arduino.h>
#include "SerialTCPHelper.h"
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

namespace SerialTCPHost_NS
{
  typedef void (*StartTLSCallback)(bool &success);

  struct serial_tcp_client_context
  {
    Client *client = nullptr;
    StartTLSCallback tlsCb = NULL;
    char host[64];
    uint16_t port = 0;
    bool tlsStarted = false;
    bool sslMode = false;
    bool server_status = false;
    unsigned long ms = 0;
  };
}

using namespace SerialTCPHost_NS;
using namespace SerialTCPHelper_NS;

class SerialTCPHost
{
public:
  SerialTCPHost(Stream &sink)
      : sink(sink)
  {
    for (size_t i = 0; i < MAX_TCP_CLIENTS; i++)
    {
      clients[i].client = nullptr;
      clients[i].tlsCb = NULL;
    }
  }

  void setClient(Client *client, int slot, StartTLSCallback tlsCb = NULL)
  {
    if (slot < MAX_TCP_CLIENTS && slot > -1)
    {
      clients[slot].client = client;
      clients[slot].tlsCb = tlsCb;
    }
  }

  void loop()
  {
    handleSerialCommands();
    checkWiFiConnection();
  }

private:
  Stream &sink;
  char ssid[32];
  char pass[32];
  serial_tcp_client_context clients[MAX_TCP_CLIENTS];

  size_t cmdLen = 0;
  bool autoReconnect = true;
  int retryLimit = 5;
  unsigned long retryDelay = 5000;
  int debugLevel = 0;
  int total_read = 0;       // State variable for tracking total bytes read
  bool reading_cmd = false; // Member variable for command state
  bool is_busy = false;
  StartTLSCallback tlsCB = NULL;

  serial_bridge_status_context status;

  char cmdBuf[MAX_CMD_BUF];
  char status_resp[70];

  unsigned long lastWiFiCheck = 0;
  int wifiRetryCount = 0;

  bool processResponse(serial_tcp_client_context *ctx, unsigned long timeout = 1000)
  {
    SerialTCPHelper::yield();
    // We trust the available in case data has been read.
    if (total_read > 0 && ctx->client->available() == 0)
    {
      DEBUG_STATUS("✅ Success (READRESP END_DATA)\r\n");
      sendFramedResponse(ctx, "END_DATA", "READRESP");
      return false;
    }

    // Wait for data with timeout
    unsigned long ms = millis();
    while (millis() - ms < timeout && ctx->client->available() == 0)
    {
      SerialTCPHelper::yield();
    }

    if (ctx->client->available() == 0)
    {
      DEBUG_STATUS("❌ No data available (READRESP)\r\n");
      sendFramedResponse(ctx, "FALSE", "READRESP");
      return false;
    }

    uint8_t buf[DATA_CHUNK_SIZE];
    size_t idx = 0;
    int _c = 0;
    ms = millis();
    while (millis() - ms < timeout)
    {

      SerialTCPHelper::yield();
      int c = ctx->client->read(); // Read byte-by-byte from TCP stream

      if (c != -1)
      {
        // Check for buffer overflow on line buffer
        if (idx >= DATA_CHUNK_SIZE - 1)
        {
          buf[idx] = 0;
          sendFramedResponse(ctx, "TRUE", "READRESP", buf, idx);
          total_read += idx;
          idx = 0;
          DEBUG_STATUS("✅ Success (READRESP)\r\n");
          return true;
        }

        buf[idx++] = (uint8_t)c;
        ms = millis();

        // Check for end of line (CRLF or LF)
        if ((_c == '\r' && c == '\n') || c == '\n')
        {
          buf[idx] = '\0';
          sendFramedResponse(ctx, "TRUE", "READRESP", buf, idx);
          total_read += idx;
          idx = 0;
          DEBUG_STATUS("✅ Success (READRESP)\r\n");
          return true;
        }
        _c = c;
      }
    }
    DEBUG_STATUS("✅ Success (READRESP)\r\n");
    return total_read > 0;
  }

  size_t get_keyword_end_index(const char *cmd)
  {
    const char *space_ptr = strchr(cmd, ' ');
    if (space_ptr)
    {
      return (size_t)(space_ptr - cmd);
    }
    return strlen(cmd);
  }

  void handleSerialCommands()
  {
    bool reading_cmd = this->reading_cmd;

    while (sink.available())
    {
      SerialTCPHelper::yield();
      char c = sink.read();

      if (!reading_cmd)
      {
        // Start Check: If character is printable (keyword start)
        if (c >= 32 && c != STOP_DELIMITER)
        {
          reading_cmd = true;
          cmdLen = 0;
          // FALL THROUGH to the accumulation logic to save this first character
        }
        else
        {
          // Discard noise characters
          continue;
        }
      }

      //  Accumulation Logic
      if (reading_cmd)
      {
        if (cmdLen < sizeof(cmdBuf) - 1)
        {
          cmdBuf[cmdLen++] = c;
        }
        else
        {
          cmdLen = 0;
          reading_cmd = false;
          continue;
        }

        if (c == STOP_DELIMITER)
        {
          cmdBuf[cmdLen] = 0;
          cmdLen = 0;
          reading_cmd = false;
          processCommand(cmdBuf);
        }
      }
    }
    this->reading_cmd = reading_cmd; // Update member variable state
  }

  void processCommand(const char *cmd)
  {

    SerialTCPHelper::yield();

    int slot = cmd[0] >= '0' && cmd[0] <= '9' ? cmd[0] - '0' : -1;
    serial_tcp_client_context *ctx = slot > -1 ? &clients[slot] : nullptr;
    if (slot > -1)
      cmd += 2;

    // Find command keyword length and argument frame pointer
    size_t cmd_keyword_len = get_keyword_end_index(cmd);
    const char *space_ptr = cmd + cmd_keyword_len;
    const char *arg_frame = (*space_ptr == ' ') ? space_ptr + 1 : NULL;

    // Decode and Validate argument frame (if present)
    uint8_t *decoded_args = NULL;
    size_t decoded_arg_len = 0;

    if (arg_frame && *arg_frame == START_DELIMITER)
    {
      decoded_args = SerialTCPHelper::deconstruct_and_validate_frame_only(arg_frame, &decoded_arg_len);
    }

    // Temporary buffer for keyword matching
    char keyword_buf[MAX_KEYWORD_LEN];
    strncpy(keyword_buf, cmd, cmd_keyword_len);
    keyword_buf[cmd_keyword_len] = '\0';

    // Argument validation check utility
    auto validate_args = [&](bool requires_args) __attribute__((always_inline)) -> bool
    {
      bool frame_was_empty = (decoded_args && decoded_arg_len == 0);

      if (requires_args && (!decoded_args || frame_was_empty))
        return false;

      if (!requires_args && (!decoded_args || !frame_was_empty))
        return false;

      return true;
    };

    //  Command Handling Blocks (Uses strcmp for robust keyword match)
    if (strcmp(keyword_buf, "CONNECTNET") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      DEBUG_STATUS("Connecting to network (CONNECTNET)... ");

      if (is_busy)
      {
        sendFramedResponse(nullptr, "FALSE", "CONNECTNET");
        DEBUG_STATUS("❌ Host is busy (CONNECTNET)\r\n");
        return;
      }

      if (WiFi.status() != WL_CONNECTED)
      {

        WiFi.begin(ssid, pass);
        wifiRetryCount = 0;
        unsigned long start = millis();
        is_busy = true;
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
        {
          SerialTCPHelper::yield();
        }
        is_busy = false;
      }

      status.net_status = WiFi.status() == WL_CONNECTED;
      sendFramedResponse(nullptr, status.net_status ? "TRUE" : "FALSE", "CONNECTNET");

      DEBUG_STATUS(status.net_status ? "✅ Connected (CONNECTNET)\r\n" : "❌ Disconnected (CONNECTNET)\r\n");
    }
    else if (strcmp(keyword_buf, "DISCONNECTNET") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      DEBUG_STATUS("Disconnecting WiFi (DISCONNECTNET)... ");

      WiFi.disconnect();
      status.net_status = false;
      sendFramedResponse(nullptr, "TRUE", "DISCONNECTNET");

      DEBUG_STATUS("✅ Success (DISCONNECTNET)\r\n");
    }
    else if (strcmp(keyword_buf, "NETSTATUS") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      DEBUG_STATUS("Getting net status (NETSTATUS)... ");

      sendFramedResponse(nullptr, WiFi.status() == WL_CONNECTED ? "TRUE" : "FALSE", "NETSTATUS");

      DEBUG_STATUS(WiFi.status() == WL_CONNECTED ? "✅ Connected (NETSTATUS)\r\n" : "❌ Disconnected (NETSTATUS)\r\n");
    }
    else if (ctx && strcmp(keyword_buf, "STOP") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      ctx->tlsStarted = false;

      DEBUG_STATUS("Stopping connection (STOP)... ");

      if (ctx->client->connected())
        ctx->client->stop();
      sendFramedResponse(ctx, "TRUE", "STOP");

      DEBUG_STATUS("✅ Success (STOP)\r\n");
    }
    else if (ctx && strcmp(keyword_buf, "SERVERSTATUS") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      DEBUG_STATUS("Checking server status (SERVERSTATUS)... ");

      sendFramedResponse(ctx, ctx->client->connected() ? "TRUE" : "FALSE", "SERVERSTATUS");

      DEBUG_STATUS(ctx->client->connected() ? "✅ Connected (SERVERSTATUS)\r\n" : "❌ Disconnected (SERVERSTATUS)\r\n");
    }
    else if (ctx && strcmp(keyword_buf, "STARTTLS") == 0)
    {

      if (!validate_args(false))
        goto fail_arg_check;

      DEBUG_STATUS("Starting TLS (STARTTLS)... ");

      bool tlsReady = false;

      if (!ctx->sslMode && ctx->tlsCb)
        ctx->tlsCb(tlsReady);

      if (!ctx->sslMode && tlsReady)
      {
        ctx->tlsStarted = true;
        DEBUG_STATUS("✅ Success (STARTTLS)\r\n");
        sendFramedResponse(ctx, "TRUE", "STARTTLS");
      }
      else
      {
        DEBUG_STATUS("❌ Error (STARTTLS)\r\n");
        sendFramedResponse(ctx, "FALSE", "STARTTLS");
      }
    }
    // Command: SETWIFI
    else if (strcmp(keyword_buf, "SETWIFI") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Setting WiFi (SETWIFI)... ");

      const char *creds = (const char *)decoded_args;
      const char *sep = strchr(creds, ' ');
      if (sep)
      {

        size_t ssidLen = sep - creds;
        strncpy(ssid, creds, ssidLen < sizeof(ssid) ? ssidLen : sizeof(ssid) - 1);
        ssid[ssidLen < sizeof(ssid) ? ssidLen : sizeof(ssid) - 1] = '\0';
        strncpy(pass, sep + 1, sizeof(pass) - 1);
        pass[sizeof(pass) - 1] = '\0';
        sendFramedResponse(nullptr, "TRUE", "SETWIFI");

        DEBUG_STATUS("✅ Success (SETWIFI)\r\n");
      }
      else
      {
        sendFramedResponse(nullptr, "FALSE", "SETWIFI");
        DEBUG_STATUS("❌ Error (SETWIFI)\r\n");
      }
    }
    // Command: AUTO_RECONNECT
    else if (strcmp(keyword_buf, "AUTO_RECONNECT") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Setting auto reconnect (AUTO_RECONNECT)... ");

      autoReconnect = strcmp((const char *)decoded_args, "ON") == 0;
      sendFramedResponse(nullptr, "TRUE", "AUTO_RECONNECT");

      DEBUG_STATUS("✅ Success (AUTO_RECONNECT)\r\n");
    }
    // Command: RETRYLIMIT
    else if (strcmp(keyword_buf, "RETRYLIMIT") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Setting retry limit (RETRYLIMIT)... ");

      retryLimit = atoi((const char *)decoded_args);
      sendFramedResponse(nullptr, "TRUE", "RETRYLIMIT");

      DEBUG_STATUS("✅ Success (RETRYLIMIT)\r\n");
    }
    // Command: RETRYDELAY
    else if (strcmp(keyword_buf, "RETRYDELAY") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Setting retry delay (RETRYDELAY)... ");

      retryDelay = atol((const char *)decoded_args);
      sendFramedResponse(nullptr, "TRUE", "RETRYDELAY");

      DEBUG_STATUS("✅ Success (RETRYDELAY)\r\n");
    }
    // Command: DEBUGLEVEL
    else if (strcmp(keyword_buf, "DEBUGLEVEL") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Setting debug level (DEBUGLEVEL)... ");

      debugLevel = atoi((const char *)decoded_args);
      sendFramedResponse(nullptr, "TRUE", "DEBUGLEVEL");

      DEBUG_STATUS("✅ Success (DEBUGLEVEL)\r\n");
    }
    // Command: CONNECT
    else if (ctx && strcmp(keyword_buf, "CONNECT") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Connecting to server (CONNECT)... ");

      if (is_busy)
      {
        sendFramedResponse(ctx, "FALSE", "CONNECT");
        DEBUG_STATUS("❌ Host is busy (CONNECT)");
        return;
      }

      if (WiFi.status() == WL_CONNECTED)
      {
        is_busy = true;
        char tokens[MAX_SPLIT_TOKENS][MAX_SPLIT_TOKEN_LEN];
        int token_count = SerialTCPHelper::extract_and_store_tokens((const char *)decoded_args, tokens);

        if (token_count >= 2)
        {
          strncpy(ctx->host, tokens[0], sizeof(ctx->host) - 1);
          ctx->host[sizeof(ctx->host) - 1] = '\0';
          ctx->port = atoi(tokens[1]);
          ctx->sslMode = (token_count == 3 && strcmp(tokens[token_count - 1], "SSL") == 0);

          ctx->client->stop();
          if (ctx->client->connect(ctx->host, ctx->port))
          {
            ctx->tlsStarted = ctx->sslMode;
            sendFramedResponse(ctx, "TRUE", "CONNECT");
            DEBUG_STATUS("✅ Success (CONNECT)\r\n");
          }
          else
          {
            sendFramedResponse(ctx, "FALSE", "CONNECT");
            DEBUG_STATUS("❌ Unable to connect to server (CONNECT)\r\n");
          }
        }
        else
        {
          sendFramedResponse(ctx, "FALSE", "CONNECT");
          DEBUG_STATUS("❌ Error (CONNECT)\r\n");
        }
        is_busy = false;
      }
      else
      {
        sendFramedResponse(ctx, "FALSE", "CONNECT");
        DEBUG_STATUS("❌ Network is not connected (CONNECT)\r\n");
      }
    }
    // Command: WRITE
    else if (ctx && strcmp(keyword_buf, "WRITE") == 0)
    {

      total_read = 0; // Reset total read counter for new transaction
      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Writing server request (WRITE)... ");

      if (!ctx->client->connected())
      {
        sendFramedResponse(ctx, "FALSE", "WRITE");
        DEBUG_STATUS("❌ Error (WRITE)\r\n");
        goto cleanup;
      }

      int sent = ctx->client->write(decoded_args, decoded_arg_len);
      sendFramedResponse(ctx, sent == (int)decoded_arg_len ? "TRUE" : "FALSE", "WRITE");

      DEBUG_STATUS(sent == (int)decoded_arg_len ? "✅ Success (WRITE)\r\n" : "❌ Error (WRITE)\r\n");
    }
    // Command: READRESP
    else if (ctx && strcmp(keyword_buf, "READRESP") == 0)
    {

      if (!decoded_args)
        goto fail_arg_check;

      DEBUG_STATUS("Reading server reasponse (READRESP)... ");

      unsigned long timeout = atol((const char *)decoded_args);

      // processResponse now streams the response data
      processResponse(ctx, timeout);
    }
    else
    {

      // Unknown command
      sendFramedResponse(ctx, "FALSE", "CMD");
      DEBUG_STATUS("❌ Unknown command (CMD)\r\n");
    }

  // Standard cleanup and error handling blocks:
  cleanup:
    if (decoded_args)
      free(decoded_args);
    return;

  fail_arg_check:
    sendFramedResponse(ctx, "FALSE", "CMD");
    DEBUG_STATUS("❌ Arg/CRC mismatch (CMD)\r\n");
    if (decoded_args)
      free(decoded_args);
    // Also purge on failed commands
  }

  void getStatusResponse(serial_tcp_client_context *ctx, const char *result, const char *caller)
  {
    status.net_status = WiFi.status() == WL_CONNECTED;
    bool server_status = false;
    if (ctx)
    {
      ctx->server_status = ctx->client->connected();
      server_status = ctx->server_status;
    }
    snprintf(status_resp, sizeof(status_resp), "%s %s %d %d %s %s", result, caller, status.net_status, server_status, ssid, pass);
  }

  size_t sendFramedResponse(serial_tcp_client_context *ctx, const char *result, const char *caller, const uint8_t *buf = nullptr, size_t size = 0)
  {
    getStatusResponse(ctx, result, caller);
    char num_buf[32];
    int resp_len = strlen(status_resp); // status response length
    snprintf(num_buf, 32, "%d", resp_len);
    int num_len = strlen(num_buf);
    size_t sz = size + resp_len + num_len + 3;

    uint8_t *rawBuf = (uint8_t *)malloc(sz + 1);
    if (!rawBuf)
      return 0;

    int index = 0;
    memcpy(rawBuf, num_buf, num_len);
    index += num_len;
    memcpy(rawBuf + index, " ", 1);
    index++;
    memcpy(rawBuf + index, status_resp, resp_len);
    index += resp_len;
    memcpy(rawBuf + index, " ", 1);
    index++;
    if (buf && size > 0)
      memcpy(rawBuf + index, buf, size);
    index += size;

    rawBuf[index] = 0;

    char tx_frame_buffer[MAX_CMD_BUF];
    size_t frame_len = SerialTCPHelper::construct_and_encode_frame(
        rawBuf,
        sz,
        tx_frame_buffer,
        MAX_CMD_BUF);

    free(rawBuf);
    rawBuf = nullptr;

    if (frame_len == 0)
      return 0;

    size_t written = sink.write((const uint8_t *)tx_frame_buffer, frame_len);
    sink.flush();
    return written;
  }

  void checkWiFiConnection()
  {
    if (!autoReconnect)
      return;

    unsigned long now = millis();
    if (now - lastWiFiCheck < retryDelay)
      return;
    lastWiFiCheck = now;

    if (is_busy)
    {
      sendFramedResponse(nullptr, "FALSE", "RECONNECTNET");
      DEBUG_STATUS("❌ Host is busy (RECONNECTNET)\r\n");
      return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      if (ssid[0] == 0 || pass[0] == 0)
      {
        status.net_status = false;
        return;
      }

      is_busy = true;
      if (wifiRetryCount < retryLimit)
      {
        WiFi.begin(ssid, pass);
        wifiRetryCount++;
        if (debugLevel >= 1)
          sendFramedResponse(nullptr, "FALSE", "RECONNECTNET");
      }
      else
      {
        status.net_status = false;
        if (debugLevel >= 1)
          sendFramedResponse(nullptr, "FALSE", "RECONNECTNET");
      }
      is_busy = false;
    }
    else
    {
      if (!status.net_status)
      {
        status.net_status = true;
        wifiRetryCount = 0;
        if (debugLevel >= 1)
          sendFramedResponse(nullptr, "OK", "RECONNECTNET");
      }
    }
  }
};

#endif