#pragma once
#include <Arduino.h>

uint16_t crc16(const uint8_t* buf, uint8_t len);
bool readRegisters(uint16_t startReg, uint16_t count, int16_t* out);
bool writeRegister(uint16_t reg, int16_t value);
