// =============================================================================
// test_bms — tests for bms_parser.cpp (LWS Modbus protocol V1.36)
// Calls bms_parse_modbus() directly with synthetic register arrays.
// Scale factors verified against bms_scales.h constants.
// =============================================================================
#include "unity.h"
#include "bms_parser.h"
#include "bms_scales.h"
#include <string.h>
#include <stdint.h>

static BmsData bms;

// Build a zeroed register array of BMS_REG_COUNT elements
static int16_t regs[BMS_REG_COUNT];

void setUp(void) {
    memset(&bms, 0, sizeof(bms));
    memset(regs, 0, sizeof(regs));
}
void tearDown(void) {}

// Helper — call bms_parse_modbus with zero cutoffs and max currents
static void parse(int16_t chg_cutoff = 0, int16_t dischg_cutoff = 0,
                  uint16_t max_chg = 0, uint16_t max_dischg = 0) {
    bms_parse_modbus(regs, bms, chg_cutoff, dischg_cutoff, max_chg, max_dischg);
}

// ---------------------------------------------------------------------------
// Voltage — 0x1000, UINT16, 0.01 V
// ---------------------------------------------------------------------------
void test_voltage(void) {
    // 49600 raw → 49600 * 0.01 = 496.0 V
    regs[0x00] = (int16_t)49600;
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 496.0f, bms.voltage_v);
}

// ---------------------------------------------------------------------------
// Current — 0x1001, INT16, 0.01 A, discharge = negative
// ---------------------------------------------------------------------------
void test_current_charging(void) {
    // 4000 raw → 4000 * 0.01 = 40.0 A (positive = charging)
    regs[0x01] = 4000;
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, bms.current_a);
}

void test_current_discharging(void) {
    // -20000 raw → -200.0 A (discharging)
    regs[0x01] = -20000;
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -200.0f, bms.current_a);
}

void test_current_idle(void) {
    regs[0x01] = 0;
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, bms.current_a);
}

// ---------------------------------------------------------------------------
// SOC / SOH — 0x1008, 0x1009, UINT16, 0.1 %
// ---------------------------------------------------------------------------
void test_soc(void) {
    regs[0x08] = 750;   // 750 * 0.1 = 75.0 %
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 75.0f, bms.soc_pct);
}

void test_soh(void) {
    regs[0x09] = 980;   // 980 * 0.1 = 98.0 %
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 98.0f, bms.soh_pct);
}

// ---------------------------------------------------------------------------
// Temperature — INT16, 0.1 °C
// ---------------------------------------------------------------------------
void test_temp_avg(void) {
    regs[0x03] = 245;   // 24.5 °C
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.5f, bms.temp_avg_c);
}

void test_temp_cell_max(void) {
    regs[0x10] = 270;   // 27.0 °C
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27.0f, bms.temp_cell_max_c);
}

void test_temp_cell_min(void) {
    regs[0x11] = 220;   // 22.0 °C
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 22.0f, bms.temp_cell_min_c);
}

void test_temp_fet(void) {
    regs[0x12] = 310;   // 31.0 °C
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 31.0f, bms.temp_fet_c);
}

void test_temp_negative(void) {
    regs[0x03] = -50;   // -5.0 °C
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, bms.temp_avg_c);
}

// ---------------------------------------------------------------------------
// Cell voltages — UINT16, 0.001 V
// ---------------------------------------------------------------------------
void test_cell_voltage_max(void) {
    regs[0x0D] = 3310;  // 3.310 V
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.310f, bms.cell_voltage_max_v);
}

void test_cell_voltage_min(void) {
    regs[0x0E] = 3280;  // 3.280 V
    parse();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.280f, bms.cell_voltage_min_v);
}

// ---------------------------------------------------------------------------
// Cutoff voltages — passed as params, UINT16, 0.01 V
// ---------------------------------------------------------------------------
void test_charge_cutoff(void) {
    // 53760 * 0.01 = 537.6 V
    parse(53760, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 537.6f, bms.charge_cutoff_v);
}

void test_discharge_cutoff(void) {
    // 43200 * 0.01 = 432.0 V
    parse(0, 43200);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 432.0f, bms.discharge_cutoff_v);
}

// ---------------------------------------------------------------------------
// Max currents — passed as params, UINT16, 0.1 A
// ---------------------------------------------------------------------------
void test_max_charge_current(void) {
    // 2000 * 0.1 = 200.0 A
    parse(0, 0, 2000, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, bms.max_charge_a);
}

