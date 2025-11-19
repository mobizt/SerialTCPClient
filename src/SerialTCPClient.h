#pragma once

#include <Arduino.h>
#include <Stream.h>
#include <Client.h>
#include "SerialTCPProtocol.h"

// Bring in the protocol namespace
using namespace SerialTCPProtocol;

// Buffers
#define SERIAL_TCP_TX_BUFFER_SIZE 256
// (RX buffer size is defined in SerialTCPProtocol.h)

class SerialTCPClient : public Client
{
    friend class SerialHostManager;

private:
    Stream *sink = nullptr;
    int slot = 0;
    volatile bool _connected_status = false;
    volatile bool _is_ssl = false;

    uint8_t _debug_level = 1;

    // TX Buffer (for data to host)
    uint8_t _tx_buffer[SERIAL_TCP_TX_BUFFER_SIZE];
    size_t _tx_buffer_len = 0;
    uint32_t _last_tx_activity_ms = 0;

    // RX Buffer (for data from host)
    uint8_t _rx_buffer[SERIAL_TCP_RX_BUFFER_SIZE];
    volatile size_t _rx_head = 0;
    volatile size_t _rx_tail = 0;

    // Packet Handling
    PacketReceiver _receiver;
    uint8_t _decoded_buffer[MAX_PACKET_BUFFER_SIZE];
    uint8_t _last_packet_cmd = 0;
    uint8_t _last_packet_slot = 0;
    volatile bool _ack_received = false;
    volatile bool _nak_received = false;
    volatile bool _ping_response_received = false;
    volatile bool _poll_response_received = false;

    uint16_t _host_session_id = 0;

    // Private: Packet Processing

    /**
     * @brief Processes one incoming packet.
     */
    void processPacket(const uint8_t *pkt, size_t len)
    {
        uint8_t cmd = pkt[0];
        uint8_t slot = pkt[1];

        _last_packet_cmd = cmd;
        _last_packet_slot = slot;

        // Global responses
        if (slot == GLOBAL_SLOT_ID)
        {
            if (cmd == CMD_H_ACK)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Client]", "Global ACK received");
#endif
                _ack_received = true;
            }
            else if (cmd == CMD_H_NAK)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Client]", "Global NAK received");
#endif
                _nak_received = true;
            }
            else if (cmd == CMD_H_PING_RESPONSE)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Client]", "Global PING_RESPONSE received");
