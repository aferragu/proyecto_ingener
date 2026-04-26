#include "bms.h"
#include "config.h"
#include "driver/twai.h"

void initCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t  t_config = CAN_SPEED;
    twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] Error instalando driver"); return;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] Error iniciando driver"); return;
    }
    Serial.println("[CAN] Driver iniciado en modo LISTEN_ONLY");
}

void pollCAN(JsonDocument& telemetry) {
    // STUB: reemplazar con protocolo real del BMS
    // Para Pylontech ver CANBusProtocolPylonhighvoltageV1.24.pdf
    // BMS_CAN_ADDR define la dirección (embebida en el CAN ID)
    //
    // Ejemplo Pylontech:
    // if (msg.identifier == (0x4210 + BMS_CAN_ADDR)) {
    //     telemetry["bms_voltage_v"] = ((msg.data[0]<<8)|msg.data[1]) * 0.1f;
    //     telemetry["bms_current_a"] = (int16_t)((msg.data[2]<<8)|msg.data[3]) * 0.1f;
    //     telemetry["bms_soc_pct"]   = msg.data[4];
    // }

    twai_message_t msg;
    while (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        Serial.printf("[CAN] ID: 0x%03X len=%d data:", msg.identifier, msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++)
            Serial.printf(" %02X", msg.data[i]);
        Serial.println();
        // publicar trama cruda para observación
        char key[24], val[40];
        snprintf(key, sizeof(key), "can_0x%03X", msg.identifier);
        snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                 msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        telemetry[key] = val;
    }
}
