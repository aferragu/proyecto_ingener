// =============================================================================
// inverter.cpp — Capa de hardware para inversor SinoSoar SP6030
//
// Instancia propia de ModbusMaster (FC03 lectura, FC06 escritura).
// Protocolo: SinoSoar PCS Modbus V3.0
// Baud: 115200, device ID configurable en config.h (MODBUS_DEVICE_ID)
//
// API pública:
//   inverterInit(serial, deRePin) — init Modbus y secuencia de arranque
//   pollModbus(telemetry)         — lee todos los bloques de registros
//   inverterSetPower(kw)          — escribe setpoint AC (reg 135, 0.1kW)
//   inverterPowerOn()             — enciende inversor (reg 650 = 1)
//   inverterShutdown()            — apaga inversor    (reg 650 = 0)
//   verifyAndReinit()             — verifica y corrige registros de config
//   readFirmwareVersion(mqtt)     — lee versión y publica como atributos TB
// =============================================================================
#include "inverter.h"
#include "inverter_parser.h"
#include "inverter_scales.h"
#include "config.h"
#include <ModbusMaster.h>

// ---------------------------------------------------------------------------
// Modbus node — inverter
// ---------------------------------------------------------------------------
static ModbusMaster inv;

static uint8_t _deRePin;

static void preTransmission()  { digitalWrite(_deRePin, HIGH); }
static void postTransmission() { digitalWrite(_deRePin, LOW);  }

static bool inverterRead(uint16_t reg, uint16_t count, int16_t* out) {
    uint8_t result = inv.readHoldingRegisters(reg, count);
    if (result != ModbusMaster::ku8MBSuccess) {
        Serial.printf("[Inverter] Read reg %d count %d failed: 0x%02X\n", reg, count, result);
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)inv.getResponseBuffer(i);
    return true;
}

