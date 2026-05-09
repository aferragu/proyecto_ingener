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
#include <ModbusMaster.h>
#include "config.h"
#include "inverter_parser.h"
#include "inverter_scales.h"

#define POLL_INTERVAL_MS 5000

static ModbusMaster node;

static void preTransmission()  { digitalWrite(RS485_DE_RE_PIN, HIGH); }
static void postTransmission() { digitalWrite(RS485_DE_RE_PIN, LOW);  }

static const char* modbusErrorStr(uint8_t code) {
    switch (code) {
        case 0x01: return "illegal function";
        case 0x02: return "illegal data address";
        case 0x03: return "illegal data value";
        case 0x04: return "slave device failure";
        case 0xE0: return "invalid slave ID";
        case 0xE1: return "invalid function";
        case 0xE2: return "response timed out";
        case 0xE3: return "invalid CRC";
        default:   return "unknown error";
    }
}

static bool inv_read(uint16_t reg, uint16_t count, int16_t* out) {
    uint8_t r = node.readHoldingRegisters(reg, count);
    if (r != ModbusMaster::ku8MBSuccess) {
        Serial.printf("  [FAIL 0x%02X — %s]\n", r, modbusErrorStr(r));
        return false;
    }
    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)node.getResponseBuffer(i);
    return true;
}

static bool inv_write(uint16_t reg, int16_t value) {
    uint8_t r = node.writeSingleRegister(reg, (uint16_t)value);
    return r == ModbusMaster::ku8MBSuccess;
}

void pollAndPrint() {
    int16_t raw[26];
    Serial.println("\n========================================");
    Serial.printf("Poll @ %lums\n", millis());
    Serial.println("========================================");

    if (inv_read(REG_STATUS, REG_STATUS_COUNT, raw)) {
        StatusData s; inverter_parse_status(raw, s);
        Serial.println("\n[Status]");
        Serial.printf("  running=%d  grid_tied=%d  off_grid=%d  standby=%d\n",
                      s.running, s.grid_tied, s.off_grid, s.standby);
        Serial.printf("  derating=%d  alarm=%d  fault=%d  raw=0x%04X\n",
                      s.derating, s.alarm, s.fault, (uint16_t)raw[0]);
    } else Serial.println("\n[Status] FAIL — reg 32");

    if (inv_read(REG_AC_START, REG_AC_COUNT, raw)) {
        AcData ac; inverter_parse_ac(raw, ac);
        Serial.println("\n[AC]");
        Serial.printf("  freq=%.2fHz  v_a=%.1fV  v_ab=%.1fV\n", ac.freq_hz, ac.v_a, ac.v_ab);
        Serial.printf("  i_a=%.1fA  p_inv=%.2fkW  pf=%.2f\n", ac.i_a, ac.p_inv, ac.pf_total);
    } else Serial.println("\n[AC] FAIL — regs 100-125");

    if (inv_read(REG_DC_START, REG_DC_COUNT, raw)) {
        DcData dc; inverter_parse_dc(raw, dc);
        Serial.println("\n[DC]");
        Serial.printf("  v=%.1fV  i=%.1fA  p=%.2fkW\n", dc.voltage_v, dc.current_a, dc.power_kw);
    } else Serial.println("\n[DC] FAIL — regs 141-143");

    int16_t grid_p_raw = 0;
    if (inv_read(REG_GRID_START, REG_GRID_COUNT, raw) &&
        inv_read(REG_GRID_POWER, 1, &grid_p_raw)) {
        GridData g; inverter_parse_grid(raw, grid_p_raw, g);
        Serial.println("\n[Grid]");
        Serial.printf("  freq=%.2fHz  v_a=%.1fV  p=%.2fkW\n", g.freq_hz, g.v_a, g.p_kw);
    } else Serial.println("\n[Grid] FAIL — regs 170-179/192");

    if (inv_read(REG_LOAD_START, REG_LOAD_COUNT, raw)) {
        LoadData l; inverter_parse_load(raw, l);
        Serial.println("\n[Load]");
        Serial.printf("  p=%.2fkW  s=%.2fkVA\n", l.p_total, l.s_total);
    } else Serial.println("\n[Load] FAIL — regs 200-213 (requires RTU V3.0+)");

    Serial.println("\n[Version regs 0-9]");
    if (inv_read(REG_VERSION_START, 10, raw))
        for (int i = 0; i < 10; i++)
            Serial.printf("  reg %2d = %6d (0x%04X)\n", i, raw[i], (uint16_t)raw[i]);
    else Serial.println("  FAIL — regs 0-9");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_modbus_hw starting...");
    Serial.printf("[Boot] Device ID: %d  Baud: %d  DE/RE: GPIO%d\n",
                  MODBUS_DEVICE_ID, RS485_BAUD, RS485_DE_RE_PIN);

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    node.begin(MODBUS_DEVICE_ID, Serial2);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);

    Serial.println("[Boot] Running inverter init...");
    bool ok = inverter_run_init(inv_write, inv_read);
    Serial.printf("[Boot] Init %s\n", ok ? "OK" : "WARNING: some registers failed");
    Serial.println("[Boot] Polling every 5s");
}

void loop() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollAndPrint();
    }
}
