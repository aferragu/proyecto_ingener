#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// inverter_core — pure register decoding for SinoSoar SP6030
// No Arduino/ESP32 dependencies — safe to include in host unit tests.
// Scale factors from: SinoSoar PCS Modbus Communication Protocol V3.0
// =============================================================================

// --- Register addresses (from protocol table) --------------------------------
#define REG_DC_MAX_DISCHG_CURRENT  763
#define REG_DC_MAX_CHG_CURRENT     764
#define REG_3PHASE_CTRL_MODE       341
#define REG_PV_SWITCH              652
#define REG_LEAKAGE_DETECT         795
#define REG_DCDC_SWITCH            656
#define REG_ANTI_BACKFLOW          873
#define REG_POWER_ON               650

// Decoded AC measurements (registers 100–125)
struct AcData {
    float freq_hz;
    float v_ab, v_bc, v_ca;       // line voltages (V)
    float i_a,  i_b,  i_c;        // phase currents (A)
    float v_a,  v_b,  v_c;        // phase voltages (V)
    float p_a,  p_b,  p_c;        // active power per phase (kW)
    float q_a,  q_b,  q_c;        // reactive power per phase (kvar)
    float pf_a, pf_b, pf_c;       // power factor per phase
    float p_inv;                   // total active power (kW)
    float q_inv;                   // total reactive power (kvar)
    float pf_total;                // total power factor
};

// Decoded DC measurements (registers 141–143)
struct DcData {
    float power_kw;
    float voltage_v;
    float current_a;
};

// Decoded grid measurements (registers 170–192)
struct GridData {
    float freq_hz;
    float v_a, v_b, v_c;          // phase voltages (V)
    float p_kw;                    // total active power (kW)
};

// Decoded load measurements (registers 200–213, protocol V3.0+)
struct LoadData {
    float freq_hz;
    float i_a,  i_b,  i_c;
    float v_a,  v_b,  v_c;
    float p_a,  p_b,  p_c;        // per-phase active power (kW)
    float p_total;                 // total active power (kW)
    float s_total;                 // total apparent power (kVA)
};

// Status register (reg 32) bit fields
struct StatusData {
    bool fault;
    bool alarm;
    bool running;
    bool grid_tied;
    bool off_grid;
    bool derating;
    bool standby;
};

// Init sequence entry — one register write
struct InitCmd {
    uint16_t    reg;
    int16_t     val;
    const char* name;
};

// Function pointer types for hardware-independent init
typedef bool (*WriteRegFn)(uint16_t reg, int16_t value);
typedef bool (*ReadRegFn)(uint16_t reg, uint16_t count, int16_t* out);

// Parse raw registers from readRegisters() into typed structs.
// raw[] must have at least the count passed to readRegisters().
void inverter_parse_ac    (const int16_t* raw, AcData&     out);
void inverter_parse_dc    (const int16_t* raw, DcData&     out);
void inverter_parse_grid  (const int16_t* raw, int16_t grid_p_raw, GridData& out);
void inverter_parse_load  (const int16_t* raw, LoadData&   out);
void inverter_parse_status(const int16_t* raw, StatusData& out);

// Returns the standard init sequence and its length.
// Caller must NOT write REG_ANTI_BACKFLOW (873) from this table —
// that one requires a read-modify-write and is handled separately.
const InitCmd* inverter_init_sequence(uint8_t* count_out);

// Run the full init sequence using the provided read/write functions.
// Prints progress to Serial. Safe to call from any sketch or firmware.
void inverter_run_init(WriteRegFn write_fn, ReadRegFn read_fn);
