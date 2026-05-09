#pragma once

// =============================================================================
// bms_scales.h — Modbus register scale factors for LWS BMS
// Protocol: LWS Modbus Communication Protocol V1.36
//
// Usage: physical = (int16_t)raw * SCALE_xxx
// All registers are 2 bytes, high byte first.
// =============================================================================

// 0x1000 — Total battery pack voltage (UINT16, 0.01 V)
#define BMS_SCALE_VOLTAGE_V         0.01f

// 0x1001 — Battery pack current (INT16, 0.01 A, discharge = negative)
#define BMS_SCALE_CURRENT_A         0.01f

// 0x1003 — Average cell temperature (INT16, 0.1 °C)
// 0x1010 — Max cell temperature     (INT16, 0.1 °C)
// 0x1011 — Min cell temperature     (INT16, 0.1 °C)
// 0x1012 — FET temperature          (INT16, 0.1 °C)
#define BMS_SCALE_TEMP_C            0.1f

// 0x1008 — SOC (UINT16, 0.1 %)
// 0x1009 — SOH (UINT16, 0.1 %)
#define BMS_SCALE_SOC_PCT           0.1f

// 0x100C — Max charging current (UINT16, 0.01 A)
#define BMS_SCALE_MAX_CHG_A         0.01f

// 0x100D — Max cell voltage (UINT16, 0.001 V)
// 0x100E — Min cell voltage (UINT16, 0.001 V)
#define BMS_SCALE_CELL_VOLTAGE_V    0.001f

// 0x101D — Total voltage overcharge protection on  (UINT16, 0.01 V) → charge cutoff
// 0x1020 — Total voltage overdischarge protection on (UINT16, 0.01 V) → discharge cutoff
#define BMS_SCALE_CUTOFF_V          0.01f

// 0x2500 — Max charging current  (UINT16, 100 mA units → A)
// 0x2501 — Max discharge current (UINT16, 100 mA units → A)
#define BMS_SCALE_MAX_CURRENT_A     0.1f

// 0x1007 byte1 bit definitions
#define BMS_STATUS_CHARGING_BIT     0   // bit 0: charging status
#define BMS_STATUS_DISCHARGING_BIT  1   // bit 1: discharging status
#define BMS_STATUS_CHG_MOS_BIT      2   // bit 2: charge MOSFET on (0=forbidden)
#define BMS_STATUS_DISCHG_MOS_BIT   3   // bit 3: discharge MOSFET on (0=forbidden)

// 0x1005 byte1 bit3: low SOC alarm → force charge request
#define BMS_ALARM_LOW_SOC_BIT       3
