#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>

void     modbusInit();
bool     readRegisters(uint16_t startReg, uint16_t count, int16_t* out);
bool     writeRegister(uint16_t reg, int16_t value);
