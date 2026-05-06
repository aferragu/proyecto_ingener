// =============================================================================
// test_bms — tests for bms_core.cpp
// Calls bms_decode() directly with synthetic CAN frames.
// Scale factors verified against bms_scales.h constants.
// =============================================================================
#include "unity.h"
#include "bms_core.h"
#include "bms_scales.h"
#include <string.h>

#define ADDR 1

static BmsData bms;

void setUp(void)    { memset(&bms, 0, sizeof(bms)); }
void tearDown(void) {}

// ---------------------------------------------------------------------------
// 0x421 — ensemble info
// ---------------------------------------------------------------------------

void test_421_voltage(void) {
    // v_raw = 4960 → 4960 * 0.1 - 0 = 496.0 V
    uint8_t d[] = { 0x13, 0x60,  0x75, 0x30,  0x03, 0xE8,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 496.0f, bms.voltage_v);
}

void test_421_current_idle(void) {
    // i_raw = 0x7530 = 30000 → 30000 * 0.1 - 3000 = 0 A
    uint8_t d[] = { 0x13, 0x60,  0x75, 0x30,  0x03, 0xE8,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, bms.current_a);
}

void test_421_current_discharging(void) {
    // i_raw = 0x7D00 = 32000 → 32000 * 0.1 - 3000 = 200 A
    uint8_t d[] = { 0x13, 0x60,  0x7D, 0x00,  0x03, 0xE8,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, bms.current_a);
}

void test_421_current_charging(void) {
    // i_raw = 0x6D60 = 28000 → 28000 * 0.1 - 3000 = -200 A (charging)
    uint8_t d[] = { 0x13, 0x60,  0x6D, 0x60,  0x03, 0xE8,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -200.0f, bms.current_a);
}

void test_421_temperature_zero(void) {
    // t_raw = 0x03E8 = 1000 → 1000 * 0.1 - 100 = 0 °C
    uint8_t d[] = { 0x13, 0x60,  0x75, 0x30,  0x03, 0xE8,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, bms.temperature_c);
}

void test_421_temperature_25c(void) {
    // t_raw = 0x04F0 = 1264 → 1264 * 0.1 - 100 = 26.4 °C
    uint8_t d[] = { 0x00, 0x00,  0x75, 0x30,  0x04, 0xF0,  80, 98 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 26.4f, bms.temperature_c);
}

void test_421_soc_soh(void) {
    uint8_t d[] = { 0x13, 0x60,  0x75, 0x30,  0x03, 0xE8,  85, 97 };
    bms_decode(bms, ADDR, 0x421 + ADDR, d);
    TEST_ASSERT_EQUAL_UINT8(85, bms.soc_pct);
    TEST_ASSERT_EQUAL_UINT8(97, bms.soh_pct);
    TEST_ASSERT_TRUE(bms.valid);
}

// ---------------------------------------------------------------------------
// 0x422 — charge/discharge limits
// ---------------------------------------------------------------------------

void test_422_charge_cutoff_voltage(void) {
    // cv_raw = 0x1450 = 5200 → 5200 * 0.1 - 0 = 520.0 V
    uint8_t d[] = { 0x14, 0x50,  0x10, 0xE8,  0x7D, 0x00,  0x7D, 0x00 };
    bms_decode(bms, ADDR, 0x422 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 520.0f, bms.charge_cutoff_v);
}

void test_422_discharge_cutoff_voltage(void) {
    // dv_raw = 0x10E8 = 4328 → 4328 * 0.1 = 432.8 V
    uint8_t d[] = { 0x14, 0x50,  0x10, 0xE8,  0x7D, 0x00,  0x7D, 0x00 };
    bms_decode(bms, ADDR, 0x422 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 432.8f, bms.discharge_cutoff_v);
}

void test_422_max_charge_current(void) {
    // ci_raw = 0x7D00 = 32000 → 32000 * 0.1 - 3000 = 200 A
    uint8_t d[] = { 0x14, 0x50,  0x10, 0xE8,  0x7D, 0x00,  0x7D, 0x00 };
    bms_decode(bms, ADDR, 0x422 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, bms.max_charge_a);
}

void test_422_max_discharge_current(void) {
    // di_raw = 0x7D00 = 32000 → 200 A
    uint8_t d[] = { 0x14, 0x50,  0x10, 0xE8,  0x7D, 0x00,  0x7D, 0x00 };
    bms_decode(bms, ADDR, 0x422 + ADDR, d);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 200.0f, bms.max_discharge_a);
}

// ---------------------------------------------------------------------------
// 0x425 — status, fault, alarm, protection
// ---------------------------------------------------------------------------

