// =============================================================================
// bms.cpp — Capa de hardware para BMS LWS
//
// Instancia propia de ModbusMaster (FC04 lectura — LWS usa Input Registers).
// Protocolo: LWS Modbus Communication Protocol V1.36
// Baud: 115200 (configurar en BMS antes de conectar al bus compartido)
// Device ID: configurable en config.h (BMS_MODBUS_DEVICE_ID, default 51)
//
// Comparte el bus RS-485 con el inversor — mismo Serial2 y mismo pin DE/RE.
// inverterInit() debe llamarse antes que bmsInit() para configurar el pin.
//
// API pública:
//   bmsInit(serial, deRePin) — init Modbus, no requiere secuencia de arranque
//   pollBMS(telemetry)       — lee todos los registros y publica telemetría
// =============================================================================
#include "bms.h"
#include "bms_parser.h"
#include "config.h"
#include <ModbusMaster.h>

BmsData bmsData = {};

// ---------------------------------------------------------------------------
// Modbus node — BMS
// ---------------------------------------------------------------------------
static ModbusMaster bms;

static uint8_t _deRePin;

static void preTransmission()  { digitalWrite(_deRePin, HIGH); }
static void postTransmission() { digitalWrite(_deRePin, LOW);  }

static bool bmsRead(uint16_t reg, uint16_t count, int16_t* out) {
    uint8_t result = bms.readInputRegisters(reg, count);  // LWS uses FC04
    if (result != ModbusMaster::ku8MBSuccess) {
        Serial.printf("[BMS] Read reg 0x%04X count %d failed: 0x%02X\n", reg, count, result);
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)bms.getResponseBuffer(i);
    return true;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void bmsInit(HardwareSerial& serial, uint8_t deRePin) {
    _deRePin = deRePin;
    // DE/RE already configured by inverterInit — same pin, same bus
    bms.begin(BMS_MODBUS_DEVICE_ID, serial);
    bms.preTransmission(preTransmission);
    bms.postTransmission(postTransmission);
    Serial.printf("[BMS] LWS Modbus ready — device ID %d\n", BMS_MODBUS_DEVICE_ID);
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------
void pollBMS(JsonDocument& telemetry) {
    int16_t r[BMS_REG_COUNT];
    if (!bmsRead(BMS_REG_START, BMS_REG_COUNT, r)) {
        Serial.println("[BMS] FAIL: reg 0x1000 block");
        return;
    }

    int16_t chg_cutoff = 0, dischg_cutoff = 0;
    int16_t max_currents[2] = {};
    bmsRead(BMS_REG_CHG_CUTOFF,    1, &chg_cutoff);
    bmsRead(BMS_REG_DISCHG_CUTOFF, 1, &dischg_cutoff);
    bmsRead(BMS_REG_MAX_CHG_A,     2, max_currents);

    bms_parse_modbus(r, bmsData,
                     chg_cutoff, dischg_cutoff,
                     (uint16_t)max_currents[0], (uint16_t)max_currents[1]);

    if (!bmsData.valid) return;

    telemetry["bms_voltage_v"]           = bmsData.voltage_v;
    telemetry["bms_current_a"]           = bmsData.current_a;
    telemetry["bms_soc_pct"]             = bmsData.soc_pct;
    telemetry["bms_soh_pct"]             = bmsData.soh_pct;
    telemetry["bms_temp_avg_c"]          = bmsData.temp_avg_c;
    telemetry["bms_temp_cell_max_c"]     = bmsData.temp_cell_max_c;
    telemetry["bms_temp_cell_min_c"]     = bmsData.temp_cell_min_c;
    telemetry["bms_temp_fet_c"]          = bmsData.temp_fet_c;
    telemetry["bms_cell_v_max"]          = bmsData.cell_voltage_max_v;
    telemetry["bms_cell_v_min"]          = bmsData.cell_voltage_min_v;
    telemetry["bms_max_charge_a"]        = bmsData.max_charge_a;
    telemetry["bms_max_discharge_a"]     = bmsData.max_discharge_a;
    telemetry["bms_charge_cutoff_v"]     = bmsData.charge_cutoff_v;
    telemetry["bms_discharge_cutoff_v"]  = bmsData.discharge_cutoff_v;
    telemetry["bms_charging"]            = bmsData.charging        ? 1 : 0;
    telemetry["bms_discharging"]         = bmsData.discharging     ? 1 : 0;
    telemetry["bms_charge_forbidden"]    = bmsData.charge_forbidden    ? 1 : 0;
    telemetry["bms_discharge_forbidden"] = bmsData.discharge_forbidden ? 1 : 0;
    telemetry["bms_force_charge"]        = bmsData.force_charge_req    ? 1 : 0;
    telemetry["bms_fault"]               = bmsData.fault;
    telemetry["bms_alarm"]               = bmsData.alarm;
    telemetry["bms_protection"]          = bmsData.protection;

    Serial.printf("[BMS] SOC=%.1f%% V=%.2fV I=%.2fA T=%.1f°C fault=%d\n",
                  bmsData.soc_pct, bmsData.voltage_v, bmsData.current_a,
                  bmsData.temp_avg_c, bmsData.fault);
}
