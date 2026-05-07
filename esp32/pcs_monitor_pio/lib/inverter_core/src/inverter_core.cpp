#include "inverter_core.h"
#include "inverter_scales.h"

void inverter_parse_status(const int16_t* r, StatusData& o) {
    uint16_t s = (uint16_t)r[0];
    o.fault     = (s >> 0) & 1;
    o.alarm     = (s >> 1) & 1;
    o.running   = (s >> 2) & 1;
    o.grid_tied = (s >> 3) & 1;
    o.off_grid  = (s >> 4) & 1;
    o.derating  = (s >> 5) & 1;
    o.standby   = (s >> 7) & 1;
}

void inverter_parse_ac(const int16_t* r, AcData& o) {
    // reg 100–125, offsets relative to REG_AC_START (100)
    o.freq_hz  = r[0]  * SCALE_FREQ_HZ;
    o.v_ab     = r[1]  * SCALE_VOLTAGE_V;
    o.v_bc     = r[2]  * SCALE_VOLTAGE_V;
    o.v_ca     = r[3]  * SCALE_VOLTAGE_V;
    o.i_a      = r[4]  * SCALE_CURRENT_A;
    o.i_b      = r[5]  * SCALE_CURRENT_A;
    o.i_c      = r[6]  * SCALE_CURRENT_A;
    o.v_a      = r[7]  * SCALE_VOLTAGE_V;
    o.v_b      = r[8]  * SCALE_VOLTAGE_V;
    o.v_c      = r[9]  * SCALE_VOLTAGE_V;
    o.p_a      = r[10] * SCALE_POWER_KW;
    o.p_b      = r[11] * SCALE_POWER_KW;
    o.p_c      = r[12] * SCALE_POWER_KW;
    o.q_a      = r[13] * SCALE_REACTIVE_KVAR;
    o.q_b      = r[14] * SCALE_REACTIVE_KVAR;
    o.q_c      = r[15] * SCALE_REACTIVE_KVAR;
    // r[16]=apparent A, r[17]=apparent B, r[18]=apparent C (not published to TB)
    o.pf_a     = r[19] * SCALE_PF;
    o.pf_b     = r[20] * SCALE_PF;
    o.pf_c     = r[21] * SCALE_PF;
    o.p_inv    = r[22] * SCALE_POWER_KW;
    o.q_inv    = r[23] * SCALE_REACTIVE_KVAR;
    // r[24]=total apparent (not published to TB)
    o.pf_total = r[25] * SCALE_PF;
}

void inverter_parse_dc(const int16_t* r, DcData& o) {
    // reg 141–143, offsets relative to REG_DC_START (141)
    o.power_kw  = r[0] * SCALE_POWER_KW;
    o.voltage_v = r[1] * SCALE_VOLTAGE_V;
    o.current_a = r[2] * SCALE_CURRENT_A;
}

void inverter_parse_grid(const int16_t* r, int16_t grid_p_raw, GridData& o) {
    // reg 170–179, offsets relative to REG_GRID_START (170)
    // r[1-3]=line voltages (not in protocol V3 grid table, skipped)
    // r[4-6]=grid currents A/B/C
    // r[7-9]=grid phase voltages A/B/C
    o.freq_hz = r[0] * SCALE_FREQ_HZ;
    o.v_a     = r[7] * SCALE_VOLTAGE_V;
    o.v_b     = r[8] * SCALE_VOLTAGE_V;
    o.v_c     = r[9] * SCALE_VOLTAGE_V;
    // reg 192 read separately
    o.p_kw    = grid_p_raw * SCALE_POWER_KW;
}

void inverter_parse_load(const int16_t* r, LoadData& o) {
    // reg 200–213, offsets relative to REG_LOAD_START (200)
    o.freq_hz = r[0]  * SCALE_FREQ_HZ;
    o.i_a     = r[1]  * SCALE_CURRENT_A;
    o.i_b     = r[2]  * SCALE_CURRENT_A;
    o.i_c     = r[3]  * SCALE_CURRENT_A;
    o.v_a     = r[4]  * SCALE_VOLTAGE_V;
    o.v_b     = r[5]  * SCALE_VOLTAGE_V;
    o.v_c     = r[6]  * SCALE_VOLTAGE_V;
    o.p_a     = r[7]  * SCALE_POWER_KW;
    o.p_b     = r[8]  * SCALE_POWER_KW;
    o.p_c     = r[9]  * SCALE_POWER_KW;
    // r[10-12]=apparent power per phase (not published to TB)
    o.p_total = r[13] * SCALE_POWER_KW;
    o.s_total = r[14] * SCALE_APPARENT_KVA;
}

const InitCmd* inverter_init_sequence(uint8_t* count_out) {
    static const InitCmd seq[] = {
        { REG_DC_MAX_DISCHG_CURRENT, 1500, "Max DC discharge = 150A" },
        { REG_DC_MAX_CHG_CURRENT,    1500, "Max DC charge = 150A"    },
        { REG_3PHASE_CTRL_MODE,         1, "Control por fase"        },
        { REG_PV_SWITCH,                0, "PV OFF"                  },
        { REG_LEAKAGE_DETECT,           0, "Leakage OFF"             },
        { REG_DCDC_SWITCH,              0, "DCDC OFF"                },
    };
    *count_out = sizeof(seq) / sizeof(seq[0]);
    return seq;
}

void inverter_run_init(WriteRegFn write_fn, ReadRegFn read_fn) {
    uint8_t count;
    const InitCmd* seq = inverter_init_sequence(&count);
    for (uint8_t i = 0; i < count; i++) {
        write_fn(seq[i].reg, seq[i].val);
    }
    // reg 873: anti-backflow — read-modify-write
    int16_t cur873 = 0;
    read_fn(REG_ANTI_BACKFLOW, 1, &cur873);
    write_fn(REG_ANTI_BACKFLOW, cur873 | 0x01);
}
