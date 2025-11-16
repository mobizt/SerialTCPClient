#ifndef SERIAL_TCP_HELPER_H
#define SERIAL_TCP_HELPER_H
#include <Arduino.h>

#define MAX_SPLIT_TOKENS 10
#define MAX_SPLIT_TOKEN_LEN 50

#define START_DELIMITER '<'
#define STOP_DELIMITER '>'
#define CRC_SIZE 2 // 2 bytes for CRC-16

namespace SerialTCPHelper_NS
{
  static inline void DEBUG_STATUS(bool enable, const char *x)
  {
    if (enable)
      Serial.print(x);
  }

  struct serial_bridge_status_context
  {
    int status_length = 0;
    char result[20];
    char caller[20];
    bool net_status = false;
    char ssid[32];
    char pass[32];
  };

  class SerialTCPHelper
  {
  public:
    static inline void yield()
    {
#if defined(ARDUINO_ESP8266_MAJOR) && defined(ARDUINO_ESP8266_MINOR) && defined(ARDUINO_ESP8266_REVISION) && ((ARDUINO_ESP8266_MAJOR == 3 && ARDUINO_ESP8266_MINOR >= 1) || ARDUINO_ESP8266_MAJOR > 3)
      esp_yield();
#else
      delay(0);
#endif
    }

    static uint8_t hexCharToByte(char c)
    {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      return 0; // Note: For robust decoding, this should ideally signal an error
    }

    static void byteToHex(uint8_t b, unsigned char *hex)
    {
      const char *hex_chars = "0123456789ABCDEF";
      hex[0] = hex_chars[(b >> 4) & 0x0F];
      hex[1] = hex_chars[b & 0x0F];
    }

    template <typename Sink>
    static void encode(Sink &client_sink, const uint8_t *buf, size_t len, int &sent)
    {
      unsigned char hex[2];
      for (size_t i = 0; i < len; ++i)
      {
        yield();
        byteToHex(buf[sent + i], hex);
        client_sink.write(hex, 2);
      }
    }

    // DECODING UTILITIES (Retained for backwards compatibility if needed, but not used by new protocol)

    static uint8_t *decode(const char *encoded)
    {
      size_t hexLen = strlen(encoded);
      size_t rawLen = hexLen / 2;

      uint8_t *decoded = (uint8_t *)malloc(rawLen + 2);
      if (!decoded)
      {
        return nullptr;
      }

      for (size_t i = 0; i < rawLen; i++)
      {
        yield();
        uint8_t high = hexCharToByte(encoded[i * 2]);
        uint8_t low = hexCharToByte(encoded[i * 2 + 1]);
        decoded[i] = (high << 4) | low;
      }
      decoded[rawLen] = '\0';
      return decoded;
    }

    // STRING/MEMORY UTILITIES

    static int manual_copy(char *dest, char *src, int n)
    {
      int count = 0;
      while (count < n && *src != '\0')
      {
        *dest = *src;
        dest++;
        src++;
        count++;
      }
      *dest = '\0';
      return count;
    }

    static int extract_and_store_tokens(const char *src_str, char tokens_array[MAX_SPLIT_TOKENS][MAX_SPLIT_TOKEN_LEN])
    {
      const char *current = src_str;
      const char *start_of_token = src_str;
      int token_index = 0;

      while (*current != '\0' && token_index < MAX_SPLIT_TOKENS)
      {
        if (*current == ' ')
        {
          int token_len = (int)(current - start_of_token);
          manual_copy(tokens_array[token_index], (char *)start_of_token, token_len);
          token_index++;
          start_of_token = current + 1;
        }
        else if (*(current + 1) == '\0')
        {
          int token_len = (int)(current - start_of_token) + 1;
          manual_copy(tokens_array[token_index], (char *)start_of_token, token_len);
          token_index++;
        }
        current++;
      }
      return token_index;
    }

    // CRC CALCULATION

    // CRC-16-CCITT (Poly: 0x1021, Init: 0xFFFF, XOR Out: 0x0000, Ref In: False, Ref Out: False)
    static uint16_t calculate_crc16(const uint8_t *data, size_t length)
    {
      uint16_t crc = 0xFFFF;
      for (size_t i = 0; i < length; i++)
      {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
        {
          if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
          else
            crc <<= 1;
        }
      }
      return crc;
    }

