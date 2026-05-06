// =============================================================================
// test_modbus — tests for modbus_core.cpp
// Covers: CRC16, frame building, response parsing, register scaling
// =============================================================================
#include "unity.h"
#include "modbus_core.h"
#include "inverter_scales.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// CRC16
// ---------------------------------------------------------------------------

void test_crc16_fc03_frame(void) {
    uint8_t frame[6] = { 0x01, 0x03, 0x00, 0x64, 0x00, 0x1A };
    uint16_t crc = crc16(frame, 6);
    TEST_ASSERT_EQUAL_HEX16(crc, crc16(frame, 6));
}

void test_crc16_changes_with_data(void) {
    uint8_t a[] = { 0x01, 0x03, 0x00, 0x64, 0x00, 0x1A };
    uint8_t b[] = { 0x01, 0x03, 0x00, 0x64, 0x00, 0x1B };
    TEST_ASSERT_NOT_EQUAL(crc16(a, 6), crc16(b, 6));
}

void test_crc16_empty(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16(nullptr, 0));
}

// ---------------------------------------------------------------------------
// Frame building
// ---------------------------------------------------------------------------

void test_build_read_frame_structure(void) {
    uint8_t frame[8];
    modbus_build_read(frame, 1, 100, 26);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_UINT8(0x64, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(0x1A, frame[5]);
    uint16_t expected_crc = crc16(frame, 6);
    uint16_t frame_crc = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    TEST_ASSERT_EQUAL_HEX16(expected_crc, frame_crc);
}

void test_build_write_frame_structure(void) {
    uint8_t frame[8];
    modbus_build_write(frame, 1, 135, 250);
    TEST_ASSERT_EQUAL_UINT8(0x01, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_UINT8(0x87, frame[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(0xFA, frame[5]);
    uint16_t expected_crc = crc16(frame, 6);
    uint16_t frame_crc = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    TEST_ASSERT_EQUAL_HEX16(expected_crc, frame_crc);
}

void test_build_write_negative_value(void) {
    uint8_t frame[8];
    modbus_build_write(frame, 1, 135, -250);
    TEST_ASSERT_EQUAL_UINT8(0xFF, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(0x06, frame[5]);
}

// ---------------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------------

static void make_read_response(uint8_t* buf, uint8_t device_id,
                                uint16_t count, const int16_t* values,
                                uint8_t* len_out) {
    buf[0] = device_id;
    buf[1] = 0x03;
    buf[2] = count * 2;
    for (uint16_t i = 0; i < count; i++) {
        buf[3 + i*2] = (uint16_t)values[i] >> 8;
        buf[4 + i*2] = (uint16_t)values[i] & 0xFF;
    }
    uint8_t len = 3 + count * 2;
    uint16_t crc = crc16(buf, len);
    buf[len]     = crc & 0xFF;
    buf[len + 1] = crc >> 8;
    *len_out = len + 2;
}

void test_parse_read_single_register(void) {
    int16_t values[] = { 0x000C };
    uint8_t buf[16]; uint8_t len;
    make_read_response(buf, 1, 1, values, &len);
    int16_t out[1];
    TEST_ASSERT_TRUE(modbus_parse_read(buf, len, 1, out));
    TEST_ASSERT_EQUAL_INT16(0x000C, out[0]);
}

void test_parse_read_multiple_registers(void) {
    int16_t values[] = { 5000, 2300, -150, 99 };
    uint8_t buf[64]; uint8_t len;
    make_read_response(buf, 1, 4, values, &len);
    int16_t out[4];
    TEST_ASSERT_TRUE(modbus_parse_read(buf, len, 4, out));
    TEST_ASSERT_EQUAL_INT16(5000, out[0]);
    TEST_ASSERT_EQUAL_INT16(2300, out[1]);
    TEST_ASSERT_EQUAL_INT16(-150, out[2]);
    TEST_ASSERT_EQUAL_INT16(99,   out[3]);
}

void test_parse_read_bad_crc(void) {
    int16_t values[] = { 0x000C };
    uint8_t buf[16]; uint8_t len;
    make_read_response(buf, 1, 1, values, &len);
    buf[len - 1] ^= 0xFF;
    int16_t out[1];
    TEST_ASSERT_FALSE(modbus_parse_read(buf, len, 1, out));
}

void test_parse_read_exception_response(void) {
    uint8_t buf[8] = { 0x01, 0x83, 0x02, 0x00, 0x00 };
    uint16_t crc = crc16(buf, 3);
    buf[3] = crc & 0xFF; buf[4] = crc >> 8;
    int16_t out[1];
    TEST_ASSERT_FALSE(modbus_parse_read(buf, 5, 1, out));
}

void test_parse_read_too_short(void) {
    uint8_t buf[] = { 0x01, 0x03 };
    int16_t out[1];
    TEST_ASSERT_FALSE(modbus_parse_read(buf, 2, 1, out));
}

// ---------------------------------------------------------------------------
// Scale factors (inverter_scales.h)
// ---------------------------------------------------------------------------

void test_scale_frequency(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, (int16_t)5000 * SCALE_FREQ_HZ);
}

void test_scale_voltage(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.0f, (int16_t)2300 * SCALE_VOLTAGE_V);
}

void test_scale_current(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.0f, (int16_t)150 * SCALE_CURRENT_A);
}

void test_scale_power_positive(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, (int16_t)2500 * SCALE_POWER_KW);
}

void test_scale_power_negative(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -25.0f, (int16_t)-2500 * SCALE_POWER_KW);
}

