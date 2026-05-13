// =============================================================================
// test_bms_hw — Hardware sketch: Modbus RTU RS-485 → BMS LWS → Serial debug
//
// Polls the LWS BMS via Modbus RTU (FC04) every 5s and prints all register
// blocks to Serial with scaled physical values.
// No WiFi, no ThingsBoard, no display — pure Serial diagnostics.
//
// NOTA: El BMS LWS opera a 9600 baud (fijo, no configurable).
// En producción el BMS comparte bus con el inversor — esto no es posible ya que
// el inversor opera a 115200. Este sketch conecta el BMS SOLO en el bus para
// diagnóstico. En producción habrá que usar un segundo UART (Serial1).
//
// Wiring (BMS solo en el bus):
//   MAX485 DI    → GPIO17
//   MAX485 RO    → GPIO16
//   MAX485 DE+RE → GPIO5
//   MAX485 A/B   → RS-485 → BMS LWS
//
// Device ID: BMS_MODBUS_DEVICE_ID (config.h, default 51 — DIP switch en batería)
// =============================================================================

#include <Arduino.h>
#include <ModbusMaster.h>
#include "config.h"
#include "bms_parser.h"
#include "bms_scales.h"

#define POLL_INTERVAL_MS 5000
#define BMS_BAUD         9600

static ModbusMaster node;

static void preTransmission()  { digitalWrite(RS485_DE_RE_PIN, HIGH); }
static void postTransmission() { digitalWrite(RS485_DE_RE_PIN, LOW);  }

static const char* modbusErrorStr(uint8_t code) {
    switch (code) {
        case 0x01: return "illegal function";
        case 0x02: return "illegal data address";
        case 0x03: return "illegal data value";
        case 0x04: return "slave device failure";
        case 0xE0: return "invalid slave ID";
        case 0xE1: return "invalid function";
        case 0xE2: return "response timed out";
        case 0xE3: return "invalid CRC";
        default:   return "unknown error";
    }
}

static bool bms_read(uint16_t reg, uint16_t count, int16_t* out) {
    uint8_t r = node.readInputRegisters(reg, count);  // LWS uses FC04
    if (r != ModbusMaster::ku8MBSuccess) {
        Serial.printf("  [FAIL 0x%02X — %s]\n", r, modbusErrorStr(r));
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)node.getResponseBuffer(i);
    return true;
}

void pollAndPrint() {
    Serial.println("\n========================================");
    Serial.printf("Poll @ %lums\n", millis());
    Serial.println("========================================");

    // Block 1: main data 0x1000–0x1012
    int16_t r[BMS_REG_COUNT];
    if (bms_read(BMS_REG_START, BMS_REG_COUNT, r)) {
        Serial.println("\n[Main]");
        Serial.printf("  voltage     = %.2f V\n",  (uint16_t)r[0x00] * BMS_SCALE_VOLTAGE_V);
        Serial.printf("  current     = %.2f A\n",  r[0x01] * BMS_SCALE_CURRENT_A);
        Serial.printf("  soc         = %.1f %%\n", (uint16_t)r[0x08] * BMS_SCALE_SOC_PCT);
        Serial.printf("  soh         = %.1f %%\n", (uint16_t)r[0x09] * BMS_SCALE_SOC_PCT);
        Serial.printf("  temp_avg    = %.1f C\n",  r[0x03] * BMS_SCALE_TEMP_C);
        Serial.printf("  temp_max    = %.1f C\n",  r[0x10] * BMS_SCALE_TEMP_C);
        Serial.printf("  temp_min    = %.1f C\n",  r[0x11] * BMS_SCALE_TEMP_C);
        Serial.printf("  temp_fet    = %.1f C\n",  r[0x12] * BMS_SCALE_TEMP_C);
        Serial.printf("  cell_v_max  = %.3f V\n",  (uint16_t)r[0x0D] * BMS_SCALE_CELL_VOLTAGE_V);
        Serial.printf("  cell_v_min  = %.3f V\n",  (uint16_t)r[0x0E] * BMS_SCALE_CELL_VOLTAGE_V);
        Serial.printf("  alarm       = 0x%04X\n",  (uint16_t)r[0x05]);
        Serial.printf("  protection  = 0x%04X\n",  (uint16_t)r[0x06]);
        Serial.printf("  fault/status= 0x%04X\n",  (uint16_t)r[0x07]);
        uint8_t status = (uint16_t)r[0x07] & 0xFF;
        Serial.printf("  charging=%d  discharging=%d  chg_mos=%d  dischg_mos=%d\n",
                      (status >> 0) & 1, (status >> 1) & 1,
                      (status >> 2) & 1, (status >> 3) & 1);
    } else Serial.println("\n[Main] FAIL — regs 0x1000-0x1012");

    // Block 2: charge cutoff 0x101D
    int16_t chg_cutoff = 0;
    if (bms_read(BMS_REG_CHG_CUTOFF, 1, &chg_cutoff))
        Serial.printf("\n[Config]\n  charge_cutoff    = %.2f V\n",
                      (uint16_t)chg_cutoff * BMS_SCALE_CUTOFF_V);
    else Serial.println("\n[Config] FAIL — reg 0x101D (charge cutoff)");

    // Block 3: discharge cutoff 0x1020
    int16_t dischg_cutoff = 0;
    if (bms_read(BMS_REG_DISCHG_CUTOFF, 1, &dischg_cutoff))
        Serial.printf("  discharge_cutoff = %.2f V\n",
                      (uint16_t)dischg_cutoff * BMS_SCALE_CUTOFF_V);
    else Serial.println("  FAIL — reg 0x1020 (discharge cutoff)");

    // Block 4: max currents 0x2500-0x2501
    int16_t max_curr[2] = {};
    if (bms_read(BMS_REG_MAX_CHG_A, 2, max_curr))
        Serial.printf("  max_charge_a     = %.1f A\n  max_discharge_a  = %.1f A\n",
                      (uint16_t)max_curr[0] * BMS_SCALE_MAX_CURRENT_A,
                      (uint16_t)max_curr[1] * BMS_SCALE_MAX_CURRENT_A);
    else Serial.println("  FAIL — regs 0x2500-0x2501 (max currents)");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_bms_hw starting...");
    Serial.printf("[Boot] Device ID: %d  Baud: %d  DE/RE: GPIO%d\n",
                  BMS_MODBUS_DEVICE_ID, BMS_BAUD, RS485_DE_RE_PIN);

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    Serial2.begin(BMS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    node.begin(BMS_MODBUS_DEVICE_ID, Serial2);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);

    Serial.println("[Boot] Ready — polling every 5s");
}

void loop() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollAndPrint();
    }
}