    // FRAMING AND VALIDATION UTILITIES
    static size_t construct_and_encode_frame(const uint8_t *raw_data_buf, size_t data_len, char *encoded_frame_buf, size_t max_frame_len)
    {

      size_t total_bytes_to_encode = data_len + CRC_SIZE;
      size_t encoded_hex_len = total_bytes_to_encode * 2;
      size_t required_len = encoded_hex_len + 3; // Start + Encoded + Stop + Null

      if (required_len > max_frame_len || max_frame_len == 0)
      {
        return 0;
      }

      // Calculate CRC
      uint16_t crc = calculate_crc16(raw_data_buf, data_len);

      // Build temporary buffer containing Data + CRC
      uint8_t temp_buf[total_bytes_to_encode];
      memcpy(temp_buf, raw_data_buf, data_len);

      // Append the 2-byte CRC value (Low byte, then High byte)
      temp_buf[data_len] = (uint8_t)(crc & 0xFF);
      temp_buf[data_len + 1] = (uint8_t)(crc >> 8);

      // Frame Construction
      encoded_frame_buf[0] = START_DELIMITER;
      size_t current_pos = 1;

      // Encode Data + CRC into Hex
      const char *hex_chars = "0123456789ABCDEF";
      for (size_t i = 0; i < total_bytes_to_encode; ++i)
      {
        uint8_t b = temp_buf[i];
        encoded_frame_buf[current_pos++] = hex_chars[(b >> 4) & 0x0F];
        encoded_frame_buf[current_pos++] = hex_chars[b & 0x0F];
      }

      encoded_frame_buf[current_pos++] = STOP_DELIMITER;
      encoded_frame_buf[current_pos] = '\0';

      return current_pos;
    }

    static uint8_t *deconstruct_and_validate_frame_only(const char *received_frame, size_t *data_len)
    {

      size_t frame_len = strlen(received_frame);

      // Basic Frame Check: Delimiters
      if (frame_len < 4 || received_frame[0] != START_DELIMITER || received_frame[frame_len - 1] != STOP_DELIMITER)
      {
        *data_len = 0;
        return NULL;
      }

      // Isolate the encoded hex string (remove delimiters)
      const char *encoded_hex = received_frame + 1;
      size_t encoded_hex_len = frame_len - 2;

      // Hex Format Check
      if (encoded_hex_len % 2 != 0 || encoded_hex_len < 2 * CRC_SIZE)
      {
        *data_len = 0;
        return NULL;
      }

      // Decode Hex String into Raw Bytes
      size_t total_raw_bytes = encoded_hex_len / 2;
      uint8_t raw_temp[total_raw_bytes];

      for (size_t i = 0; i < total_raw_bytes; i++)
      {
        uint8_t high = hexCharToByte(encoded_hex[i * 2]);
        uint8_t low = hexCharToByte(encoded_hex[i * 2 + 1]);
        raw_temp[i] = (high << 4) | low;
      }

      // Isolate Data and CRC
      size_t actual_data_len = total_raw_bytes - CRC_SIZE;
      uint16_t received_crc = (uint16_t)raw_temp[total_raw_bytes - 2] | ((uint16_t)raw_temp[total_raw_bytes - 1] << 8);

      // CRC Validation
      uint16_t calculated_crc = calculate_crc16(raw_temp, actual_data_len);

      if (calculated_crc == received_crc)
      {
        // Data is VALID! Allocate memory for the status tag/payload and copy it.
        uint8_t *decoded_status_tag = (uint8_t *)malloc(actual_data_len + 1);

        if (!decoded_status_tag)
        {
          *data_len = 0;
          return NULL;
        }

        memcpy(decoded_status_tag, raw_temp, actual_data_len);
        decoded_status_tag[actual_data_len] = '\0'; // Null-terminate

        *data_len = actual_data_len;
        return decoded_status_tag;
      }
      else
      {
        // CRC Mismatch: Data corruption detected!
        *data_len = 0;
        return NULL;
      }
    }
  };

}

#endif