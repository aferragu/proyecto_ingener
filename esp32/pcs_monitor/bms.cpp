#include "bms.h"
#include "config.h"
#include "driver/twai.h"

BmsData bmsData = {};

// ---------------------------------------------------------------------------
// Helpers CAN TX
// ---------------------------------------------------------------------------
static bool canSend(uint32_t id, uint8_t b0, uint8_t b1 = 0,
                    uint8_t b2 = 0, uint8_t b3 = 0,
                    uint8_t b4 = 0, uint8_t b5 = 0,
                    uint8_t b6 = 0, uint8_t b7 = 0) {
    twai_message_t msg = {};
    msg.identifier      = id;
    msg.extd            = 0;    // 11-bit identifier
    msg.data_length_code = 8;
    msg.data[0] = b0; msg.data[1] = b1; msg.data[2] = b2; msg.data[3] = b3;
    msg.data[4] = b4; msg.data[5] = b5; msg.data[6] = b6; msg.data[7] = b7;
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        Serial.printf("[CAN] TX error 0x%03X: %d\n", id, err);
    return err == ESP_OK;
}

// ---------------------------------------------------------------------------
// Init CAN driver
// ---------------------------------------------------------------------------
void initCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t  t_config = CAN_SPEED;
    twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] Error instalando driver"); return;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] Error iniciando driver"); return;
    }
    Serial.println("[CAN] Driver iniciado");
}

// ---------------------------------------------------------------------------
// Init BMS — wake up y habilitar carga/descarga
// Pylontech high voltage CAN protocol V1.24
// ---------------------------------------------------------------------------
void initBMS() {
    Serial.println("[BMS] Inicializando...");

    // 1. Wake up — CAN ID 0x620
    // Byte0: 0xAA = salir de sleep
    bool ok = canSend(0x620, 0xAA);
    Serial.printf("[BMS] Wake up: %s\n", ok ? "OK" : "FAIL");
    delay(200);  // dar tiempo al BMS para despertar

    // 2. Habilitar carga y descarga — CAN ID 0x621
    // Byte0=0xAA: enable charge, Byte1=0xAA: enable discharge
    ok = canSend(0x621, 0xAA, 0xAA);
    Serial.printf("[BMS] Enable charge/discharge: %s\n", ok ? "OK" : "FAIL");
    delay(100);

    // 3. Solicitar información general — broadcast CAN ID 0x420
    // Byte0=0: ensemble information request
    ok = canSend(0x420, 0x00);
    Serial.printf("[BMS] Query ensemble info: %s\n", ok ? "OK" : "FAIL");

    Serial.println("[BMS] Init completo");
}

// ---------------------------------------------------------------------------
// Decodificar mensajes CAN del BMS
// Pylontech usa 11-bit IDs: 0x421+Addr para respuestas del BMS
// BMS_CAN_ADDR está en config.h
// ---------------------------------------------------------------------------
static void decodeBmsMessage(twai_message_t& msg) {
    uint32_t id = msg.identifier;
    uint8_t* d  = msg.data;

    if (id == (0x421 + BMS_CAN_ADDR)) {
        // Ensemble info: voltaje, corriente, temperatura, SOC, SOH
        uint16_t v_raw  = ((uint16_t)d[0] << 8) | d[1];
        uint16_t i_raw  = ((uint16_t)d[2] << 8) | d[3];
        uint16_t t_raw  = ((uint16_t)d[4] << 8) | d[5];
        bmsData.voltage_v     = v_raw * 0.1f;
        bmsData.current_a     = i_raw * 0.1f - 3000.0f;  // offset -3000A
        bmsData.temperature_c = t_raw * 0.1f - 100.0f;   // offset -100°C
        bmsData.soc_pct       = d[6];
        bmsData.soh_pct       = d[7];
        bmsData.valid         = true;

    } else if (id == (0x422 + BMS_CAN_ADDR)) {
        // Charge/discharge limits
        uint16_t cv_raw  = ((uint16_t)d[0] << 8) | d[1];
        uint16_t dv_raw  = ((uint16_t)d[2] << 8) | d[3];
        uint16_t ci_raw  = ((uint16_t)d[4] << 8) | d[5];
        uint16_t di_raw  = ((uint16_t)d[6] << 8) | d[7];
        bmsData.charge_cutoff_v    = cv_raw * 0.1f;
        bmsData.discharge_cutoff_v = dv_raw * 0.1f;
        bmsData.max_charge_a       = ci_raw * 0.1f - 3000.0f;
        bmsData.max_discharge_a    = di_raw * 0.1f - 3000.0f;

    } else if (id == (0x425 + BMS_CAN_ADDR)) {
        // Status, fault, alarm, protection
        bmsData.status          = d[0] & 0x07;  // bits 2:0
        bmsData.force_charge_req= (d[0] >> 3) & 0x01;
        bmsData.fault           = d[1];
        bmsData.alarm           = ((uint16_t)d[2] << 8) | d[3];
        bmsData.protection      = ((uint16_t)d[4] << 8) | d[5];

    } else if (id == (0x428 + BMS_CAN_ADDR)) {
        // Charge/discharge forbidden flags + SOE
        bmsData.charge_forbidden    = (d[0] == 0xAA);
        bmsData.discharge_forbidden = (d[1] == 0xAA);
        bmsData.soe_pct             = d[3];
    }
}

// ---------------------------------------------------------------------------
// Poll CAN — leer mensajes disponibles y actualizar bmsData
// Se llama periódicamente desde el loop
// ---------------------------------------------------------------------------
void pollCAN(JsonDocument& telemetry) {
    twai_message_t msg;
    int count = 0;

    while (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        decodeBmsMessage(msg);
        count++;
    }

    if (!bmsData.valid) return;

    // Publicar a ThingsBoard
    telemetry["bms_voltage_v"]       = bmsData.voltage_v;
    telemetry["bms_current_a"]       = bmsData.current_a;
    telemetry["bms_temperature_c"]   = bmsData.temperature_c;
    telemetry["bms_soc_pct"]         = bmsData.soc_pct;
    telemetry["bms_soh_pct"]         = bmsData.soh_pct;
    telemetry["bms_soe_pct"]         = bmsData.soe_pct;
    telemetry["bms_max_charge_a"]    = bmsData.max_charge_a;
    telemetry["bms_max_discharge_a"] = bmsData.max_discharge_a;
    telemetry["bms_status"]          = bmsData.status;
    telemetry["bms_fault"]           = bmsData.fault;
    telemetry["bms_alarm"]           = bmsData.alarm;
    telemetry["bms_protection"]      = bmsData.protection;
    telemetry["bms_charge_forbidden"]    = bmsData.charge_forbidden    ? 1 : 0;
    telemetry["bms_discharge_forbidden"] = bmsData.discharge_forbidden ? 1 : 0;
    telemetry["bms_force_charge"]        = bmsData.force_charge_req    ? 1 : 0;

    // Log periódico de estado
    if (count > 0)
        Serial.printf("[BMS] SOC=%d%% V=%.1fV I=%.1fA T=%.1f°C fault=%d alarm=%d\n",
                      bmsData.soc_pct, bmsData.voltage_v, bmsData.current_a,
                      bmsData.temperature_c, bmsData.fault, bmsData.alarm);
}