#endif
                _ping_response_received = true;
            }
            else if (cmd == CMD_H_HOST_RESET)
            {
                if (len >= 4)
                {
                    uint16_t new_id = (uint16_t)(pkt[2] << 8) | pkt[3];
#if defined(ENABLE_SERIALTCP_DEBUG)
                    DEBUG_PRINT(_debug_level, "[Client]", "Received HOST RESET! New Session ID: " + String(new_id, HEX));
#endif

                    // If ID changed, force disconnect
                    if (_host_session_id != new_id || _connected_status)
                    {
                        _connected_status = false;
                        _is_ssl = false;
                        _host_session_id = new_id;
                        _tx_buffer_len = 0;
                        _rx_head = 0;
                        _rx_tail = 0;
                    }
                }
            }
            return;
        }

        // Slot-specific responses
        if (slot != this->slot)
        {
            return; // Not for us
        }

        switch (cmd)
        {
        case CMD_H_ACK:
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Control ACK received");
#endif
            _ack_received = true;
            break;
        case CMD_H_NAK:
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Control NAK received");
#endif
            _nak_received = true;
            break;

        case CMD_H_CONNECTED_STATUS:
            _connected_status = (bool)pkt[2];
            if (!_connected_status)
                _is_ssl = false; // Reset SSL on disconnect
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Got Host-Push Status: " + String(_connected_status));
#endif
            _ack_received = true; // For 'is_connected' command
            break;

        case CMD_H_POLL_RESPONSE:
            if (len < 5)
                break;
            _connected_status = (bool)pkt[2];
            uint16_t data_len = (uint16_t)(pkt[3] << 8) | pkt[4];

            if (data_len > 0)
            {
                const uint8_t *payload = &pkt[5];
                for (size_t i = 0; i < data_len; i++)
                {
                    size_t next_head = (_rx_head + 1) % SERIAL_TCP_RX_BUFFER_SIZE;
                    if (next_head != _rx_tail)
                    {
                        _rx_buffer[_rx_head] = payload[i];
                        _rx_head = next_head;
                    }
                }
            }
            _poll_response_received = true;
            break;

        case CMD_H_DATA_PAYLOAD:
        {
            size_t data_len = len - 4; // cmd, slot, crc_lo, crc_hi
            const uint8_t *payload = &pkt[2];
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Got Host-Push Data: " + String(data_len) + " bytes");
#endif

            for (size_t i = 0; i < data_len; i++)
            {
                size_t next_head = (_rx_head + 1) % SERIAL_TCP_RX_BUFFER_SIZE;
                if (next_head != _rx_tail)
                {
                    _rx_buffer[_rx_head] = payload[i];
                    _rx_head = next_head;
                }
                else
                {
#if defined(ENABLE_SERIALTCP_DEBUG)
                    DEBUG_PRINT(_debug_level, "[Client]", "ERROR: RX Buffer Overflow!");
#endif
                }
            }
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Sending Data ACK");
#endif
            sendPacket(*sink, CMD_C_DATA_ACK, this->slot, nullptr, 0);

            break;
        }

        case CMD_H_NET_STATUS:
            // TODO: Handle global status
            break;
        }
    }

    bool awaitAckNak(uint32_t timeout)
    {
        uint32_t start = millis();
        _ack_received = false;
        _nak_received = false;
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Waiting for ACK (" + String(timeout) + "ms)...");
#endif

        while (millis() - start < timeout)
        {
            maintenance();
            if (_ack_received)
                return true;
            if (_nak_received)
                return false;
            delay(1);
        }
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "ERROR: ACK Timeout!");
#endif
        return false;
    }

    bool awaitPollResponse(uint32_t timeout)
    {
        uint32_t start = millis();
        _poll_response_received = false;
        while (millis() - start < timeout)
        {
            maintenance();
            if (_poll_response_received)
                return true;
            delay(1);
        }
        return false;
    }

    bool awaitPingResponse(uint32_t timeout)
    {
        uint32_t start = millis();
        _ping_response_received = false;
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Waiting for PING_RESPONSE (" + String(timeout) + "ms)...");
#endif

        while (millis() - start < timeout)
        {
            maintenance();
            if (_ping_response_received)
                return true;
            delay(1);
        }
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "ERROR: PING Timeout!");
#endif
        return false;
    }

    bool sendCommand(uint8_t cmd, uint8_t slot, const uint8_t *payload, size_t len, bool wait_for_ack, uint32_t timeout = DEFAULT_CMD_TIMEOUT)
    {
        if (!sink)
            return false;

        _ack_received = false;
        _nak_received = false;
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Sending command: " + String(cmd, HEX));
#endif
        if (sendPacket(*sink, cmd, slot, payload, len) == 0)
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "ERROR: sendPacket failed!");
#endif
            return false;
        }

        if (wait_for_ack)
        {
            if (cmd == CMD_C_PING_HOST)
            {
                return awaitPingResponse(timeout);
            }
            else
            {
                return awaitAckNak(timeout);
            }
        }
        return true;
    }

    bool flushTxBuffer()
    {
        if (_tx_buffer_len == 0)
        {
            return true;
        }
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Flushing TX buffer (" + String(_tx_buffer_len) + " bytes)");
#endif

        uint8_t temp_buf[SERIAL_TCP_TX_BUFFER_SIZE];
        memcpy(temp_buf, _tx_buffer, _tx_buffer_len);
        size_t temp_len = _tx_buffer_len;

        _tx_buffer_len = 0;
        _last_tx_activity_ms = 0;

        bool success = sendCommand(CMD_C_WRITE, this->slot, temp_buf, temp_len, true, DEFAULT_CMD_TIMEOUT);

        if (!success)
        {
            _connected_status = false;
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "ERROR: flushTxBuffer failed!");
#endif
        }

        return success;
    }

    size_t _get_buffered_count()
    {
        if (_rx_head == _rx_tail)
            return 0;
        if (_rx_head > _rx_tail)
        {
            return _rx_head - _rx_tail;
        }
        else
        {
            return SERIAL_TCP_RX_BUFFER_SIZE - (_rx_tail - _rx_head);
        }
    }

    void pollHost()
    {
        if (!sink)
            return;
        uint8_t payload[2];
        payload[0] = (uint8_t)(_host_session_id >> 8);
        payload[1] = (uint8_t)(_host_session_id & 0xFF);

        // Send CMD_C_POLL_DATA. Host checks ID, sends RESET if mismatch.
        // We wait for POLL_RESPONSE or RESET.
        if (sendPacket(*sink, CMD_C_POLL_DATA, slot, payload, 2) == 0)
        {
            return;
        }
        awaitPollResponse(DEFAULT_CMD_TIMEOUT);
    }

    bool setWiFiImpl(const char *ssid, const char *password)
    {
        size_t ssid_len = strlen(ssid);
        size_t pass_len = strlen(password);
        if (ssid_len > 100 || pass_len > 100)
            return false;

        size_t payload_len = 2 + ssid_len + pass_len;
        uint8_t payload[payload_len];
        payload[0] = (uint8_t)ssid_len;
        memcpy(&payload[1], ssid, ssid_len);
        payload[1 + ssid_len] = (uint8_t)pass_len;
        memcpy(&payload[2 + ssid_len], password, pass_len);

        return sendCommand(CMD_C_SET_WIFI, GLOBAL_SLOT_ID, payload, payload_len, true);
    }

    bool connectNetworkImpl()
    {
        return sendCommand(CMD_C_CONNECT_NET, GLOBAL_SLOT_ID, nullptr, 0, true, SERIAL_TCP_CONNECT_TIMEOUT);
    }

    bool disconnectNetworkImpl()
    {
        return sendCommand(CMD_C_DISCONNECT_NET, GLOBAL_SLOT_ID, nullptr, 0, true);
    }

    bool isNetworkConnectedImpl()
    {
        return sendCommand(CMD_C_IS_NET_CONNECTED, GLOBAL_SLOT_ID, nullptr, 0, true);
    }

    bool pingHostImpl(uint32_t timeout = 1000)
    {
        return sendCommand(CMD_C_PING_HOST, GLOBAL_SLOT_ID, nullptr, 0, true, timeout);
    }

    bool rebootHostImpl()
    {
        return sendCommand(CMD_C_REBOOT_HOST, GLOBAL_SLOT_ID, nullptr, 0, true);
    }

    bool setDebugLevelImpl(int level)
    {
        _debug_level = (uint8_t)level;
        uint8_t payload = (uint8_t)level;
        return sendCommand(CMD_C_SET_DEBUG, GLOBAL_SLOT_ID, &payload, 1, true);
    }

    void setLocalDebugLevelImpl(int level)
    {
        _debug_level = (uint8_t)level;
    }

    void setSerial(HardwareSerial &sink) { this->sink = &sink; }

    void maintenance()
    {
        if (!sink)
            return;

        // Process all incoming data
        while (sink->available())
        {
            delay(0); // WDT fix

            uint8_t b = sink->read();
            size_t cobsLen = _receiver.read_byte(b);
            if (cobsLen > 0)
            {
                size_t decodedLen = cobs_decode(_receiver.buffer, cobsLen, _decoded_buffer);

                if (decodedLen > 2)
                {
                    uint16_t rcvd_crc = (uint16_t)(_decoded_buffer[decodedLen - 1] << 8) | _decoded_buffer[decodedLen - 2];
                    uint16_t calc_crc = calculate_crc16(_decoded_buffer, decodedLen - 2);

                    if (rcvd_crc == calc_crc)
                    {
                        processPacket(_decoded_buffer, decodedLen);
                    }
                    else
                    {
#if defined(ENABLE_SERIALTCP_DEBUG)
                        DEBUG_PRINT(_debug_level, "[Client]", "ERROR: Bad CRC!");
#endif
                    }
                }
            }
        }

        // Check for TX auto-flush (for MQTT)
        if (_tx_buffer_len > 0 && _last_tx_activity_ms > 0 &&
            (millis() - _last_tx_activity_ms > AUTO_FLUSH_TIMEOUT_MS))
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "Auto-flushing TX buffer...");
#endif
            flushTxBuffer();
        }
    }

