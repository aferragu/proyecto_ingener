#include "inverter.h"
#include "inverter_core.h"
#include "modbus.h"
#include "config.h"

void readFirmwareVersion(PubSubClient& mqtt) {
    Serial.println("[Version] Leyendo versión del firmware...");
    int16_t v[REG_VERSION_COUNT];
    if (!readRegisters(REG_VERSION_START, REG_VERSION_COUNT, v)) {
        Serial.println("[Version] Error leyendo registros 0-21");
        return;
    }
    uint32_t hw_ver  = ((uint32_t)(uint16_t)v[12] << 16) | (uint16_t)v[13];
    uint32_t dsp_fw  = ((uint32_t)(uint16_t)v[14] << 16) | (uint16_t)v[15];
    uint32_t com_fw  = ((uint32_t)(uint16_t)v[17] << 16) | (uint16_t)v[18];
    uint16_t rtu_ver = (uint16_t)v[19];

    Serial.printf("[Version] Model: %d  HW: %08X  DSP FW: %08X  COM FW: %08X  RTU: %d\n",
                  v[1], hw_ver, dsp_fw, com_fw, rtu_ver);

    if (rtu_ver >= 30)
        Serial.println("[Version] ✓ Protocolo V3.0+ — load side registers disponibles");
    else
        Serial.printf("[Version] ⚠ Protocolo V%d — registros 200-213 pueden no estar disponibles\n", rtu_ver);

    StaticJsonDocument<256> attrs;
    attrs["fw_model"]        = v[1];
    attrs["fw_hw_version"]   = hw_ver;
    attrs["fw_dsp_version"]  = dsp_fw;
    attrs["fw_com_version"]  = com_fw;
    attrs["fw_rtu_protocol"] = rtu_ver;
    char payload[256];
    serializeJson(attrs, payload, sizeof(payload));
    mqtt.publish("v1/devices/me/attributes", payload);
    Serial.println("[Version] Atributos publicados a ThingsBoard");
}

void inverterInit() {
    Serial.println("[Init] Configurando inversor...");
    modbusInit();
    inverter_run_init(writeRegister, readRegisters);
    Serial.println("[Init] Listo.");
}

void verifyAndReinit() {
    static const struct { uint16_t reg; int16_t expected; const char* name; } cfg[] = {
        { 763,    0, "Max DC discharge"          },
        { 764,    0, "Max DC charge"             },
        { 341,    1, "3-phase ctrl"              },
        { 652,    0, "PV switch"                 },
        { 795,    0, "Leakage detect"            },
        { 656,    0, "DCDC switch"               },
        { 873,    0, "Function mgmt (on-grid)"   },
        { 758,    0, "Grid sched mode (AC pwr)"  },
    };
    bool anyFixed = false;
    for (const auto& c : cfg) {
        int16_t cur;
        if (!readRegisters(c.reg, 1, &cur)) { Serial.printf("[Verify] Error reg %d\n", c.reg); continue; }
        if (cur != c.expected) {
            Serial.printf("[Verify] reg %d: %d → %d\n", c.reg, cur, c.expected);
            writeRegister(c.reg, c.expected);
            anyFixed = true;
            delay(100);
        }
    }
    Serial.println(anyFixed ? "[Verify] Parámetros corregidos — posible reinicio del inversor"
                             : "[Verify] Config OK");
}

void pollModbus(JsonDocument& telemetry) {
    telemetry.clear();

    int16_t raw[1];
    if (readRegisters(REG_STATUS, REG_STATUS_COUNT, raw)) {
        StatusData s;
        inverter_parse_status(raw, s);
        telemetry["fault"]     = s.fault;
        telemetry["alarm"]     = s.alarm;
        telemetry["running"]   = s.running;
        telemetry["grid_tied"] = s.grid_tied;
        telemetry["off_grid"]  = s.off_grid;
        telemetry["derating"]  = s.derating;
        telemetry["standby"]   = s.standby;
    } else Serial.println("[Modbus] Error: reg 32 (status)");

    int16_t ac_raw[REG_AC_COUNT];
    if (readRegisters(REG_AC_START, REG_AC_COUNT, ac_raw)) {
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
    } else Serial.println("[Modbus] Error: reg 100-125 (AC)");

    int16_t dc_raw[REG_DC_COUNT];
    if (readRegisters(REG_DC_START, REG_DC_COUNT, dc_raw)) {
        DcData dc;
        inverter_parse_dc(dc_raw, dc);
        telemetry["dc_power_kw"]  = dc.power_kw;
        telemetry["dc_voltage_v"] = dc.voltage_v;
        telemetry["dc_current_a"] = dc.current_a;
    } else Serial.println("[Modbus] Error: reg 141-143 (DC)");

    int16_t grid_raw[REG_GRID_COUNT];
    int16_t grid_p_raw = 0;
    bool grid_ok  = readRegisters(REG_GRID_START, REG_GRID_COUNT, grid_raw);
    bool gridp_ok = readRegisters(REG_GRID_POWER, 1, &grid_p_raw);
    if (grid_ok && gridp_ok) {
        GridData g;
        inverter_parse_grid(grid_raw, grid_p_raw, g);
        telemetry["grid_freq_hz"] = g.freq_hz;
        telemetry["grid_v_a"]     = g.v_a;
        telemetry["grid_v_b"]     = g.v_b;
        telemetry["grid_v_c"]     = g.v_c;
        telemetry["grid_p_kw"]    = g.p_kw;
    } else Serial.println("[Modbus] Error: reg 170-179 / 192 (grid)");

    int16_t load_raw[REG_LOAD_COUNT];
    if (readRegisters(REG_LOAD_START, REG_LOAD_COUNT, load_raw)) {
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
    } else Serial.println("[Modbus] Error: reg 200-213 (load V3.0)");
}
