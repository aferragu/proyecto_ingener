#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// bms_core — pure Pylontech high-voltage CAN frame decoding
// No Arduino/ESP-IDF dependencies — safe to include in host unit tests.
// Protocol: CANBus Protocol Pylontech High Voltage V1.24
// =============================================================================

struct BmsData {
    float    voltage_v;           // total pack voltage (V)
    float    current_a;           // current (A, positive=charging, negative=discharging)
    float    temperature_c;       // BMS temperature (°C)
    uint8_t  soc_pct;             // State of Charge (%)
    uint8_t  soh_pct;             // State of Health (%)
    uint8_t  soe_pct;             // State of Energy available (%)
    float    max_charge_a;        // max allowed charge current (A)
    float    max_discharge_a;     // max allowed discharge current (A)
    float    charge_cutoff_v;     // charge cutoff voltage (V)
    float    discharge_cutoff_v;  // discharge cutoff voltage (V)

    bool     charge_forbidden;    // BMS prohibits charging
    bool     discharge_forbidden; // BMS prohibits discharging

    uint8_t  status;              // 0=sleep 1=charging 2=discharging 3=idle
    bool     force_charge_req;    // BMS requests forced charge

    uint8_t  fault;
    uint16_t alarm;
    uint16_t protection;

    bool     valid;               // true once at least one 0x421 message received
};

// Decode one CAN frame (11-bit id, 8 data bytes) into bms.
// bms_addr is the BMS address from config (1-15).
// Call repeatedly as frames arrive — each ID updates a different field group.
void bms_decode(BmsData& bms, uint8_t bms_addr,
                uint32_t can_id, const uint8_t* data);
