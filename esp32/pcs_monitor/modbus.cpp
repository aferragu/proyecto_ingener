#include "modbus.h"
#include "config.h"

uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

bool readRegisters(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    frame[0] = MODBUS_DEVICE_ID;
    frame[1] = 0x03;
    frame[2] = startReg >> 8;  frame[3] = startReg & 0xFF;
    frame[4] = count    >> 8;  frame[5] = count    & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[256];
    uint32_t t = millis();
    uint8_t  idx = 0;
    while ((millis() - t) < 50 && idx == 0) {
        if (RS485_SERIAL.available())
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20) {
        if (RS485_SERIAL.available() && idx < sizeof(rxBuf))
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx < 5) return false;
    if (crc16(rxBuf, idx - 2) != ((uint16_t)rxBuf[idx-1] << 8 | rxBuf[idx-2])) return false;
    if (rxBuf[1] & 0x80) return false;
    if (rxBuf[2] != count * 2) return false;

    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)(((uint16_t)rxBuf[3 + i*2] << 8) | rxBuf[4 + i*2]);
    return true;
}

bool writeRegister(uint16_t reg, int16_t value) {
    uint8_t frame[8];
    frame[0] = MODBUS_DEVICE_ID;  frame[1] = 0x06;
    frame[2] = reg   >> 8;  frame[3] = reg   & 0xFF;
    frame[4] = (uint16_t)value >> 8;  frame[5] = (uint16_t)value & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[8];
    uint32_t t = millis();
    uint8_t  idx = 0;
    while ((millis() - t) < 50 && idx == 0) {
        if (RS485_SERIAL.available())
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20) {
        if (RS485_SERIAL.available() && idx < 8)
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx < 8) return false;
    if (crc16(rxBuf, 6) != ((uint16_t)rxBuf[7] << 8 | rxBuf[6])) return false;
    if (rxBuf[1] & 0x80) return false;
    return true;
}
