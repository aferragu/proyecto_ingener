// =============================================================================
// test_modbus_hw — Hardware sketch: Modbus RTU RS-485 → Serial debug
//
// Polls the SinoSoar SP6030 inverter via Modbus RTU every 5s and prints
// all register blocks to Serial with scaled physical values.
// No WiFi, no ThingsBoard, no display — pure Serial diagnostics.
//
// Wiring:
//   MAX485: GPIO17→DI, GPIO16→RO, GPIO5→DE+RE, A/B→inverter RS-485
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "modbus_core.h"
#include "inverter_core.h"
#include "inverter_scales.h"

#define POLL_INTERVAL_MS 5000

// ---------------------------------------------------------------------------
// Modbus read — uses modbus_core from lib/
// ---------------------------------------------------------------------------
bool modbusRead(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    modbus_build_read(frame, MODBUS_DEVICE_ID, startReg, count);

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[256];
    uint32_t t = millis();
    uint8_t idx = 0;
    while ((millis() - t) < 50 && idx == 0)
        if (RS485_SERIAL.available()) rxBuf[idx++] = RS485_SERIAL.read();
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20)
        if (RS485_SERIAL.available() && idx < (uint16_t)sizeof(rxBuf)) rxBuf[idx++] = RS485_SERIAL.read();

    return modbus_parse_read(rxBuf, idx, count, out);
}

