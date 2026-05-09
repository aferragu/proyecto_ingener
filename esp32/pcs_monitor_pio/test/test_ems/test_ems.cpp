// =============================================================================
// test_ems — tests for EMS control logic
//
// ems.cpp calls writeRegister() which is hardware. Rather than linking
// the full hardware stack, we test the decision logic in isolation by
// extracting it into a local emsDecide() that takes a write function pointer.
// This mirrors what ems.cpp does — if the logic changes there, update here.
// =============================================================================
#include "unity.h"
#include "bms_parser.h"
#include "inverter_scales.h"
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Mock writeRegister
// ---------------------------------------------------------------------------
static uint16_t last_reg;
static int16_t  last_value;
static int      write_count;

static bool mock_write(uint16_t reg, int16_t value) {
    last_reg   = reg;
    last_value = value;
    write_count++;
    return true;
}

void setUp(void)    { last_reg = 0; last_value = 0; write_count = 0; }
void tearDown(void) {}

// ---------------------------------------------------------------------------
// EMS parameters (mirror of ems.cpp statics)
// ---------------------------------------------------------------------------
#define REG_SET_POWER       135
#define EMS_SOC_MIN         20.0f
#define EMS_SOC_TARGET      90.0f
#define EMS_MAX_DISCHARGE   20.0f
#define EMS_CHARGE_POWER    10.0f

// ---------------------------------------------------------------------------
// Decision function — mirrors ems.cpp logic exactly
// Returns chosen setpoint in kW, calls write_fn for the register write.
// ---------------------------------------------------------------------------
typedef bool (*WriteFn)(uint16_t, int16_t);

static float emsDecide(const BmsData& bms, float load_p_kw,
                        const char** state, WriteFn write_fn) {
    if (!bms.valid) { *state = "NO_BMS"; return 0.0f; }

    if (bms.charge_forbidden && bms.discharge_forbidden) {
        write_fn(REG_SET_POWER, 0);
        *state = "BMS_FAULT"; return 0.0f;
    }

    if (bms.fault != 0 || bms.protection != 0) {
        write_fn(REG_SET_POWER, 0);
        *state = "BMS_PROTECTION"; return 0.0f;
    }

    float max_disch_kw = EMS_MAX_DISCHARGE;
    float bms_limit_kw = bms.max_discharge_a * bms.voltage_v / 1000.0f;
    if (bms_limit_kw < max_disch_kw) max_disch_kw = bms_limit_kw;

    float setPwr = 0.0f;
    *state = "IDLE";

    if (bms.soc_pct > EMS_SOC_MIN && !bms.discharge_forbidden) {
        setPwr = load_p_kw < max_disch_kw ? load_p_kw : max_disch_kw;
        *state = "DISCHARGING";
    }

    if (bms.soc_pct < EMS_SOC_TARGET && load_p_kw < 5.0f && !bms.charge_forbidden) {
        setPwr = -EMS_CHARGE_POWER;
        *state = "CHARGING";
    }

    if (setPwr >  100.0f) setPwr =  100.0f;
    if (setPwr < -100.0f) setPwr = -100.0f;

    write_fn(REG_SET_POWER, (int16_t)(setPwr / SCALE_SET_POWER_KW));
    return setPwr;
}

