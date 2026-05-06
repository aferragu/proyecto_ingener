#pragma once

// =============================================================================
// bms_scales.h — CAN frame scale factors for Pylontech high-voltage BMS
// Source: CANBus Protocol Pylontech High Voltage V1.24
//
// Usage: physical_value = (raw_uint16 * SCALE_xxx) - OFFSET_xxx
// All raw values are uint16 assembled from two consecutive bytes (big-endian).
// =============================================================================

// --- 0x421: Ensemble information ---

// Bytes 0-1: Battery pack total voltage
// Precision: 0.1 V, Offset: 0
#define BMS_SCALE_VOLTAGE_V         0.1f
#define BMS_OFFSET_VOLTAGE_V        0.0f

// Bytes 2-3: Battery pack current (positive = charging, negative = discharging)
// Precision: 0.1 A, Offset: -3000 A  (0x7530 = 30000 raw → 0 A)
#define BMS_SCALE_CURRENT_A         0.1f
#define BMS_OFFSET_CURRENT_A        3000.0f

// Bytes 4-5: BMS temperature (second-level BMS / master controller)
// Precision: 0.1 °C, Offset: -100 °C  (0x03E8 = 1000 raw → 0 °C)
#define BMS_SCALE_TEMP_C            0.1f
#define BMS_OFFSET_TEMP_C           100.0f

// Byte 6: State of Charge
// Precision: 1 %, Offset: 0
#define BMS_SCALE_SOC_PCT           1

// Byte 7: State of Health
// Precision: 1 %, Offset: 0
#define BMS_SCALE_SOH_PCT           1

// --- 0x422: Charge/discharge limits ---

// Bytes 0-1: Charge cutoff voltage
// Precision: 0.1 V, Offset: 0
#define BMS_SCALE_CUTOFF_V          0.1f
#define BMS_OFFSET_CUTOFF_V         0.0f

// Bytes 2-3: Discharge cutoff voltage
// Same scale as charge cutoff

// Bytes 4-5: Max charge current
// Precision: 0.1 A, Offset: -3000 A  (same encoding as pack current)
#define BMS_SCALE_MAX_CURRENT_A     0.1f
#define BMS_OFFSET_MAX_CURRENT_A    3000.0f

// Bytes 6-7: Max discharge current
// Same scale as max charge current

// --- 0x425: Status, fault, alarm, protection ---
// No scaling — bitfields, decoded by masking (see bms_core.h)

// Byte 0, bits 2:0 — operating status
//   0 = Sleep, 1 = Charge, 2 = Discharge, 3 = Idle
// Byte 0, bit 3 — forced charge request
// Byte 0, bit 4 — balance charge request
// Byte 1         — fault byte (see protocol Table 2)
// Bytes 2-3      — alarm word (see protocol Table 3)
// Bytes 4-5      — protection word (see protocol Table 4)  (note: byte 6-7 in protocol)

// --- 0x428: Forbidden flags and SOE ---

// Byte 0: Charge forbidden    — 0xAA = forbidden, any other value = allowed
// Byte 1: Discharge forbidden — 0xAA = forbidden, any other value = allowed
// Byte 2: Heartbeat counter   — increments each response (not stored in BmsData)
// Byte 3: State of Energy available (dischargeable energy percentage)
// Precision: 1 %, Offset: 0
#define BMS_SCALE_SOE_PCT           1
#define BMS_FORBIDDEN_MARK          0xAA
