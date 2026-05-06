#include "bms_core.h"
#include "bms_scales.h"

void bms_decode(BmsData& bms, uint8_t bms_addr,
                uint32_t can_id, const uint8_t* d) {

    if (can_id == (uint32_t)(0x421 + bms_addr)) {
        uint16_t v_raw = ((uint16_t)d[0] << 8) | d[1];
        uint16_t i_raw = ((uint16_t)d[2] << 8) | d[3];
        uint16_t t_raw = ((uint16_t)d[4] << 8) | d[5];
        bms.voltage_v     = v_raw * BMS_SCALE_VOLTAGE_V  - BMS_OFFSET_VOLTAGE_V;
        bms.current_a     = i_raw * BMS_SCALE_CURRENT_A  - BMS_OFFSET_CURRENT_A;
        bms.temperature_c = t_raw * BMS_SCALE_TEMP_C     - BMS_OFFSET_TEMP_C;
        bms.soc_pct       = d[6];
        bms.soh_pct       = d[7];
        bms.valid         = true;

    } else if (can_id == (uint32_t)(0x422 + bms_addr)) {
        uint16_t cv_raw = ((uint16_t)d[0] << 8) | d[1];
        uint16_t dv_raw = ((uint16_t)d[2] << 8) | d[3];
        uint16_t ci_raw = ((uint16_t)d[4] << 8) | d[5];
        uint16_t di_raw = ((uint16_t)d[6] << 8) | d[7];
        bms.charge_cutoff_v    = cv_raw * BMS_SCALE_CUTOFF_V      - BMS_OFFSET_CUTOFF_V;
        bms.discharge_cutoff_v = dv_raw * BMS_SCALE_CUTOFF_V      - BMS_OFFSET_CUTOFF_V;
        bms.max_charge_a       = ci_raw * BMS_SCALE_MAX_CURRENT_A - BMS_OFFSET_MAX_CURRENT_A;
        bms.max_discharge_a    = di_raw * BMS_SCALE_MAX_CURRENT_A - BMS_OFFSET_MAX_CURRENT_A;

    } else if (can_id == (uint32_t)(0x425 + bms_addr)) {
        bms.status           = d[0] & 0x07;
        bms.force_charge_req = (d[0] >> 3) & 0x01;
        bms.fault            = d[1];
        bms.alarm            = ((uint16_t)d[2] << 8) | d[3];
        bms.protection       = ((uint16_t)d[4] << 8) | d[5];

    } else if (can_id == (uint32_t)(0x428 + bms_addr)) {
        bms.charge_forbidden    = (d[0] == BMS_FORBIDDEN_MARK);
        bms.discharge_forbidden = (d[1] == BMS_FORBIDDEN_MARK);
        bms.soe_pct             = d[3];
    }
}