void test_425_status_discharging(void) {
    // bits 2:0 = 2 → discharging
    uint8_t d[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x425 + ADDR, d);
    TEST_ASSERT_EQUAL_UINT8(2, bms.status);
    TEST_ASSERT_FALSE(bms.force_charge_req);
}

void test_425_force_charge_request(void) {
    // bit 3 = 1 → force charge; bits 2:0 = 1 → charging
    uint8_t d[] = { 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x425 + ADDR, d);
    TEST_ASSERT_TRUE(bms.force_charge_req);
    TEST_ASSERT_EQUAL_UINT8(1, bms.status);
}

void test_425_fault_byte(void) {
    uint8_t d[] = { 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x425 + ADDR, d);
    TEST_ASSERT_EQUAL_UINT8(0x05, bms.fault);
}

void test_425_alarm_word(void) {
    uint8_t d[] = { 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x425 + ADDR, d);
    TEST_ASSERT_EQUAL_HEX16(0x0104, bms.alarm);
}

void test_425_protection_word(void) {
    uint8_t d[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x425 + ADDR, d);
    TEST_ASSERT_EQUAL_HEX16(0x0003, bms.protection);
}

// ---------------------------------------------------------------------------
// 0x428 — forbidden flags and SOE
// ---------------------------------------------------------------------------

void test_428_charge_forbidden(void) {
    uint8_t d[] = { 0xAA, 0x00, 0x01, 60, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x428 + ADDR, d);
    TEST_ASSERT_TRUE(bms.charge_forbidden);
    TEST_ASSERT_FALSE(bms.discharge_forbidden);
    TEST_ASSERT_EQUAL_UINT8(60, bms.soe_pct);
}

void test_428_discharge_forbidden(void) {
    uint8_t d[] = { 0x00, 0xAA, 0x01, 20, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x428 + ADDR, d);
    TEST_ASSERT_FALSE(bms.charge_forbidden);
    TEST_ASSERT_TRUE(bms.discharge_forbidden);
}

void test_428_both_allowed(void) {
    uint8_t d[] = { 0x00, 0x00, 0x01, 75, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x428 + ADDR, d);
    TEST_ASSERT_FALSE(bms.charge_forbidden);
    TEST_ASSERT_FALSE(bms.discharge_forbidden);
    TEST_ASSERT_EQUAL_UINT8(75, bms.soe_pct);
}

void test_428_non_aa_not_forbidden(void) {
    // Any value other than 0xAA means allowed
    uint8_t d[] = { 0x55, 0x01, 0x00, 50, 0x00, 0x00, 0x00, 0x00 };
    bms_decode(bms, ADDR, 0x428 + ADDR, d);
    TEST_ASSERT_FALSE(bms.charge_forbidden);
    TEST_ASSERT_FALSE(bms.discharge_forbidden);
}

// ---------------------------------------------------------------------------
// Unknown CAN ID — bmsData must not change
// ---------------------------------------------------------------------------

void test_unknown_id_ignored(void) {
    bms.voltage_v = 500.0f;
    uint8_t d[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    bms_decode(bms, ADDR, 0x999, d);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, bms.voltage_v);
}

void test_wrong_addr_ignored(void) {
    bms.voltage_v = 500.0f;
    uint8_t d[] = { 0x13, 0x60, 0x75, 0x30, 0x03, 0xE8, 80, 98 };
    bms_decode(bms, ADDR, 0x421 + 2, d);  // addr=2, not our addr=1
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 500.0f, bms.voltage_v);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_421_voltage);
    RUN_TEST(test_421_current_idle);
    RUN_TEST(test_421_current_discharging);
    RUN_TEST(test_421_current_charging);
    RUN_TEST(test_421_temperature_zero);
    RUN_TEST(test_421_temperature_25c);
    RUN_TEST(test_421_soc_soh);

    RUN_TEST(test_422_charge_cutoff_voltage);
    RUN_TEST(test_422_discharge_cutoff_voltage);
    RUN_TEST(test_422_max_charge_current);
    RUN_TEST(test_422_max_discharge_current);

    RUN_TEST(test_425_status_discharging);
    RUN_TEST(test_425_force_charge_request);
    RUN_TEST(test_425_fault_byte);
    RUN_TEST(test_425_alarm_word);
    RUN_TEST(test_425_protection_word);

    RUN_TEST(test_428_charge_forbidden);
    RUN_TEST(test_428_discharge_forbidden);
    RUN_TEST(test_428_both_allowed);
    RUN_TEST(test_428_non_aa_not_forbidden);

    RUN_TEST(test_unknown_id_ignored);
    RUN_TEST(test_wrong_addr_ignored);

    return UNITY_END();
}
