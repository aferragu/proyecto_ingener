#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// bms_parser — LWS BMS Modbus register parsing
// No Arduino/ESP-IDF dependencies — safe to include in host unit tests.
// Protocol: LWS Modbus Communication Protocol V1.36
// =============================================================================

struct BmsData {
    // — Electrical —
    float    voltage_v;           // total pack voltage (V)
    float    current_a;           // current (A, positive=charging, negative=discharging)
    float    max_charge_a;        // max allowed charge current (A)
    float    max_discharge_a;     // max allowed discharge current (A)
    float    charge_cutoff_v;     // charge cutoff voltage (V)   — overcharge protection on
    float    discharge_cutoff_v;  // discharge cutoff voltage (V) — overdischarge protection on

    // — State —
    float    soc_pct;             // State of Charge (%)
    float    soh_pct;             // State of Health (%)

    // — Temperature —
    float    temp_avg_c;          // average cell temperature (°C)
    float    temp_cell_max_c;     // max cell temperature (°C)
    float    temp_cell_min_c;     // min cell temperature (°C)
    float    temp_fet_c;          // FET temperature (°C)

    // — Cell voltages —
    float    cell_voltage_max_v;  // max cell voltage (V)
    float    cell_voltage_min_v;  // min cell voltage (V)

    // — Status flags —
    bool     charging;            // BMS in charging state
    bool     discharging;         // BMS in discharging state
    bool     charge_forbidden;    // charge MOSFET off → charging forbidden
    bool     discharge_forbidden; // discharge MOSFET off → discharging forbidden
    bool     force_charge_req;    // low SOC alarm → force charge request

    // — Diagnostics —
    uint8_t  fault;               // fault byte (0x1007 byte0)
    uint16_t alarm;               // alarm word (0x1005)
    uint16_t protection;          // protection word (0x1006)

    bool     valid;               // true once successfully parsed
};

// Register map — read 0x1000..0x1012 in one block (19 registers)
// plus 0x101D and 0x1020 (cutoff voltages) and 0x2500..0x2501 (max currents)
#define BMS_REG_START       0x1000
#define BMS_REG_COUNT       19      // 0x1000–0x1012 (skip 0x100F and 0x1013 reserved)
#define BMS_REG_CHG_CUTOFF  0x101D
#define BMS_REG_DISCHG_CUTOFF 0x1020
#define BMS_REG_MAX_CHG_A   0x2500
#define BMS_REG_MAX_DISCHG_A 0x2501

// Parse a block of registers read starting at BMS_REG_START into bms.
// r[i] = register value at address (BMS_REG_START + i)
void bms_parse_modbus(const int16_t* r, BmsData& bms,
                      int16_t chg_cutoff_raw, int16_t dischg_cutoff_raw,
                      uint16_t max_chg_raw, uint16_t max_dischg_raw);