// ---------------------------------------------------------------------------
// Poll and print
// ---------------------------------------------------------------------------
void pollAndPrint() {
    int16_t raw[26];
    Serial.println("\n========================================");
    Serial.printf("Poll @ %lums\n", millis());
    Serial.println("========================================");

    if (modbusRead(REG_STATUS, REG_STATUS_COUNT, raw)) {
        StatusData s; inverter_parse_status(raw, s);
        Serial.println("\n[Status]");
        Serial.printf("  running   = %d\n", s.running);
        Serial.printf("  grid_tied = %d\n", s.grid_tied);
        Serial.printf("  off_grid  = %d\n", s.off_grid);
        Serial.printf("  standby   = %d\n", s.standby);
        Serial.printf("  derating  = %d\n", s.derating);
        Serial.printf("  alarm     = %d\n", s.alarm);
        Serial.printf("  fault     = %d\n", s.fault);
        Serial.printf("  raw       = 0x%04X\n", (uint16_t)raw[0]);
    } else Serial.println("\n[Status] FAIL — no response");

    if (modbusRead(REG_AC_START, REG_AC_COUNT, raw)) {
        AcData ac; inverter_parse_ac(raw, ac);
        Serial.println("\n[AC Inverter]");
        Serial.printf("  freq_hz   = %.2f Hz\n",   ac.freq_hz);
        Serial.printf("  v_ab      = %.1f V\n",    ac.v_ab);
        Serial.printf("  v_bc      = %.1f V\n",    ac.v_bc);
        Serial.printf("  v_ca      = %.1f V\n",    ac.v_ca);
        Serial.printf("  v_a       = %.1f V\n",    ac.v_a);
        Serial.printf("  v_b       = %.1f V\n",    ac.v_b);
        Serial.printf("  v_c       = %.1f V\n",    ac.v_c);
        Serial.printf("  i_a       = %.1f A\n",    ac.i_a);
        Serial.printf("  i_b       = %.1f A\n",    ac.i_b);
        Serial.printf("  i_c       = %.1f A\n",    ac.i_c);
        Serial.printf("  p_a_kw    = %.2f kW\n",   ac.p_a);
        Serial.printf("  p_b_kw    = %.2f kW\n",   ac.p_b);
        Serial.printf("  p_c_kw    = %.2f kW\n",   ac.p_c);
        Serial.printf("  p_inv_kw  = %.2f kW\n",   ac.p_inv);
        Serial.printf("  q_inv     = %.2f kVAR\n", ac.q_inv);
        Serial.printf("  pf_total  = %.2f\n",      ac.pf_total);
    } else Serial.println("\n[AC] FAIL — no response");

    if (modbusRead(REG_DC_START, REG_DC_COUNT, raw)) {
        DcData dc; inverter_parse_dc(raw, dc);
        Serial.println("\n[DC]");
        Serial.printf("  dc_power_kw  = %.2f kW\n", dc.power_kw);
        Serial.printf("  dc_voltage_v = %.1f V\n",  dc.voltage_v);
        Serial.printf("  dc_current_a = %.1f A\n",  dc.current_a);
    } else Serial.println("\n[DC] FAIL — no response");

    int16_t grid_p_raw = 0;
    if (modbusRead(REG_GRID_START, REG_GRID_COUNT, raw) &&
        modbusRead(REG_GRID_POWER, 1, &grid_p_raw)) {
        GridData g; inverter_parse_grid(raw, grid_p_raw, g);
        Serial.println("\n[Grid]");
        Serial.printf("  grid_freq_hz = %.2f Hz\n", g.freq_hz);
        Serial.printf("  grid_v_a     = %.1f V\n",  g.v_a);
        Serial.printf("  grid_v_b     = %.1f V\n",  g.v_b);
        Serial.printf("  grid_v_c     = %.1f V\n",  g.v_c);
        Serial.printf("  grid_p_kw    = %.2f kW\n", g.p_kw);
    } else Serial.println("\n[Grid] FAIL — no response");

    if (modbusRead(REG_LOAD_START, REG_LOAD_COUNT, raw)) {
        LoadData l; inverter_parse_load(raw, l);
        Serial.println("\n[Load]");
        Serial.printf("  load_freq_hz = %.2f Hz\n",  l.freq_hz);
        Serial.printf("  load_v_a     = %.1f V\n",   l.v_a);
        Serial.printf("  load_v_b     = %.1f V\n",   l.v_b);
        Serial.printf("  load_v_c     = %.1f V\n",   l.v_c);
        Serial.printf("  load_i_a     = %.1f A\n",   l.i_a);
        Serial.printf("  load_i_b     = %.1f A\n",   l.i_b);
        Serial.printf("  load_i_c     = %.1f A\n",   l.i_c);
        Serial.printf("  load_p_a_kw  = %.2f kW\n",  l.p_a);
        Serial.printf("  load_p_b_kw  = %.2f kW\n",  l.p_b);
        Serial.printf("  load_p_c_kw  = %.2f kW\n",  l.p_c);
        Serial.printf("  load_p_kw    = %.2f kW\n",  l.p_total);
        Serial.printf("  load_s_kva   = %.2f kVA\n", l.s_total);
    } else Serial.println("\n[Load] FAIL — no response (requires firmware RTU >= V3.0)");

    Serial.println("\n[Raw] Registers 0–9 (firmware info):");
    if (modbusRead(REG_VERSION_START, 10, raw)) {
        for (int i = 0; i < 10; i++)
            Serial.printf("  reg %2d = %6d  (0x%04X)\n", i, raw[i], (uint16_t)raw[i]);
    } else Serial.println("  FAIL");
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_modbus_hw starting...");
    Serial.printf("[Boot] Device ID: %d  Baud: %d  DE/RE: GPIO%d\n",
                  MODBUS_DEVICE_ID, RS485_BAUD, RS485_DE_RE_PIN);

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    Serial.println("[Boot] RS-485 ready — polling every 5s");
    Serial.println("[Boot] If all blocks FAIL, check:");
    Serial.println("         - RS-485 A/B polarity (swap if needed)");
    Serial.printf("         - MODBUS_DEVICE_ID (%d) matches inverter DIP switch\n", MODBUS_DEVICE_ID);
    Serial.printf("         - DE/RE wired to GPIO%d\n", RS485_DE_RE_PIN);
}

void loop() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollAndPrint();
    }
}