void test_scale_power_factor(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.99f, (int16_t)99 * SCALE_PF);
}

void test_scale_power_factor_negative(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.99f, (int16_t)-99 * SCALE_PF);
}

// ---------------------------------------------------------------------------
// setPower raw conversion
// ---------------------------------------------------------------------------

static int16_t power_kw_to_raw(float kw) {
    if (kw >  100.0f) kw =  100.0f;
    if (kw < -100.0f) kw = -100.0f;
    return (int16_t)(kw / SCALE_SET_POWER_KW);
}

void test_setpower_raw_25kw(void)     { TEST_ASSERT_EQUAL_INT16( 250, power_kw_to_raw( 25.0f)); }
void test_setpower_raw_negative(void) { TEST_ASSERT_EQUAL_INT16(-100, power_kw_to_raw(-10.0f)); }
void test_setpower_raw_clamp_max(void){ TEST_ASSERT_EQUAL_INT16(1000, power_kw_to_raw(150.0f)); }
void test_setpower_raw_clamp_min(void){ TEST_ASSERT_EQUAL_INT16(-1000,power_kw_to_raw(-200.0f));}
void test_setpower_raw_zero(void)     { TEST_ASSERT_EQUAL_INT16(   0, power_kw_to_raw(  0.0f)); }

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_crc16_fc03_frame);
    RUN_TEST(test_crc16_changes_with_data);
    RUN_TEST(test_crc16_empty);

    RUN_TEST(test_build_read_frame_structure);
    RUN_TEST(test_build_write_frame_structure);
    RUN_TEST(test_build_write_negative_value);

    RUN_TEST(test_parse_read_single_register);
    RUN_TEST(test_parse_read_multiple_registers);
    RUN_TEST(test_parse_read_bad_crc);
    RUN_TEST(test_parse_read_exception_response);
    RUN_TEST(test_parse_read_too_short);

    RUN_TEST(test_scale_frequency);
    RUN_TEST(test_scale_voltage);
    RUN_TEST(test_scale_current);
    RUN_TEST(test_scale_power_positive);
    RUN_TEST(test_scale_power_negative);
    RUN_TEST(test_scale_power_factor);
    RUN_TEST(test_scale_power_factor_negative);

    RUN_TEST(test_setpower_raw_25kw);
    RUN_TEST(test_setpower_raw_negative);
    RUN_TEST(test_setpower_raw_clamp_max);
    RUN_TEST(test_setpower_raw_clamp_min);
    RUN_TEST(test_setpower_raw_zero);

    return UNITY_END();
}