static bool inverterWrite(uint16_t reg, int16_t value) {
    uint8_t result = inv.writeSingleRegister(reg, (uint16_t)value);
    if (result != ModbusMaster::ku8MBSuccess) {
        Serial.printf("[Inverter] Write reg %d = %d failed: 0x%02X\n", reg, value, result);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void inverterInit(HardwareSerial& serial, uint8_t deRePin) {
    _deRePin = deRePin;
    pinMode(deRePin, OUTPUT);
    digitalWrite(deRePin, LOW);
    inv.begin(MODBUS_DEVICE_ID, serial);
    inv.preTransmission(preTransmission);
    inv.postTransmission(postTransmission);

    Serial.println("[Inverter] Configurando...");
    bool ok = inverter_run_init(inverterWrite, inverterRead);
    Serial.printf("[Inverter] Init %s\n", ok ? "OK" : "WARNING: algún registro falló");
}

// ---------------------------------------------------------------------------
// Firmware version
// ---------------------------------------------------------------------------
void readFirmwareVersion(PubSubClient& mqtt) {
    int16_t v[REG_VERSION_COUNT];
    if (!inverterRead(REG_VERSION_START, REG_VERSION_COUNT, v)) {
        Serial.println("[Inverter] Error leyendo versión");
        return;
    }
    uint32_t hw_ver  = ((uint32_t)(uint16_t)v[12] << 16) | (uint16_t)v[13];
    uint32_t dsp_fw  = ((uint32_t)(uint16_t)v[14] << 16) | (uint16_t)v[15];
    uint32_t com_fw  = ((uint32_t)(uint16_t)v[17] << 16) | (uint16_t)v[18];
    uint16_t rtu_ver = (uint16_t)v[19];

    Serial.printf("[Inverter] Model: %d  HW: %08X  DSP: %08X  COM: %08X  RTU: %d\n",
                  v[1], hw_ver, dsp_fw, com_fw, rtu_ver);
    if (rtu_ver >= 30)
        Serial.println("[Inverter] ✓ Protocolo V3.0+");
    else
        Serial.printf("[Inverter] ⚠ Protocolo V%d — regs 200-213 pueden no estar\n", rtu_ver);

    JsonDocument attrs;
    attrs["fw_model"]        = v[1];
    attrs["fw_hw_version"]   = hw_ver;
    attrs["fw_dsp_version"]  = dsp_fw;
    attrs["fw_com_version"]  = com_fw;
    attrs["fw_rtu_protocol"] = rtu_ver;
    char payload[256];
    serializeJson(attrs, payload, sizeof(payload));
    mqtt.publish("v1/devices/me/attributes", payload);
}

// ---------------------------------------------------------------------------
// Verify and reinit
// ---------------------------------------------------------------------------
void verifyAndReinit() {
    static const struct { uint16_t reg; int16_t expected; const char* name; } cfg[] = {
        { 763, 1500, "Max DC discharge"         },
        { 764, 1500, "Max DC charge"            },
        { 341,    1, "3-phase ctrl"             },
        { 652,    0, "PV switch"                },
        { 795,    0, "Leakage detect"           },
        { 656,    0, "DCDC switch"              },
        { 873,    0, "Function mgmt (on-grid)"  },
        { 758,    0, "Grid sched mode (AC pwr)" },
    };
    bool anyFixed = false;
    for (const auto& c : cfg) {
        int16_t cur;
        if (!inverterRead(c.reg, 1, &cur)) { Serial.printf("[Verify] Error reg %d\n", c.reg); continue; }
        if (cur != c.expected) {
            Serial.printf("[Verify] reg %d: %d → %d\n", c.reg, cur, c.expected);
            inverterWrite(c.reg, c.expected);
            anyFixed = true;
            delay(100);
        }
    }
    Serial.println(anyFixed ? "[Verify] Parámetros corregidos" : "[Verify] Config OK");
}

// ---------------------------------------------------------------------------
// Set power
// ---------------------------------------------------------------------------
bool inverterSetPower(float kw) {
    int16_t raw = (int16_t)(kw / SCALE_SET_POWER_KW);
    return inverterWrite(REG_SET_POWER, raw);
}

bool inverterPowerOn() {
    return inverterWrite(REG_POWER_ON, 1);
}

bool inverterShutdown() {
    return inverterWrite(REG_POWER_ON, 0);
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------
void pollModbus(JsonDocument& telemetry) {
    telemetry.clear();

    int16_t raw[1];
    if (inverterRead(REG_STATUS, REG_STATUS_COUNT, raw)) {
        StatusData s;
        inverter_parse_status(raw, s);
        telemetry["fault"]     = s.fault;
        telemetry["alarm"]     = s.alarm;
        telemetry["running"]   = s.running;
        telemetry["grid_tied"] = s.grid_tied;
        telemetry["off_grid"]  = s.off_grid;
        telemetry["derating"]  = s.derating;
        telemetry["standby"]   = s.standby;
    } else Serial.println("[Inverter] Error: reg 32 (status)");

    int16_t ac_raw[REG_AC_COUNT];
    if (inverterRead(REG_AC_START, REG_AC_COUNT, ac_raw)) {
        AcData ac;
        inverter_parse_ac(ac_raw, ac);
        telemetry["freq_hz"]    = ac.freq_hz;
        telemetry["v_ab"]       = ac.v_ab;
        telemetry["v_bc"]       = ac.v_bc;
        telemetry["v_ca"]       = ac.v_ca;
        telemetry["i_a"]        = ac.i_a;
        telemetry["i_b"]        = ac.i_b;
        telemetry["i_c"]        = ac.i_c;
        telemetry["v_a"]        = ac.v_a;
        telemetry["v_b"]        = ac.v_b;
        telemetry["v_c"]        = ac.v_c;
        telemetry["p_a_kw"]     = ac.p_a;
        telemetry["p_b_kw"]     = ac.p_b;
        telemetry["p_c_kw"]     = ac.p_c;
        telemetry["q_a_kvar"]   = ac.q_a;
        telemetry["q_b_kvar"]   = ac.q_b;
        telemetry["q_c_kvar"]   = ac.q_c;
        telemetry["pf_a"]       = ac.pf_a;
        telemetry["pf_b"]       = ac.pf_b;
        telemetry["pf_c"]       = ac.pf_c;
        telemetry["p_inv_kw"]   = ac.p_inv;
        telemetry["q_inv_kvar"] = ac.q_inv;
        telemetry["pf_total"]   = ac.pf_total;
    } else Serial.println("[Inverter] Error: reg 100-125 (AC)");

    int16_t dc_raw[REG_DC_COUNT];
    if (inverterRead(REG_DC_START, REG_DC_COUNT, dc_raw)) {
        DcData dc;
        inverter_parse_dc(dc_raw, dc);
        telemetry["dc_power_kw"]  = dc.power_kw;
        telemetry["dc_voltage_v"] = dc.voltage_v;
        telemetry["dc_current_a"] = dc.current_a;
    } else Serial.println("[Inverter] Error: reg 141-143 (DC)");

    int16_t grid_raw[REG_GRID_COUNT];
    int16_t grid_p_raw = 0;
    if (inverterRead(REG_GRID_START, REG_GRID_COUNT, grid_raw) &&
        inverterRead(REG_GRID_POWER, 1, &grid_p_raw)) {
        GridData g;
        inverter_parse_grid(grid_raw, grid_p_raw, g);
        telemetry["grid_freq_hz"] = g.freq_hz;
        telemetry["grid_v_a"]     = g.v_a;
        telemetry["grid_v_b"]     = g.v_b;
        telemetry["grid_v_c"]     = g.v_c;
        telemetry["grid_p_kw"]    = g.p_kw;
    } else Serial.println("[Inverter] Error: reg 170-179 / 192 (grid)");

    int16_t load_raw[REG_LOAD_COUNT];
    if (inverterRead(REG_LOAD_START, REG_LOAD_COUNT, load_raw)) {
        LoadData l;
        inverter_parse_load(load_raw, l);
        telemetry["load_freq_hz"] = l.freq_hz;
        telemetry["load_i_a"]     = l.i_a;
        telemetry["load_i_b"]     = l.i_b;
        telemetry["load_i_c"]     = l.i_c;
        telemetry["load_v_a"]     = l.v_a;
        telemetry["load_v_b"]     = l.v_b;
        telemetry["load_v_c"]     = l.v_c;
        telemetry["load_p_a_kw"]  = l.p_a;
        telemetry["load_p_b_kw"]  = l.p_b;
        telemetry["load_p_c_kw"]  = l.p_c;
        telemetry["load_p_kw"]    = l.p_total;
        telemetry["load_s_kva"]   = l.s_total;
    } else Serial.println("[Inverter] Error: reg 200-213 (load V3.0)");
}
