#pragma once

// =============================================================================
// inverter_scales.h — Register scale factors for SinoSoar SP6030
// Source: SinoSoar PCS Modbus Communication Protocol V3.0, sheet 数据点表1
//
// Usage: physical_value = raw_int16 * SCALE_xxx
// All registers are int16 unless noted. Negative values = bidirectional.
// =============================================================================

// --- Frequency (reg 100, 170, 200) ---
// Unit: Hz, Precision: 0.01 Hz
#define SCALE_FREQ_HZ       0.01f

// --- AC voltages: line (reg 101-103) and phase (reg 107-109, 177-179, 204-206) ---
// Unit: V, Precision: 0.1 V
#define SCALE_VOLTAGE_V     0.1f

// --- AC currents: inverter (reg 104-106) and grid/load (reg 174-176, 201-203) ---
// Unit: A, Precision: 0.1 A
#define SCALE_CURRENT_A     0.1f

// --- Active power: per-phase and total (reg 110-112, 122, 141, 180-182, 192, 207-209, 213) ---
// Unit: kW, Precision: 0.01 kW
#define SCALE_POWER_KW      0.01f

// --- Reactive power: per-phase and total (reg 113-115, 123) ---
// Unit: kVAR, Precision: 0.01 kVAR
#define SCALE_REACTIVE_KVAR 0.01f

// --- Apparent power: per-phase and total (reg 116-118, 124, 186-188, 210-212) ---
// Unit: kVA, Precision: 0.01 kVA
#define SCALE_APPARENT_KVA  0.01f

// --- Power factor: per-phase and total (reg 119-121, 125) ---
// Dimensionless, Precision: 0.01
#define SCALE_PF            0.01f

// --- DC (reg 141-143) ---
// DC power: kW, Precision: 0.01 kW  → same as SCALE_POWER_KW
// DC voltage: V, Precision: 0.1 V   → same as SCALE_VOLTAGE_V
// DC current: A, Precision: 0.1 A   → same as SCALE_CURRENT_A

// --- Writable: Active power dispatch (reg 135) ---
// Range: -100.0 to +100.0 kW, Precision: 0.1 kW
// Positive = discharge, Negative = charge
// raw = kW * 10  (i.e. 25.0 kW → 250)
#define SCALE_SET_POWER_KW  0.1f