public:
    SerialTCPClient() {}

    /**
     * @brief Constructor for SerialTCPClient.
     * @param sink The HardwareSerial interface (e.g., Serial1) used for communication.
     * @param slot The specific client slot ID to use on the serial bridge.
     */
    SerialTCPClient(HardwareSerial &sink, int slot)
        : sink(&sink), slot(slot) {}

    /**
     * @brief Constructor for SerialTCPClient (unslotted).
     * @param sink The HardwareSerial interface (e.g., Serial1) used for communication.
     */
    SerialTCPClient(HardwareSerial &sink)
        : sink(&sink), slot(0) {}

    /**
     * @brief (Re)initializes the client with a specific HardwareSerial interface.
     * @param serial The HardwareSerial interface (e.g., Serial1) to bind to.
     */
    void begin(HardwareSerial &serial)
    {
        this->sink = &serial;
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
     * @brief Pings the host to check if it's alive.
     */
    bool pingHost(uint32_t timeout = 1000)
    {
        return sendCommand(CMD_C_PING_HOST, GLOBAL_SLOT_ID, nullptr, 0, true, timeout);
    }

    /**
     * @brief Sets the debug level only on the client.
     */
    void setLocalDebugLevel(int level)
    {
        _debug_level = (uint8_t)level;
    }

    /**
     * @brief Connects to a remote host (TCP).
     * @param host The hostname or IP address of the server.
     * @param port The port number of the server.
     * @return 1 if the connection succeeds, 0 otherwise.
     */
    int connect(const char *host, uint16_t port) override
    {
        return connect(host, port, false);
    }

    /**
     * @brief Connects to a remote host (TCP) using an IPAddress.
     * @param ip The IPAddress object of the server.
     * @param port The port number of the server.
     * @return 1 if the connection succeeds, 0 otherwise.
     */
    int connect(IPAddress ip, uint16_t port) override
    {
        return connect(ip.toString().c_str(), port, false);
    }

    /**
     * @brief Connects to a remote host with an option for SSL/TLS.
     * @param host The hostname or IP address of the server.
     * @param port The port number of the server.
     * @param use_ssl Set to true to establish a secure (SSL/TLS) connection.
     * @return true if the connection succeeds, false otherwise.
     */
    int connect(const char *host, uint16_t port, bool use_ssl)
    {
        size_t host_len = strlen(host);
        if (host_len > 200)
            return 0;

        // Payload: [use_ssl (1 byte)] [port (2 bytes)] [host_len (1 byte)] [host_str]
        size_t payload_len = 4 + host_len;
        uint8_t payload[payload_len];
        payload[0] = (uint8_t)use_ssl;
        payload[1] = (uint8_t)(port >> 8);
        payload[2] = (uint8_t)(port & 0xFF);
        payload[3] = (uint8_t)host_len;
        memcpy(&payload[4], host, host_len);

        while (sink->available())
            sink->read();
        _rx_tail = 0;
        _rx_head = 0;

        if (sendCommand(CMD_C_CONNECT_HOST, this->slot, payload, payload_len, true, SERIAL_TCP_CONNECT_TIMEOUT))
        {
            _connected_status = true;
            _is_ssl = use_ssl;
            return 1;
        }
        else
        {
            _connected_status = false;
            _is_ssl = false;
            return 0;
        }
    }

    /**
     * @brief Connects to a remote host with an option for SSL/TLS.
     * @param ip The IPAddress object of the server.
     * @param port The port number of the server.
     * @param use_ssl Set to true to establish a secure (SSL/TLS) connection.
     * @return true if the connection succeeds, false otherwise.
     */
    int connect(IPAddress ip, uint16_t port, bool use_ssl)
    {
        return connect(ip.toString().c_str(), port, use_ssl);
    }

    /**
     * @brief Set a sticky CA cert filename on the host for this slot.
     * @param filename The filename of the CA certificate on the host filesystem.
     */
    bool setCACert(const char *filename)
    {
        size_t len = strlen(filename);
        if (len == 0 || len > 200)
            return false;
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Sending SET_CA_CERT command...");
#endif
        return sendCommand(CMD_C_SET_CA_CERT, this->slot, (const uint8_t *)filename, len, true);
    }

    /**
     * @brief Upgrades an existing connection to SSL/TLS (STARTTLS).
     */
    bool startTLS()
    {
        if (!_connected_status)
            return false;
        if (_is_ssl)
            return true;
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Client]", "Sending STARTTLS command...");
#endif
        if (sendCommand(CMD_C_START_TLS, this->slot, nullptr, 0, true, SERIAL_TCP_CONNECT_TIMEOUT))
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "STARTTLS successful.");
#endif
            _is_ssl = true;
            return true;
        }
        else
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "ERROR: STARTTLS failed (NAK or Timeout).");
#endif
            return false;
        }
    }

    /**
     * @brief Checks if the current connection is using SSL/TLS.
     * @return true if the connection is secure (SSL/TLS), false otherwise.
     */
    bool isSSL()
    {
        return _is_ssl;
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
        if (!_connected_status)
            return 0;

        size_t written = 0;
        for (size_t i = 0; i < size; i++)
        {
            if (_tx_buffer_len >= SERIAL_TCP_TX_BUFFER_SIZE)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Client]", "TX buffer full, flushing...");
#endif
                if (!flushTxBuffer())
                {
                    _connected_status = false;
                    return written;
                }
            }
            _tx_buffer[_tx_buffer_len++] = buf[i];
            written++;
        }

        if (written > 0)
        {
            _last_tx_activity_ms = millis();
        }

        return written;
    }

    /**
     * @brief Gets the number of bytes available for writing in the TX buffer.
     */
    int availableForWrite()
    {
        if (_tx_buffer_len >= SERIAL_TCP_TX_BUFFER_SIZE)
            return 0;
        return SERIAL_TCP_TX_BUFFER_SIZE - _tx_buffer_len;
    }

    /**
     * @brief Gets the number of bytes available.
     * This calls maintenance() to prime the buffer.
     */
    int available() override
    {
        maintenance();

        size_t count = _get_buffered_count();
        if (count > 0)
        {
            return count;
        }

        if (!_connected_status)
        {
            return 0;
        }

        // Second check (smart sync for edge cases)
        maintenance();
        count = _get_buffered_count();
        if (count > 0)
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Client]", "...data arrived on second check.");
#endif
            return count;
        }

        return 0;
    }

    /**
     * @brief Reads a byte from the receive buffer.
     * Returns -1 if the buffer is empty. NON-BLOCKING.
     */
    int read() override
    {
        if (_rx_head == _rx_tail)
        {
            return -1;
        }

        uint8_t b = _rx_buffer[_rx_tail];
        _rx_tail = (_rx_tail + 1) % SERIAL_TCP_RX_BUFFER_SIZE;
        return b;
    }

    /**
     * @brief Reads data from the receive buffer into a provided buffer.
     * @param buf Pointer to the destination buffer.
     * @param size The maximum number of bytes to read.
     * @return The number of bytes actually read (0 if none).
     */
    int read(uint8_t *buf, size_t size) override
    {
        size_t i = 0;
        for (i = 0; i < size; i++)
        {
            // Call our "smart" available() to prime the pump
            if (available() == 0)
            {
                break;
            }

            int b = read();
            if (b == -1)
            {
                break;
            }
            buf[i] = (uint8_t)b;
        }
        return i;
    }

    /**
     * @brief Peeks at the next byte in the receive buffer without consuming it.
     * @return The next byte, or -1 if no data is available.
     */
    int peek() override
    {
        if (_rx_head == _rx_tail)
        {
            return -1;
        }
        return _rx_buffer[_rx_tail];
    }

    /**
     * @brief Clears the local receive buffer.
     */
    void flush() override
    {
        if (!_connected_status)
            return;
        if (!flushTxBuffer())
        {
            _connected_status = false;
        }
    }

    /**
     * @brief Stops the TCP connection.
     */
    void stop() override
    {
        sendCommand(CMD_C_STOP, this->slot, nullptr, 0, false);
        _connected_status = false;
        _is_ssl = false;
        _tx_buffer_len = 0;
        _rx_tail = 0;
        _rx_head = 0;

        maintenance();
    }

    /**
     * @brief Checks if the TCP connection is active.
     * @return 1 if connected, 0 otherwise.
     */
    uint8_t connected() override
    {
        maintenance();
        return _connected_status;
    }

    /**
     * @brief Boolean operator to check connection status.
     */
    operator bool() override
    {
        return connected();
    }
};