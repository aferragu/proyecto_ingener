#include "modbus_core.h"

uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

void modbus_build_read(uint8_t* frame, uint8_t device_id,
                       uint16_t start_reg, uint16_t count) {
    frame[0] = device_id;
    frame[1] = 0x03;
    frame[2] = start_reg >> 8;  frame[3] = start_reg & 0xFF;
    frame[4] = count     >> 8;  frame[5] = count     & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;
}

void modbus_build_write(uint8_t* frame, uint8_t device_id,
                        uint16_t reg, int16_t value) {
    frame[0] = device_id;
    frame[1] = 0x06;
    frame[2] = reg >> 8;               frame[3] = reg & 0xFF;
    frame[4] = (uint16_t)value >> 8;   frame[5] = (uint16_t)value & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;
}

bool modbus_parse_read(const uint8_t* buf, uint8_t len,
                       uint16_t count, int16_t* out) {
    if (len < 5)                                          return false;
    if (buf[1] & 0x80)                                    return false;  // exception
    if (buf[2] != count * 2)                              return false;  // wrong byte count
    if (crc16(buf, len - 2) !=
        ((uint16_t)buf[len-1] << 8 | buf[len-2]))        return false;  // bad CRC

    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)(((uint16_t)buf[3 + i*2] << 8) | buf[4 + i*2]);
    return true;
}

bool modbus_parse_write(const uint8_t* buf, uint8_t len) {
    if (len < 8)                                          return false;
    if (buf[1] & 0x80)                                    return false;  // exception
    if (crc16(buf, 6) !=
        ((uint16_t)buf[7] << 8 | buf[6]))                return false;  // bad CRC
    return true;
}
