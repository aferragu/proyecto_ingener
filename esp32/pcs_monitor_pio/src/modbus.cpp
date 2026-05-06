#include "modbus.h"
#include "modbus_core.h"
#include "config.h"

bool readRegisters(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    modbus_build_read(frame, MODBUS_DEVICE_ID, startReg, count);

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

    return modbus_parse_read(rxBuf, idx, count, out);
}

bool writeRegister(uint16_t reg, int16_t value) {
    uint8_t frame[8];
    modbus_build_write(frame, MODBUS_DEVICE_ID, reg, value);

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

    return modbus_parse_write(rxBuf, idx);
}
