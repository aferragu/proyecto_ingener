// =============================================================================
// test_can_hw — OBSOLETO — diagnóstico CAN bus (Pylontech protocol)
//
// NOTA: el proyecto migró el BMS a Modbus RTU (LWS protocol V1.36).
//       Este sketch queda como referencia histórica pero NO compila
//       porque bms_decode() fue eliminado al eliminar el soporte CAN.
//
// Si necesitás volver a CAN, restaurar bms_parser con la función bms_decode()
// del commit anterior al refactor de Modbus.
//
// Wiring original (ya no en uso):
//   SN65HVD230 o similar: GPIO21→TX, GPIO22→RX
//   CAN_H / CAN_L → BMS Pylontech high voltage
// =============================================================================


#include <Arduino.h>
#include "config.h"
#include "bms_parser.h"
#include "bms_scales.h"
#include "driver/twai.h"

// How many raw frames to print before switching to decoded-only output
#define RAW_FRAMES_LIMIT 50

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
BmsData  bmsData   = {};
uint32_t frameCount = 0;

// ---------------------------------------------------------------------------
// Print raw frame
// ---------------------------------------------------------------------------
void printRaw(const twai_message_t& msg) {
    Serial.printf("[CAN] ID=0x%08X  ext=%d  dlc=%d  data=",
                  msg.identifier, msg.extd, msg.data_length_code);
    for (int i = 0; i < msg.data_length_code; i++)
        Serial.printf("%02X ", msg.data[i]);
    Serial.println();
}

// ---------------------------------------------------------------------------
// Print decoded BMS data
// ---------------------------------------------------------------------------
void printDecoded() {
    if (!bmsData.valid) return;
    Serial.println("\n[BMS Decoded]");
    Serial.printf("  voltage_v        = %.1f V\n",  bmsData.voltage_v);
    Serial.printf("  current_a        = %.1f A\n",  bmsData.current_a);
    Serial.printf("  temperature_c    = %.1f °C\n", bmsData.temperature_c);
    Serial.printf("  soc_pct          = %d %%\n",   bmsData.soc_pct);
    Serial.printf("  soh_pct          = %d %%\n",   bmsData.soh_pct);
    Serial.printf("  soe_pct          = %d %%\n",   bmsData.soe_pct);
    Serial.printf("  max_charge_a     = %.1f A\n",  bmsData.max_charge_a);
    Serial.printf("  max_discharge_a  = %.1f A\n",  bmsData.max_discharge_a);
    Serial.printf("  charge_cutoff_v  = %.1f V\n",  bmsData.charge_cutoff_v);
    Serial.printf("  discharge_cutoff = %.1f V\n",  bmsData.discharge_cutoff_v);
    Serial.printf("  status           = %d\n",      bmsData.status);
    Serial.printf("  fault            = 0x%02X\n",  bmsData.fault);
    Serial.printf("  alarm            = 0x%04X\n",  bmsData.alarm);
    Serial.printf("  protection       = 0x%04X\n",  bmsData.protection);
    Serial.printf("  charge_forbidden = %d\n",      bmsData.charge_forbidden);
    Serial.printf("  disch_forbidden  = %d\n",      bmsData.discharge_forbidden);
    Serial.printf("  force_charge_req = %d\n",      bmsData.force_charge_req);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_can_hw starting...");
    Serial.printf("[Boot] BMS_ADDR=%d  Speed=500kbps  TX=GPIO%d  RX=GPIO%d\n",
                  BMS_CAN_ADDR, CAN_TX_PIN, CAN_RX_PIN);

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = CAN_SPEED;
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        Serial.println("[Boot] FAIL: twai_driver_install"); return;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("[Boot] FAIL: twai_start"); return;
    }

    Serial.println("[Boot] CAN ready (normal mode — ESP32 ACKs BMS frames)");
    Serial.println("[Boot] Waiting for frames...");
    Serial.printf("[Boot] Expecting BMS IDs: 0x%04X, 0x%04X, 0x%04X, 0x%04X\n",
                  0x4210 + BMS_CAN_ADDR, 0x4220 + BMS_CAN_ADDR,
                  0x4250 + BMS_CAN_ADDR, 0x4280 + BMS_CAN_ADDR);
    Serial.println("[Boot] If nothing arrives, check CAN_H/CAN_L wiring and termination.");
}

void loop() {
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(100)) != ESP_OK) return;

    frameCount++;

    if (frameCount <= RAW_FRAMES_LIMIT || frameCount % 100 == 0)
        printRaw(msg);

    bool wasValid = bmsData.valid;
    bms_decode(bmsData, BMS_CAN_ADDR, msg.identifier, msg.data);

    if (msg.identifier == (uint32_t)(0x4210 + BMS_CAN_ADDR)) {
        Serial.printf("\n[BMS] frame #%u — SOC=%d%%  V=%.1fV  I=%.1fA  T=%.1f°C\n",
                      frameCount, bmsData.soc_pct, bmsData.voltage_v,
                      bmsData.current_a, bmsData.temperature_c);
    }

    if (!wasValid && bmsData.valid) {
        Serial.println("\n[BMS] First complete decode:");
        printDecoded();
    }
}
