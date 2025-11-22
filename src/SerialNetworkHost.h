/*
 * SPDX-FileCopyrightText: 2025 Suwatchai K. <suwatchai@outlook.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SERIAL_NETWORK_HOST_H
#define SERIAL_NETWORK_HOST_H

#include <Stream.h>
#include <Client.h>
#include <Udp.h>                   // Required for UDP support
#include "SerialNetworkProtocol.h" // Updated protocol file name

using namespace SerialNetworkProtocol; // Updated namespace

// Detect WiFi capability across Arduino boards
#if defined(ESP32) || defined(ESP8266)
#define HOST_HAS_WIFI
#elif defined(WiFiS3_h)
#define HOST_HAS_WIFI
#elif defined(WiFiNINA_h)
#define HOST_HAS_WIFI
#elif defined(WiFi101_h)
#define HOST_HAS_WIFI
#elif defined(PICO_CYW43_ARCH_HAS_WIFI)
#define HOST_HAS_WIFI
#endif

// Maximum length for CA cert filename
#define MAX_FILENAME_LEN 64

// UDP Host Slot State
struct UdpSlotState
{
    IPAddress remoteIP;
    uint16_t remotePort = 0;
    size_t packetSize = 0;
    size_t readPos = 0;
    uint8_t *packetData = nullptr; // Dynamically allocated buffer for the incoming packet

    // For TX reassembly
    uint8_t *txBuffer = nullptr;
    size_t txLen = 0;
    uint16_t txTargetPort = 0;

    // To store the address payload from CMD_C_UDP_BEGIN_PACKET
    uint8_t targetAddrPayload[MAX_PACKET_BUFFER_SIZE];
    size_t targetAddrPayloadLen = 0;
};

struct QueueNode
{
    uint8_t data;
    QueueNode *next;
};

class DynamicQueue
{
private:
    QueueNode *head = nullptr;
    QueueNode *tail = nullptr;
    size_t _count = 0;
    size_t _limit = SERIAL_TCP_HOST_TX_BUFFER_SIZE;

public:
    ~DynamicQueue()
    {
        clear();
    }
    size_t available() { return _count; }
    size_t space()
    {
        if (_count >= _limit)
            return 0;
        return _limit - _count;
    }

    bool enqueue(uint8_t b)
    {
        if (space() == 0)
            return false;
        QueueNode *newNode = new QueueNode;
        if (!newNode)
            return false;
        newNode->data = b;
        newNode->next = nullptr;
        if (tail)
            tail->next = newNode;
        else
            head = newNode;
        tail = newNode;
        _count++;
        return true;
    }

    int dequeue()
    {
        if (!head)
            return -1;
        QueueNode *temp = head;
        int data = temp->data;
        head = head->next;
        if (!head)
            tail = nullptr;
        delete temp;
        _count--;
        return data;
    }
    void clear()
    {
        while (dequeue() != -1)
        {
        }
        _count = 0;
        head = tail = nullptr;
    }
};

typedef bool (*StartTLSCallback)(int slot);
typedef bool (*SetWiFiCallback)(const char *ssid, const char *pass);
typedef bool (*ConnectNetworkCallback)();
typedef void (*RebootCallback)();
typedef void (*PreConnectCallback)(int slot, const char *ca_cert_filename);

class SerialNetworkHost // Renamed from SerialTCPHost
{
private:
    Stream *sink = nullptr;

    // Network Clients
    Client *_tcp_clients[MAX_SLOTS] = {nullptr}; // TCP Clients
    UDP *_udp_clients[MAX_SLOTS] = {nullptr};    // UDP Clients

    // Connection States
    bool _tcp_connected_state[MAX_SLOTS] = {false}; // TCP Connection State
    UdpSlotState _udp_states[MAX_SLOTS];            // UDP State

    PacketReceiver _receiver;
    uint8_t _decoded_buffer[MAX_PACKET_BUFFER_SIZE];

    uint8_t _debug_level = 1;

    uint16_t _session_id = 0;

    StartTLSCallback _tls_callbacks[MAX_SLOTS] = {nullptr};
    SetWiFiCallback _set_wifi_callback = nullptr;
    ConnectNetworkCallback _connect_net_callback = nullptr;
    RebootCallback _reboot_callback = nullptr;
    PreConnectCallback _pre_connect_callback = nullptr;

    char _ca_cert_filenames[MAX_SLOTS][MAX_FILENAME_LEN];

    DynamicQueue _tx_queues[MAX_SLOTS];
    enum class TxState
    {
        IDLE,
        WAIT_FOR_ACK
    };
    TxState _tx_states[MAX_SLOTS] = {TxState::IDLE};

    uint32_t _last_data_send_time[MAX_SLOTS] = {0};
    uint8_t _last_data_packet[MAX_SLOTS][MAX_PACKET_BUFFER_SIZE];
    size_t _last_data_packet_len[MAX_SLOTS] = {0};

    void processCommand(const uint8_t *pkt, size_t len)
    {
        uint8_t cmd = pkt[0];
        uint8_t slot = pkt[1];
        const uint8_t *payload = &pkt[2];
        size_t payload_len = len - 4;

        if (slot == GLOBAL_SLOT_ID)
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            char msg[32];
            snprintf(msg, sizeof(msg), "Got Global Command: %02X", cmd);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
            bool global_success = false;
            switch (cmd)
            {
            case CMD_C_SET_DEBUG:
                if (payload_len > 0)
                {
                    _debug_level = (uint8_t)payload[0];
                    global_success = true;
                }
                break;
            case CMD_C_PING_HOST:
                sendPacket(*sink, CMD_H_PING_RESPONSE, GLOBAL_SLOT_ID, nullptr, 0);
                return;
            case CMD_C_REBOOT_HOST:
                global_success = true; // ACK first
                sendPacket(*sink, CMD_H_ACK, GLOBAL_SLOT_ID, nullptr, 0);
                if (_reboot_callback)
                {
#if defined(ENABLE_SERIALTCP_DEBUG)
                    DEBUG_PRINT(_debug_level, "[Host]", "Invoking reboot callback...");
#endif
                    delay(100);
                    _reboot_callback();
                }
                return;
            case CMD_C_SET_WIFI:
                if (_set_wifi_callback && payload_len > 2)
                {
                    uint8_t ssid_len = payload[0];
                    if (ssid_len > 0 && payload_len > ssid_len + 1)
                    {
                        const char *ssid = (const char *)&payload[1];
                        uint8_t pass_len = payload[1 + ssid_len];
                        if (payload_len >= 2 + ssid_len + pass_len)
                        {
                            const char *pass = (const char *)&payload[2 + ssid_len];
                            char ssid_buf[ssid_len + 1];
                            memcpy(ssid_buf, ssid, ssid_len);
                            ssid_buf[ssid_len] = '\0';
                            char pass_buf[pass_len + 1];
                            memcpy(pass_buf, pass, pass_len);
                            pass_buf[pass_len] = '\0';
                            global_success = _set_wifi_callback(ssid_buf, pass_buf);
                        }
                    }
                }
                break;
            case CMD_C_CONNECT_NET:
                if (_connect_net_callback)
                    global_success = _connect_net_callback();
                break;
            case CMD_C_IS_NET_CONNECTED:
#if defined(HOST_HAS_WIFI)
                global_success = (WiFi.status() == WL_CONNECTED);
#else
                global_success = false;
#endif
                break;
            case CMD_C_DISCONNECT_NET:
#if defined(HOST_HAS_WIFI)
                WiFi.disconnect();
                global_success = true;
#else
                global_success = false;
#endif
                break;
            }
            sendPacket(*sink, global_success ? CMD_H_ACK : CMD_H_NAK, GLOBAL_SLOT_ID, nullptr, 0);
            return;
        }

        if (slot >= MAX_SLOTS)
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            char msg[32];
            snprintf(msg, sizeof(msg), "ERROR: Invalid slot %d", slot);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
            sendPacket(*sink, CMD_H_NAK, slot, nullptr, 0);
            return;
        }

        bool success = false;

        // UDP Command Handling
        if (cmd >= CMD_C_UDP_BEGIN && cmd <= CMD_C_UDP_PARSE_PACKET)
        {
            UDP *udpClient = _udp_clients[slot];
            if (!udpClient)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Host]", "ERROR: No UDP client in slot");
#endif
                sendPacket(*sink, CMD_H_NAK, slot, nullptr, 0);
                return;
            }

#if defined(ENABLE_SERIALTCP_DEBUG)
            char msg[64];
            snprintf(msg, sizeof(msg), "Got UDP Command: %02X for slot %d", cmd, slot);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif

            switch (cmd)
            {
            case CMD_C_UDP_BEGIN:
            {
                if (payload_len < 2)
                    break;
                uint16_t localPort = (uint16_t)(payload[0] << 8) | payload[1];
                if (udpClient->begin(localPort) == 1)
                    success = true;
                break;
            }
            case CMD_C_UDP_END:
            {
                udpClient->stop();
                if (_udp_states[slot].packetData)
                {
                    delete[] _udp_states[slot].packetData;
                    _udp_states[slot].packetData = nullptr;
                }
                if (_udp_states[slot].txBuffer)
                {
                    delete[] _udp_states[slot].txBuffer;
                    _udp_states[slot].txBuffer = nullptr;
                }
                _udp_states[slot].packetSize = 0;
                success = true;
                break;
            }
            case CMD_C_UDP_BEGIN_PACKET:
            {
                if (payload_len < 3)
                    break;
                uint16_t port = (uint16_t)(payload[0] << 8) | payload[1];

                if (_udp_states[slot].txBuffer)
                {
                    delete[] _udp_states[slot].txBuffer;
                }
                _udp_states[slot].txBuffer = new uint8_t[SERIAL_UDP_RX_BUFFER_SIZE];
                _udp_states[slot].txLen = 0;
                _udp_states[slot].txTargetPort = port;

                if (_udp_states[slot].txBuffer && payload_len <= MAX_PACKET_BUFFER_SIZE)
                {
                    memcpy(_udp_states[slot].targetAddrPayload, payload, payload_len);
                    _udp_states[slot].targetAddrPayloadLen = payload_len;
                    success = true;
                }
                else
                {
                    if (_udp_states[slot].txBuffer)
                    {
                        delete[] _udp_states[slot].txBuffer;
                    }
                    _udp_states[slot].txBuffer = nullptr;
                    success = false;
                }
                break;
            }
            case CMD_C_UDP_WRITE_DATA:
            {
                if (!_udp_states[slot].txBuffer || _udp_states[slot].txLen + payload_len > SERIAL_UDP_RX_BUFFER_SIZE)
                    break;
                memcpy(&_udp_states[slot].txBuffer[_udp_states[slot].txLen], payload, payload_len);
                _udp_states[slot].txLen += payload_len;
                success = true;
                break;
            }
            case CMD_C_UDP_END_PACKET:
            {
                uint8_t *addr_payload = _udp_states[slot].targetAddrPayload;
                size_t addr_len = _udp_states[slot].targetAddrPayloadLen;
                uint16_t port = _udp_states[slot].txTargetPort;
                uint8_t type = (addr_len > 2) ? addr_payload[2] : 0xFF;

                bool packet_started = false;
                if (type == 0 && addr_len == 7)
                {
                    IPAddress targetIP(addr_payload[3], addr_payload[4], addr_payload[5], addr_payload[6]);
                    packet_started = udpClient->beginPacket(targetIP, port);
                }
                else if (type == 1 && addr_len > 4)
                {
                    uint8_t host_len = addr_payload[3];
                    char host_name[host_len + 1];
                    memcpy(host_name, &addr_payload[4], host_len);
                    host_name[host_len] = '\0';
                    packet_started = udpClient->beginPacket(host_name, port);
                }

                if (packet_started && _udp_states[slot].txBuffer)
                {
                    size_t written = udpClient->write(_udp_states[slot].txBuffer, _udp_states[slot].txLen);
                    if (written == _udp_states[slot].txLen)
                        success = udpClient->endPacket();
                }

                if (_udp_states[slot].txBuffer)
                    delete[] _udp_states[slot].txBuffer;
                _udp_states[slot].txBuffer = nullptr;
                _udp_states[slot].txLen = 0;
                break;
            }
            case CMD_C_UDP_PARSE_PACKET:
            {
                int packetSize = udpClient->parsePacket();
                if (packetSize > 0)
                {
                    if (_udp_states[slot].packetData)
                    {
                        delete[] _udp_states[slot].packetData;
                    }
                    size_t buf_size = min((size_t)packetSize, (size_t)SERIAL_UDP_RX_BUFFER_SIZE);
                    _udp_states[slot].packetData = new uint8_t[buf_size];

                    if (_udp_states[slot].packetData)
                    {
                        size_t readLen = udpClient->read(_udp_states[slot].packetData, buf_size);
                        if (readLen > 0)
                        {
                            _udp_states[slot].packetSize = readLen;
                            _udp_states[slot].readPos = 0;
                            _udp_states[slot].remoteIP = udpClient->remoteIP();
                            _udp_states[slot].remotePort = udpClient->remotePort();

                            uint8_t response_pld[8];
                            response_pld[0] = _udp_states[slot].remoteIP[0];
                            response_pld[1] = _udp_states[slot].remoteIP[1];
                            response_pld[2] = _udp_states[slot].remoteIP[2];
                            response_pld[3] = _udp_states[slot].remoteIP[3];
                            response_pld[4] = (uint8_t)(_udp_states[slot].remotePort >> 8);
                            response_pld[5] = (uint8_t)(_udp_states[slot].remotePort & 0xFF);
                            response_pld[6] = (uint8_t)(readLen >> 8);
                            response_pld[7] = (uint8_t)(readLen & 0xFF);

                            sendPacket(*sink, CMD_H_UDP_PACKET_INFO, slot, response_pld, 8);
                            sendPacket(*sink, CMD_H_UDP_DATA_PAYLOAD, slot, _udp_states[slot].packetData, readLen);

                            delete[] _udp_states[slot].packetData;
                            _udp_states[slot].packetData = nullptr;
                        }
                        else
                        {
                            delete[] _udp_states[slot].packetData;
                            _udp_states[slot].packetData = nullptr;
                            _udp_states[slot].packetSize = 0;
                        }
                    }
                }
                success = true;

                // PARSE_PACKET implicitly returns ACK if no packet found,
                // or sends INFO/PAYLOAD if packet found.
                // We send ACK at the end of the block if control reaches there.
            }
            }

#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Host]", success ? "Sending ACK" : "Sending NAK");
#endif

            sendPacket(*sink, success ? CMD_H_ACK : CMD_H_NAK, slot, nullptr, 0);
            return;
        }

        // TCP Command Handling
        Client *client = _tcp_clients[slot];

        if (!client && cmd != CMD_C_DATA_ACK && cmd != CMD_C_START_TLS)
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            char msg[32];
            snprintf(msg, sizeof(msg), "ERROR: No client in slot %d", slot);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
            sendPacket(*sink, CMD_H_NAK, slot, nullptr, 0);
            return;
        }

#if defined(ENABLE_SERIALTCP_DEBUG)
        char msg[64];
        snprintf(msg, sizeof(msg), "Got Command: %02X for slot %d", cmd, slot);
        DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif

        switch (cmd)
        {
        case CMD_C_CONNECT_HOST:
        {
            if (payload_len < 5)
                break;
            uint16_t port = (uint16_t)(payload[1] << 8) | payload[2];
            uint8_t host_len = payload[3];
            if (host_len > payload_len - 4)
                break;
            char host_name[host_len + 1];
            memcpy(host_name, &payload[4], host_len);
            host_name[host_len] = '\0';

            // Check if filename string is not empty
            if (_pre_connect_callback && _ca_cert_filenames[slot][0] != '\0')
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                char log[64];
                snprintf(log, sizeof(log), "Invoking pre-connect callback for slot %d", slot);
                DEBUG_PRINT(_debug_level, "[Host]", log);
#endif
                _pre_connect_callback(slot, _ca_cert_filenames[slot]);
            }
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Host]", "Connecting...");
#endif
            if (client->connect(host_name, port))
            {
                success = true;
                _tcp_connected_state[slot] = true;
                _tx_queues[slot].clear();
                _tx_states[slot] = TxState::IDLE;
            }
            // Reset filename buffer
            _ca_cert_filenames[slot][0] = '\0';
            break;
        }
        case CMD_C_WRITE:
            if (client && client->connected())
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Host]", "Writing data...");
#endif
                if (client->write(payload, payload_len) == payload_len)
                {
                    client->flush();
                    success = true;
                }
            }
            break;
        case CMD_C_STOP:
            if (client)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Host]", "Stopping connection");
#endif
                client->stop();
            }
            _tcp_connected_state[slot] = false;
            _tx_queues[slot].clear();
            _tx_states[slot] = TxState::IDLE;
            _ca_cert_filenames[slot][0] = '\0';
            success = true;
            break;

        case CMD_C_POLL_DATA:
        {
            if (payload_len >= 2)
            {
                uint16_t client_sid = (uint16_t)(payload[0] << 8) | payload[1];
                // If client sends non-zero ID and it mismatches ours, reset client
                if (client_sid != 0 && client_sid != _session_id)
                {
#if defined(ENABLE_SERIALTCP_DEBUG)
                    DEBUG_PRINT(_debug_level, "[Host]", "Session Mismatch!");
#endif
                    uint8_t reset_pld[2];
                    reset_pld[0] = (uint8_t)(_session_id >> 8);
                    reset_pld[1] = (uint8_t)(_session_id & 0xFF);
                    sendPacket(*sink, CMD_H_HOST_RESET, GLOBAL_SLOT_ID, reset_pld, 2);
                    return;
                }
            }

            bool is_connected = (client ? (uint8_t)client->connected() : 0);
            // Data length 0 for poll response (data is pushed async)
            size_t pld_len = 3;
            uint8_t response_pld[pld_len];
            response_pld[0] = (uint8_t)is_connected;
            response_pld[1] = 0;
            response_pld[2] = 0;

            sendPacket(*sink, CMD_H_POLL_RESPONSE, slot, response_pld, pld_len);
            return;
        }

        case CMD_C_IS_CONNECTED:
        {
#if defined(ENABLE_SERIALTCP_DEBUG)
            DEBUG_PRINT(_debug_level, "[Host]", "Client is polling for status");
#endif
            uint8_t status = (client ? (uint8_t)client->connected() : 0);
            _tcp_connected_state[slot] = status;
            sendPacket(*sink, CMD_H_CONNECTED_STATUS, slot, &status, 1);
            return;
        }

        case CMD_C_DATA_ACK:
            if (_tx_states[slot] == TxState::WAIT_FOR_ACK)
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                char msg[40];
                snprintf(msg, sizeof(msg), "Got Data ACK for slot %d", slot);
                DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                _tx_states[slot] = TxState::IDLE;
            }
            return;

        case CMD_C_START_TLS:
#if defined(ENABLE_SERIALTCP_DEBUG)
        {
            char msg[40];
            snprintf(msg, sizeof(msg), "Got STARTTLS for slot %d", slot);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
        }
#endif
            if (_tls_callbacks[slot] != nullptr)
            {
                success = _tls_callbacks[slot](slot);
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Host]", success ? "STARTTLS OK" : "STARTTLS Failed");
#endif
            }
            else
            {
#if defined(ENABLE_SERIALTCP_DEBUG)
                char msg[50];
                snprintf(msg, sizeof(msg), "ERROR: No STARTTLS callback for slot %d", slot);
                DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                success = false;
            }
            break;

        case CMD_C_SET_CA_CERT:
        {
            if (payload_len > 0 && payload_len < MAX_FILENAME_LEN)
            {
                memcpy(_ca_cert_filenames[slot], payload, payload_len);
                _ca_cert_filenames[slot][payload_len] = '\0';
#if defined(ENABLE_SERIALTCP_DEBUG)
                DEBUG_PRINT(_debug_level, "[Host]", "Set sticky CA cert for slot");
#endif
                success = true;
            }
            break;
        }

        default:
#if defined(ENABLE_SERIALTCP_DEBUG)
            char msg[32];
            snprintf(msg, sizeof(msg), "ERROR: Unknown command %02X", cmd);
            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
            break;
        }
#if defined(ENABLE_SERIALTCP_DEBUG)
        DEBUG_PRINT(_debug_level, "[Host]", success ? "Sending ACK" : "Sending NAK");
#endif
        sendPacket(*sink, success ? CMD_H_ACK : CMD_H_NAK, slot, nullptr, 0);
    }

    void processSerial()
    {
        while (sink->available())
        {
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
                        processCommand(_decoded_buffer, decodedLen);
                    }
                    else
                    {
#if defined(ENABLE_SERIALTCP_DEBUG)
                        DEBUG_PRINT(_debug_level, "[Host]", "ERROR: Bad CRC!");
#endif
                    }
                }
            }
        }
    }

    void queueNetworkData()
    {
        for (int i = 0; i < MAX_SLOTS; i++)
        {
            if (_tcp_clients[i])
            {
                while (_tcp_clients[i]->available() > 0 && _tx_queues[i].space() > 0)
                {
                    _tx_queues[i].enqueue(_tcp_clients[i]->read());
                }

                bool is_connected = _tcp_clients[i]->connected();

                if (is_connected != _tcp_connected_state[i])
                {
                    if (is_connected)
                    {
#if defined(ENABLE_SERIALTCP_DEBUG)
                        char msg[50];
                        snprintf(msg, sizeof(msg), "Pushing connect status (1) to slot %d", i);
                        DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                        _tcp_connected_state[i] = is_connected;
                        uint8_t status = (uint8_t)is_connected;
                        sendPacket(*sink, CMD_H_CONNECTED_STATUS, i, &status, 1);
                    }
                    else
                    {
                        while (_tcp_clients[i]->available() > 0 && _tx_queues[i].space() > 0)
                        {
                            _tx_queues[i].enqueue(_tcp_clients[i]->read());
                        }

                        if (_tx_queues[i].available() == 0 && _tx_states[i] == TxState::IDLE)
                        {
#if defined(ENABLE_SERIALTCP_DEBUG)
                            char msg[50];
                            snprintf(msg, sizeof(msg), "Queue empty. Pushing disconnect to slot %d", i);
                            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                            _tcp_connected_state[i] = is_connected;
                            _ca_cert_filenames[i][0] = '\0'; // Clear cert filename
                            uint8_t status = (uint8_t)is_connected;
                            sendPacket(*sink, CMD_H_CONNECTED_STATUS, i, &status, 1);
                        }
                        else
                        {
#if defined(ENABLE_SERIALTCP_DEBUG)
                            char msg[64];
                            snprintf(msg, sizeof(msg), "Queue has %u bytes. Delaying disconnect.", (unsigned int)_tx_queues[i].available());
                            DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                        }
                    }
                }
            }
        }
    }

    void processDataQueues()
    {
        for (int i = 0; i < MAX_SLOTS; i++)
        {
            if (!_tcp_clients[i])
                continue;

            if (_tx_states[i] == TxState::WAIT_FOR_ACK)
            {
                if (millis() - _last_data_send_time[i] > SERIAL_TCP_DATA_PACKET_TIMEOUT)
                {
#if defined(ENABLE_SERIALTCP_DEBUG)
                    char msg[50];
                    snprintf(msg, sizeof(msg), "Timeout, resending data to slot %d", i);
                    DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif

                    size_t rawLen = 2 + _last_data_packet_len[i];
                    uint16_t crc = calculate_crc16(_last_data_packet[i], rawLen);
                    _last_data_packet[i][rawLen] = (uint8_t)(crc & 0xFF);
                    _last_data_packet[i][rawLen + 1] = (uint8_t)(crc >> 8);
                    rawLen += 2;

                    uint8_t cobsPkt[MAX_PACKET_BUFFER_SIZE + 2];
                    size_t cobsLen = cobs_encode(_last_data_packet[i], rawLen, cobsPkt);

                    sink->write(cobsPkt, cobsLen);
                    sink->write(FRAME_DELIMITER);

                    _last_data_send_time[i] = millis();
                }
                continue;
            }

            if (_tx_states[i] == TxState::IDLE && _tx_queues[i].available() > 0)
            {
                size_t len_to_send = min(_tx_queues[i].available(), (size_t)SERIAL_TCP_DATA_PAYLOAD_SIZE);

                _last_data_packet_len[i] = len_to_send;
                _last_data_packet[i][0] = CMD_H_DATA_PAYLOAD;
                _last_data_packet[i][1] = i;

                for (size_t j = 0; j < len_to_send; j++)
                {
                    int data = _tx_queues[i].dequeue();
                    if (data == -1)
                    {
                        len_to_send = j;
                        break;
                    }
                    _last_data_packet[i][j + 2] = (uint8_t)data;
                }

                _last_data_packet_len[i] = len_to_send;
                if (len_to_send == 0)
                    continue;
#if defined(ENABLE_SERIALTCP_DEBUG)
                char msg[50];
                snprintf(msg, sizeof(msg), "Sending %u bytes to slot %d", (unsigned int)len_to_send, i);
                DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
                sendPacket(*sink, CMD_H_DATA_PAYLOAD, i, &_last_data_packet[i][2], len_to_send);

                _tx_states[i] = TxState::WAIT_FOR_ACK;
                _last_data_send_time[i] = millis();
            }
        }
    }

public:
    /**
     * @brief Constructor for SerialTCPHost.
     * @param sink The Stream interface (e.g., Serial, Serial1) used for
     * communicating with the device running SerialTCPClient.
     */
    SerialNetworkHost(Stream &sink) : sink(&sink)
    {
        _session_id = (uint16_t)micros();
        if (_session_id == 0)
            _session_id = 1;

        // Initialize filename buffers
        for (int i = 0; i < MAX_SLOTS; i++)
        {
            _ca_cert_filenames[i][0] = '\0';
            _udp_states[i].packetData = nullptr; // Initialize UDP state
            _udp_states[i].txBuffer = nullptr;   // Initialize UDP state
        }
    }

    // Destructor to clean up dynamic memory used by UDP state
    ~SerialNetworkHost()
    {
        for (int i = 0; i < MAX_SLOTS; i++)
        {
            if (_udp_states[i].packetData)
                delete[] _udp_states[i].packetData;
            if (_udp_states[i].txBuffer)
                delete[] _udp_states[i].txBuffer;
        }
    }

    /**
     * @brief Broadcasts a HOST_RESET command to all clients.
     * This function should be called in the Host's setup() function
     * after Serial initialization. It sends a packet containing the
     * Host's current Session ID. Any connected clients receiving this
     * will know the host has rebooted and will reset their connection state.
     */
    void notifyBoot()
    {
#if defined(ENABLE_SERIALTCP_DEBUG)
        char msg[50];
        snprintf(msg, sizeof(msg), "Broadcasting HOST_RESET. Session: %04X", _session_id);
        DEBUG_PRINT(_debug_level, "[Host]", msg);
#endif
        uint8_t payload[2];
        payload[0] = (uint8_t)(_session_id >> 8);
        payload[1] = (uint8_t)(_session_id & 0xFF);
        sendPacket(*sink, CMD_H_HOST_RESET, GLOBAL_SLOT_ID, payload, 2);
        delay(50);
    }

    /**
     * @brief Sets the Client object for a specific slot (TCP).
     * @param client Pointer to the Client object (e.g., WiFiClient).
     * @param slot The client slot (0 to MAX_SLOTS-1).
     */
    void setTCPClient(Client *client, int slot)
    {
        if (slot >= 0 && slot < MAX_SLOTS)
        {
            _tcp_clients[slot] = client;
            _tcp_connected_state[slot] = false;
        }
    }

    /**
     * @brief Sets the UDP Client object for a specific slot.
     * @param udpClient Pointer to the UDP object (e.g., WiFiUDP).
     * @param slot The client slot (0 to MAX_SLOTS-1).
     */
    void setUDPClient(UDP *udpClient, int slot)
    {
        if (slot >= 0 && slot < MAX_SLOTS)
        {
            _udp_clients[slot] = udpClient;
        }
    }

    /**
     * @brief Sets the StartTLS callback for a specific slot.
     * @param slot The client slot (0 to MAX_SLOTS-1).
     * @param callback The StartTLSCallback function pointer.
     */
    void setStartTLSCallback(int slot, StartTLSCallback callback)
    {
        if (slot >= 0 && slot < MAX_SLOTS)
        {
            _tls_callbacks[slot] = callback;
        }
    }

    /**
     * @brief Sets the SetWiFi callback.
     * @param callback The SetWiFiCallback function pointer.
     */
    void setSetWiFiCallback(SetWiFiCallback callback) { _set_wifi_callback = callback; }

    /**
     * @brief Sets the ConnectNetwork callback.
     * @param callback The ConnectNetworkCallback function pointer.
     */
    void setConnectNetworkCallback(ConnectNetworkCallback callback) { _connect_net_callback = callback; }

    /**
     * @brief Sets the Reboot callback.
     * @param callback The RebootCallback function pointer.
     */
    void setRebootCallback(RebootCallback callback) { _reboot_callback = callback; }

    /**
     * @brief Sets the Pre-Connect callback.
     * @param callback The PreConnectCallback function pointer.
     */
    void setPreConnectCallback(PreConnectCallback callback) { _pre_connect_callback = callback; }

    /**
     * @brief Sets the local debug level for the host.
     * @param level The debug level (0 = none, higher = more verbose).
     */
    void setLocalDebugLevel(int level) { _debug_level = (uint8_t)level; }

    /**
     * @brief Main loop function to be called regularly.
     */
    void loop()
    {
        processSerial();
        queueNetworkData();
        processDataQueues();
    }
};

#endif