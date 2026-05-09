#include "bms_core.h"
#include "bms_scales.h"

// Register offsets from BMS_REG_START (0x1000)
#define OFF_VOLTAGE       0x00  // 0x1000 UINT16
#define OFF_CURRENT       0x01  // 0x1001 INT16
#define OFF_TEMP_AVG      0x03  // 0x1003 INT16
#define OFF_WARNING       0x05  // 0x1005 HEX
#define OFF_PROTECTION    0x06  // 0x1006 HEX
#define OFF_FAULT_STATUS  0x07  // 0x1007 HEX
#define OFF_SOC           0x08  // 0x1008 UINT16
#define OFF_SOH           0x09  // 0x1009 UINT16
#define OFF_CELL_V_MAX    0x0D  // 0x100D UINT16
#define OFF_CELL_V_MIN    0x0E  // 0x100E UINT16
#define OFF_TEMP_MAX      0x10  // 0x1010 INT16
#define OFF_TEMP_MIN      0x11  // 0x1011 INT16
#define OFF_TEMP_FET      0x12  // 0x1012 INT16

void bms_parse_modbus(const int16_t* r, BmsData& bms,
                      int16_t chg_cutoff_raw, int16_t dischg_cutoff_raw,
                      uint16_t max_chg_raw, uint16_t max_dischg_raw) {

    // Electrical
    bms.voltage_v          = (uint16_t)r[OFF_VOLTAGE]    * BMS_SCALE_VOLTAGE_V;
    bms.current_a          = r[OFF_CURRENT]               * BMS_SCALE_CURRENT_A;
    bms.charge_cutoff_v    = (uint16_t)chg_cutoff_raw     * BMS_SCALE_CUTOFF_V;
    bms.discharge_cutoff_v = (uint16_t)dischg_cutoff_raw  * BMS_SCALE_CUTOFF_V;
    bms.max_charge_a       = max_chg_raw                  * BMS_SCALE_MAX_CURRENT_A;
    bms.max_discharge_a    = max_dischg_raw               * BMS_SCALE_MAX_CURRENT_A;

    // State
    bms.soc_pct = (uint16_t)r[OFF_SOC] * BMS_SCALE_SOC_PCT;
    bms.soh_pct = (uint16_t)r[OFF_SOH] * BMS_SCALE_SOC_PCT;

    // Temperature
    bms.temp_avg_c      = r[OFF_TEMP_AVG]  * BMS_SCALE_TEMP_C;
    bms.temp_cell_max_c = r[OFF_TEMP_MAX]  * BMS_SCALE_TEMP_C;
    bms.temp_cell_min_c = r[OFF_TEMP_MIN]  * BMS_SCALE_TEMP_C;
    bms.temp_fet_c      = r[OFF_TEMP_FET]  * BMS_SCALE_TEMP_C;

    // Cell voltages
    bms.cell_voltage_max_v = (uint16_t)r[OFF_CELL_V_MAX] * BMS_SCALE_CELL_VOLTAGE_V;
    bms.cell_voltage_min_v = (uint16_t)r[OFF_CELL_V_MIN] * BMS_SCALE_CELL_VOLTAGE_V;

    // Status flags — 0x1007: byte0=fault, byte1=status
    uint8_t fault_byte  = (uint16_t)r[OFF_FAULT_STATUS] >> 8;
    uint8_t status_byte = (uint16_t)r[OFF_FAULT_STATUS] & 0xFF;
    bms.fault              = fault_byte;
    bms.charging           = (status_byte >> BMS_STATUS_CHARGING_BIT)    & 0x01;
    bms.discharging        = (status_byte >> BMS_STATUS_DISCHARGING_BIT) & 0x01;
    bms.charge_forbidden   = !((status_byte >> BMS_STATUS_CHG_MOS_BIT)   & 0x01);
    bms.discharge_forbidden= !((status_byte >> BMS_STATUS_DISCHG_MOS_BIT)& 0x01);

    // Alarm — 0x1005: byte0=alarm_low, byte1=alarm_high
    bms.alarm = (uint16_t)r[OFF_WARNING];
    uint8_t alarm_high = (bms.alarm >> 8) & 0xFF;
    bms.force_charge_req = (alarm_high >> BMS_ALARM_LOW_SOC_BIT) & 0x01;

    // Protection — 0x1006
    bms.protection = (uint16_t)r[OFF_PROTECTION];

    bms.valid = true;
}
