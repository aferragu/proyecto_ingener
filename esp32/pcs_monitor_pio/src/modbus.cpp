#include "modbus.h"
#include "config.h"

static ModbusMaster node;

static void preTransmission() {
    digitalWrite(RS485_DE_RE_PIN, HIGH);
}

static void postTransmission() {
    digitalWrite(RS485_DE_RE_PIN, LOW);
}

void modbusInit() {
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    node.begin(MODBUS_DEVICE_ID, RS485_SERIAL);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
}

bool readRegisters(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t result = node.readHoldingRegisters(startReg, count);
    if (result != node.ku8MBSuccess) {
        Serial.printf("[Modbus] Read reg %d count %d failed: 0x%02X\n",
                      startReg, count, result);
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)node.getResponseBuffer(i);
    return true;
}

bool writeRegister(uint16_t reg, int16_t value) {
    uint8_t result = node.writeSingleRegister(reg, (uint16_t)value);
    if (result != node.ku8MBSuccess) {
        Serial.printf("[Modbus] Write reg %d = %d failed: 0x%02X\n",
                      reg, value, result);
        return false;
    }
    return true;
}