void test_max_discharge_current(void) {
    // 2000 * 0.1 = 200.0 A
    parse(0, 0, 0, 2000);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, bms.max_discharge_a);
}

// ---------------------------------------------------------------------------
// Status flags — 0x1007, byte0=fault, byte1=status
// High byte = fault, low byte = status (big-endian register)
// ---------------------------------------------------------------------------
void test_charging_status(void) {
    // byte1 bit0=1 → charging
    regs[0x07] = (int16_t)0x0001;
    parse();
    TEST_ASSERT_TRUE(bms.charging);
    TEST_ASSERT_FALSE(bms.discharging);
}

void test_discharging_status(void) {
    // byte1 bit1=1 → discharging
    regs[0x07] = (int16_t)0x0002;
    parse();
    TEST_ASSERT_FALSE(bms.charging);
    TEST_ASSERT_TRUE(bms.discharging);
}

void test_charge_mos_on(void) {
    // byte1 bit2=1 → charge MOSFET on → NOT forbidden
    regs[0x07] = (int16_t)0x0004;
    parse();
    TEST_ASSERT_FALSE(bms.charge_forbidden);
}

void test_charge_mos_off(void) {
    // byte1 bit2=0 → charge MOSFET off → forbidden
    regs[0x07] = 0x0000;
    parse();
    TEST_ASSERT_TRUE(bms.charge_forbidden);
}

void test_discharge_mos_on(void) {
    // byte1 bit3=1 → discharge MOSFET on → NOT forbidden
    regs[0x07] = (int16_t)0x0008;
    parse();
    TEST_ASSERT_FALSE(bms.discharge_forbidden);
}

void test_fault_byte(void) {
    // high byte = 0x05
    regs[0x07] = (int16_t)0x0500;
    parse();
    TEST_ASSERT_EQUAL_UINT8(0x05, bms.fault);
}

// ---------------------------------------------------------------------------
// Alarm / force charge — 0x1005, byte1 bit3 = low SOC alarm
// ---------------------------------------------------------------------------
void test_force_charge_req(void) {
    // byte1 bit3 = 1 → low SOC alarm → force_charge_req
    regs[0x05] = (int16_t)0x0800;
    parse();
    TEST_ASSERT_TRUE(bms.force_charge_req);
}

void test_no_force_charge(void) {
    regs[0x05] = 0x0000;
    parse();
    TEST_ASSERT_FALSE(bms.force_charge_req);
}

// ---------------------------------------------------------------------------
// Protection — 0x1006
// ---------------------------------------------------------------------------
void test_protection_word(void) {
    regs[0x06] = (int16_t)0x0003;
    parse();
    TEST_ASSERT_EQUAL_HEX16(0x0003, bms.protection);
}

// ---------------------------------------------------------------------------
// valid flag
// ---------------------------------------------------------------------------
void test_valid_set_after_parse(void) {
    parse();
    TEST_ASSERT_TRUE(bms.valid);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_voltage);
    RUN_TEST(test_current_charging);
    RUN_TEST(test_current_discharging);
    RUN_TEST(test_current_idle);
    RUN_TEST(test_soc);
    RUN_TEST(test_soh);
    RUN_TEST(test_temp_avg);
    RUN_TEST(test_temp_cell_max);
    RUN_TEST(test_temp_cell_min);
    RUN_TEST(test_temp_fet);
    RUN_TEST(test_temp_negative);
    RUN_TEST(test_cell_voltage_max);
    RUN_TEST(test_cell_voltage_min);
    RUN_TEST(test_charge_cutoff);
    RUN_TEST(test_discharge_cutoff);
    RUN_TEST(test_max_charge_current);
    RUN_TEST(test_max_discharge_current);
    RUN_TEST(test_charging_status);
    RUN_TEST(test_discharging_status);
    RUN_TEST(test_charge_mos_on);
    RUN_TEST(test_charge_mos_off);
    RUN_TEST(test_discharge_mos_on);
    RUN_TEST(test_fault_byte);
    RUN_TEST(test_force_charge_req);
    RUN_TEST(test_no_force_charge);
    RUN_TEST(test_protection_word);
    RUN_TEST(test_valid_set_after_parse);

    return UNITY_END();
}