// Helper
static BmsData make_bms(uint8_t soc, float v = 500.0f, float max_disch_a = 200.0f) {
    BmsData b = {};
    b.valid             = true;
    b.soc_pct           = soc;
    b.voltage_v         = v;
    b.max_discharge_a   = max_disch_a;
    b.max_charge_a      = 100.0f;
    return b;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_no_bms_data_no_write(void) {
    BmsData b = {}; b.valid = false;
    const char* state;
    float pwr = emsDecide(b, 10.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pwr);
    TEST_ASSERT_EQUAL_STRING("NO_BMS", state);
    TEST_ASSERT_EQUAL_INT(0, write_count);
}

void test_both_forbidden_writes_zero(void) {
    BmsData b = make_bms(50);
    b.charge_forbidden = b.discharge_forbidden = true;
    const char* state;
    emsDecide(b, 20.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("BMS_FAULT", state);
    TEST_ASSERT_EQUAL_INT16(0, last_value);
    TEST_ASSERT_EQUAL_UINT16(REG_SET_POWER, last_reg);
}

void test_bms_fault_writes_zero(void) {
    BmsData b = make_bms(70); b.fault = 0x03;
    const char* state;
    emsDecide(b, 15.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("BMS_PROTECTION", state);
    TEST_ASSERT_EQUAL_INT16(0, last_value);
}

void test_bms_protection_writes_zero(void) {
    BmsData b = make_bms(70); b.protection = 0x0001;
    const char* state;
    emsDecide(b, 15.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("BMS_PROTECTION", state);
    TEST_ASSERT_EQUAL_INT16(0, last_value);
}

void test_discharge_normal(void) {
    // SOC=80%, load=15kW → discharge 15kW → raw=150
    const char* state;
    float pwr = emsDecide(make_bms(80), 15.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("DISCHARGING", state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, pwr);
    TEST_ASSERT_EQUAL_INT16(150, last_value);
}

void test_discharge_capped_by_ems_limit(void) {
    // load=30kW > EMS_MAX_DISCHARGE=20kW → cap at 20kW → raw=200
    const char* state;
    float pwr = emsDecide(make_bms(80), 30.0f, &state, mock_write);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, pwr);
    TEST_ASSERT_EQUAL_INT16(200, last_value);
}

void test_discharge_capped_by_bms_limit(void) {
    // BMS allows 10A × 500V = 5kW, load=15kW → cap at 5kW → raw=50
    const char* state;
    float pwr = emsDecide(make_bms(80, 500.0f, 10.0f), 15.0f, &state, mock_write);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, pwr);
    TEST_ASSERT_EQUAL_INT16(50, last_value);
}

void test_soc_below_min_idle(void) {
    // SOC=15% < EMS_SOC_MIN=20% — no discharge; load=20kW not < 5 — no charge
    const char* state;
    float pwr = emsDecide(make_bms(15), 20.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("IDLE", state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, pwr);
    TEST_ASSERT_EQUAL_INT16(0, last_value);
}

void test_charge_from_grid_low_soc_low_load(void) {
    // SOC=10% < target, load=2kW < 5kW → charge at -10kW → raw=-100
    const char* state;
    float pwr = emsDecide(make_bms(10), 2.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("CHARGING", state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -10.0f, pwr);
    TEST_ASSERT_EQUAL_INT16(-100, last_value);
}

void test_no_charge_when_charge_forbidden(void) {
    BmsData b = make_bms(10); b.charge_forbidden = true;
    const char* state;
    emsDecide(b, 2.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("IDLE", state);
}

void test_no_charge_when_load_high(void) {
    // load=20kW >= 5kW threshold → no charging even with low SOC
    const char* state;
    emsDecide(make_bms(10), 20.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("IDLE", state);
}

void test_discharge_forbidden_forces_idle(void) {
    BmsData b = make_bms(80); b.discharge_forbidden = true;
    const char* state;
    float pwr = emsDecide(b, 15.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_STRING("IDLE", state);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, pwr);
}

void test_exactly_one_write_per_call(void) {
    const char* state;
    emsDecide(make_bms(80), 10.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_INT(1, write_count);
}

void test_write_uses_correct_register(void) {
    const char* state;
    emsDecide(make_bms(80), 10.0f, &state, mock_write);
    TEST_ASSERT_EQUAL_UINT16(REG_SET_POWER, last_reg);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_no_bms_data_no_write);
    RUN_TEST(test_both_forbidden_writes_zero);
    RUN_TEST(test_bms_fault_writes_zero);
    RUN_TEST(test_bms_protection_writes_zero);
    RUN_TEST(test_discharge_normal);
    RUN_TEST(test_discharge_capped_by_ems_limit);
    RUN_TEST(test_discharge_capped_by_bms_limit);
    RUN_TEST(test_soc_below_min_idle);
    RUN_TEST(test_charge_from_grid_low_soc_low_load);
    RUN_TEST(test_no_charge_when_charge_forbidden);
    RUN_TEST(test_no_charge_when_load_high);
    RUN_TEST(test_discharge_forbidden_forces_idle);
    RUN_TEST(test_exactly_one_write_per_call);
    RUN_TEST(test_write_uses_correct_register);

    return UNITY_END();
}
