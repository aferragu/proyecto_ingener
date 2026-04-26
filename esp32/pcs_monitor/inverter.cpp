#include "inverter.h"
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
    struct { uint16_t reg; int16_t val; const char* name; } cmds[] = {
        { REG_DC_MAX_DISCHG_CURRENT, 1500, "Max DC discharge = 150A" },
        { REG_DC_MAX_CHG_CURRENT,    1500, "Max DC charge = 150A"    },
        { REG_3PHASE_CTRL_MODE,         1, "Control por fase"        },
        { REG_PV_SWITCH,                0, "PV OFF"                  },
        { REG_LEAKAGE_DETECT,           0, "Leakage OFF"             },
        { REG_DCDC_SWITCH,              0, "DCDC OFF"                },
    };
    for (auto& cmd : cmds) {
        bool ok = writeRegister(cmd.reg, cmd.val);
        Serial.printf("[Init] reg %d (%s): %s\n", cmd.reg, cmd.name, ok ? "OK" : "FAIL");
        delay(100);
    }
    // reg 873: anti-backflow — read-modify-write
    int16_t cur873 = 0;
    readRegisters(REG_ANTI_BACKFLOW, 1, &cur873);
    int16_t newVal = cur873 | 0x01;
    bool ok = writeRegister(REG_ANTI_BACKFLOW, newVal);
    Serial.printf("[Init] reg 873 (Anti-backflow, val=0x%04X): %s\n", (uint16_t)newVal, ok ? "OK" : "FAIL");
    Serial.println("[Init] Listo.");
}

void verifyAndReinit() {
    static const struct { uint16_t reg; int16_t expected; const char* name; } cfg[] = {
        { 763, 1500, "Max DC discharge" },
        { 764, 1500, "Max DC charge"    },
        { 341,    1, "3-phase ctrl"     },
        { 652,    0, "PV switch"        },
        { 795,    0, "Leakage detect"   },
        { 656,    0, "DCDC switch"      },
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
    // reg 873 read-modify-write
    int16_t cur873;
    if (readRegisters(873, 1, &cur873)) {
        if ((cur873 & 0x01) == 0) {
            writeRegister(873, cur873 | 0x01);
            Serial.println("[Verify] reg 873 Anti-backflow corregido");
            anyFixed = true;
        }
    }
    Serial.println(anyFixed ? "[Verify] Parámetros corregidos — posible reinicio del inversor"
                             : "[Verify] Config OK");
}

void pollModbus(JsonDocument& telemetry) {
    telemetry.clear();

    int16_t status[1];
    if (readRegisters(REG_STATUS, REG_STATUS_COUNT, status)) {
        telemetry["fault"]     = (status[0] >> 0) & 1;
        telemetry["alarm"]     = (status[0] >> 1) & 1;
        telemetry["running"]   = (status[0] >> 2) & 1;
        telemetry["grid_tied"] = (status[0] >> 3) & 1;
        telemetry["off_grid"]  = (status[0] >> 4) & 1;
        telemetry["derating"]  = (status[0] >> 5) & 1;
        telemetry["standby"]   = (status[0] >> 7) & 1;
    } else Serial.println("[Modbus] Error: reg 32 (status)");

    int16_t ac[REG_AC_COUNT];
    if (readRegisters(REG_AC_START, REG_AC_COUNT, ac)) {
        telemetry["freq_hz"]    = ac[0]  * 0.01f;
        telemetry["v_ab"]       = ac[1]  * 0.1f;
        telemetry["v_bc"]       = ac[2]  * 0.1f;
        telemetry["v_ca"]       = ac[3]  * 0.1f;
        telemetry["i_a"]        = ac[4]  * 0.1f;
        telemetry["i_b"]        = ac[5]  * 0.1f;
        telemetry["i_c"]        = ac[6]  * 0.1f;
        telemetry["v_a"]        = ac[7]  * 0.1f;
        telemetry["v_b"]        = ac[8]  * 0.1f;
        telemetry["v_c"]        = ac[9]  * 0.1f;
        telemetry["p_a_kw"]     = ac[10] * 0.01f;
        telemetry["p_b_kw"]     = ac[11] * 0.01f;
        telemetry["p_c_kw"]     = ac[12] * 0.01f;
        telemetry["q_a_kvar"]   = ac[13] * 0.01f;
        telemetry["q_b_kvar"]   = ac[14] * 0.01f;
        telemetry["q_c_kvar"]   = ac[15] * 0.01f;
        telemetry["pf_a"]       = ac[19] * 0.01f;
        telemetry["pf_b"]       = ac[20] * 0.01f;
        telemetry["pf_c"]       = ac[21] * 0.01f;
        telemetry["p_inv_kw"]   = ac[22] * 0.01f;
        telemetry["q_inv_kvar"] = ac[23] * 0.01f;
        telemetry["pf_total"]   = ac[25] * 0.01f;
    } else Serial.println("[Modbus] Error: reg 100-125 (AC)");

    int16_t dc[REG_DC_COUNT];
    if (readRegisters(REG_DC_START, REG_DC_COUNT, dc)) {
        telemetry["dc_power_kw"]  = dc[0] * 0.01f;
        telemetry["dc_voltage_v"] = dc[1] * 0.1f;
        telemetry["dc_current_a"] = dc[2] * 0.1f;
    } else Serial.println("[Modbus] Error: reg 141-143 (DC)");

    int16_t grid[REG_GRID_COUNT];
    if (readRegisters(REG_GRID_START, REG_GRID_COUNT, grid)) {
        telemetry["grid_freq_hz"] = grid[0] * 0.01f;
        telemetry["grid_v_a"]     = grid[7] * 0.1f;
        telemetry["grid_v_b"]     = grid[8] * 0.1f;
        telemetry["grid_v_c"]     = grid[9] * 0.1f;
    } else Serial.println("[Modbus] Error: reg 170-179 (grid)");

    int16_t gridPwr[1];
    if (readRegisters(REG_GRID_POWER, 1, gridPwr))
        telemetry["grid_p_kw"] = gridPwr[0] * 0.01f;
    else Serial.println("[Modbus] Error: reg 192 (grid power)");

    int16_t load[REG_LOAD_COUNT];
    if (readRegisters(REG_LOAD_START, REG_LOAD_COUNT, load)) {
        telemetry["load_freq_hz"] = load[0]  * 0.01f;
        telemetry["load_i_a"]     = load[1]  * 0.1f;
        telemetry["load_i_b"]     = load[2]  * 0.1f;
        telemetry["load_i_c"]     = load[3]  * 0.1f;
        telemetry["load_v_a"]     = load[4]  * 0.1f;
        telemetry["load_v_b"]     = load[5]  * 0.1f;
        telemetry["load_v_c"]     = load[6]  * 0.1f;
        telemetry["load_p_a_kw"]  = load[7]  * 0.01f;
        telemetry["load_p_b_kw"]  = load[8]  * 0.01f;
        telemetry["load_p_c_kw"]  = load[9]  * 0.01f;
        telemetry["load_p_kw"]    = load[13] * 0.01f;
        telemetry["load_s_kva"]   = load[14] * 0.01f;
    } else Serial.println("[Modbus] Error: reg 200-213 (load V3.0)");
}
