#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// modbus_core — pure Modbus RTU framing and parsing
// No Arduino/ESP32 dependencies — safe to include in host unit tests.
// =============================================================================

// CRC16 (Modbus polynomial 0xA001)
uint16_t crc16(const uint8_t* buf, uint8_t len);

// Build a FC03 read request frame into buf[8]. Returns the CRC appended.
void modbus_build_read(uint8_t* frame, uint8_t device_id,
                       uint16_t start_reg, uint16_t count);

// Build a FC06 write request frame into buf[8].
void modbus_build_write(uint8_t* frame, uint8_t device_id,
                        uint16_t reg, int16_t value);

// Parse a FC03 response. Returns false on CRC error, exception, or wrong byte count.
// On success, fills out[0..count-1] with the register values.
bool modbus_parse_read(const uint8_t* buf, uint8_t len,
                       uint16_t count, int16_t* out);

// Validate a FC06 write response (echo check + CRC).
bool modbus_parse_write(const uint8_t* buf, uint8_t len);
