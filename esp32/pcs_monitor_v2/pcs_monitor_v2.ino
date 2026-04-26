// =============================================================================
// PCS Inverter + BMS Monitor — ESP32  (solo monitoreo, sin RPC)
// 
// Modbus RTU RS-485 → inversor SinoSoar SP6030 (protocolo V3.0)
// CAN bus (TWAI)    → BMS (stub, reemplazar con protocolo real)
// WiFi MQTT         → ThingsBoard thingsboard.cloud
//
// Librerías (Arduino Library Manager):
//   - PubSubClient  (Nick O'Leary)
//   - ArduinoJson   (Benoit Blanchon)
//
// Conexiones MAX485:
//   GPIO17 (TX2) → DI
//   GPIO16 (RX2) → RO
//   GPIO4        → DE+RE
//
// Conexiones SN65HVD230 (CAN):
//   GPIO21 (TWAI TX) → D
//   GPIO22 (TWAI RX) → R
//   Rs → GND
// =============================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "driver/twai.h"

// ---------------------------------------------------------------------------
// CONFIGURACIÓN
// ---------------------------------------------------------------------------
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASSWORD    "TU_PASSWORD"
#define TB_HOST          "thingsboard.cloud"
#define TB_PORT          1883
#define TB_ACCESS_TOKEN  "TU_ACCESS_TOKEN"

// RS-485 / Modbus
#define RS485_BAUD       115200
#define RS485_SERIAL     Serial2
#define RS485_TX_PIN     17
#define RS485_RX_PIN     16
#define RS485_DE_RE_PIN  4
#define MODBUS_DEVICE_ID 1

// CAN (TWAI)
#define CAN_TX_PIN       GPIO_NUM_21
#define CAN_RX_PIN       GPIO_NUM_22
#define CAN_SPEED        TWAI_TIMING_CONFIG_250KBITS()  // ajustar según BMS

// Intervalos de polling
#define POLL_MODBUS_MS   5000    // inversor cada 5 segundos
#define POLL_CAN_MS      1000    // BMS cada 1 segundo (CAN es más rápido)
#define PUBLISH_MS       10000   // publicar a ThingsBoard cada 10 segundos
#define VERIFY_INIT_MS   60000   // verificar configuración del inversor cada 60 segundos

// ---------------------------------------------------------------------------
// REGISTROS MODBUS — SP6030 protocolo V3.0
// ---------------------------------------------------------------------------

// Secuencia de inicialización (writes al arranque)
// Ref: captura del EMS del fabricante
#define REG_DC_MAX_DISCHG_CURRENT  763   // ×0.1 A → escribir 1500 = 150.0 A
#define REG_DC_MAX_CHG_CURRENT     764   // ×0.1 A → escribir 1500 = 150.0 A
#define REG_3PHASE_CTRL_MODE       341   // 0=total trifásico, 1=por fase individual
#define REG_PV_SWITCH              652   // 1=on, 0=off
#define REG_ANTI_BACKFLOW          873   // bit0=1 → anti-backflow habilitado
#define REG_LEAKAGE_DETECT         795   // 0=deshabilitado
#define REG_DCDC_SWITCH            656   // 0=off

// Lectura — Estado general
#define REG_STATUS                  32
#define REG_STATUS_COUNT             1

// Lectura — AC inversor (reg 100–125, 26 registros)
#define REG_AC_START               100
#define REG_AC_COUNT                26

// Lectura — DC (reg 141–143, 3 registros)
#define REG_DC_START               141
#define REG_DC_COUNT                 3

// Lectura — Red (reg 170–192)
// Leer 170–179 (10 regs) y 192 por separado (están en el mismo segmento 100-299)
#define REG_GRID_START             170
#define REG_GRID_COUNT              10   // 170..179
#define REG_GRID_POWER             192   // potencia activa total red (1 reg)

// Lectura — Lado carga V3.0 (reg 200–213, 14 registros)
#define REG_LOAD_START             200
#define REG_LOAD_COUNT              14   // 200..213

// Versión del firmware (reg 0–21)
#define REG_VERSION_START            0
#define REG_VERSION_COUNT           22   // 0..21

// ---------------------------------------------------------------------------
// VARIABLES GLOBALES
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastModbusMs  = 0;
unsigned long lastCanMs     = 0;
unsigned long lastPublishMs = 0;
unsigned long lastVerifyMs  = 0;

// Último snapshot de telemetría (se actualiza con cada poll)
StaticJsonDocument<768> telemetry;

// ---------------------------------------------------------------------------
// CRC-16 Modbus
// ---------------------------------------------------------------------------
uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Modbus FC03 — leer registros
// ---------------------------------------------------------------------------
bool readRegisters(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    frame[0] = MODBUS_DEVICE_ID;
    frame[1] = 0x03;
    frame[2] = startReg >> 8;  frame[3] = startReg & 0xFF;
    frame[4] = count    >> 8;  frame[5] = count    & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[256];
    uint32_t t = millis();
    uint8_t  idx = 0;
    while ((millis() - t) < 300) {
        if (RS485_SERIAL.available() && idx < sizeof(rxBuf))
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx < 5) return false;
    if (crc16(rxBuf, idx - 2) != ((uint16_t)rxBuf[idx-1] << 8 | rxBuf[idx-2])) return false;
    if (rxBuf[1] & 0x80) return false;
    if (rxBuf[2] != count * 2) return false;

    for (uint16_t i = 0; i < count; i++)
        out[i] = (int16_t)(((uint16_t)rxBuf[3 + i*2] << 8) | rxBuf[4 + i*2]);
    return true;
}

// ---------------------------------------------------------------------------
// Modbus FC06 — escribir un registro
// ---------------------------------------------------------------------------
bool writeRegister(uint16_t reg, int16_t value) {
    uint8_t frame[8];
    frame[0] = MODBUS_DEVICE_ID;  frame[1] = 0x06;
    frame[2] = reg   >> 8;  frame[3] = reg   & 0xFF;
    frame[4] = (uint16_t)value >> 8;  frame[5] = (uint16_t)value & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;  frame[7] = c >> 8;

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[8];
    uint32_t t = millis();
    uint8_t  idx = 0;
    while ((millis() - t) < 300) {
        if (RS485_SERIAL.available() && idx < 8)
            rxBuf[idx++] = RS485_SERIAL.read();
    }
    if (idx < 8) return false;
    if (crc16(rxBuf, 6) != ((uint16_t)rxBuf[7] << 8 | rxBuf[6])) return false;
    if (rxBuf[1] & 0x80) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Leer y loguear versión del firmware del inversor
// Se llama una sola vez en setup(), antes del init.
// Los valores también se publican como atributos en ThingsBoard.
// ---------------------------------------------------------------------------
void readFirmwareVersion() {
    Serial.println("[Version] Leyendo versión del firmware...");

    int16_t v[REG_VERSION_COUNT];
    if (!readRegisters(REG_VERSION_START, REG_VERSION_COUNT, v)) {
        Serial.println("[Version] Error leyendo registros 0-21");
        return;
    }

    // reg 1: model code
    Serial.printf("[Version] Model code:         %d\n", v[1]);

    // reg 10-11: DSP bootloader version (uint32, big-endian entre registros)
    uint32_t dsp_bl  = ((uint32_t)(uint16_t)v[10] << 16) | (uint16_t)v[11];
    Serial.printf("[Version] DSP BL version:     %08X\n", dsp_bl);

    // reg 12-13: hardware version (uint32)
    uint32_t hw_ver  = ((uint32_t)(uint16_t)v[12] << 16) | (uint16_t)v[13];
    Serial.printf("[Version] Hardware version:   %08X\n", hw_ver);

    // reg 14-15: DSP firmware version (uint32)
    uint32_t dsp_fw  = ((uint32_t)(uint16_t)v[14] << 16) | (uint16_t)v[15];
    Serial.printf("[Version] DSP FW version:     %08X\n", dsp_fw);

    // reg 17-18: COM software version (uint32)
    uint32_t com_fw  = ((uint32_t)(uint16_t)v[17] << 16) | (uint16_t)v[18];
    Serial.printf("[Version] COM SW version:     %08X\n", com_fw);

    // reg 19: RTU protocol version — clave para saber si es V3.0
    uint16_t rtu_ver = (uint16_t)v[19];
    Serial.printf("[Version] RTU protocol ver:   %d\n", rtu_ver);

    if (rtu_ver >= 30) {
        Serial.println("[Version] ✓ Protocolo V3.0+ — registros 200-213 (load side) disponibles");
    } else {
        Serial.printf("[Version] ⚠ Protocolo V%d — registros 200-213 pueden no estar disponibles\n", rtu_ver);
    }

    // reg 20-21: COM bootloader version (uint32)
    uint32_t com_bl  = ((uint32_t)(uint16_t)v[20] << 16) | (uint16_t)v[21];
    Serial.printf("[Version] COM BL version:     %08X\n", com_bl);

    // Publicar como atributos del dispositivo en ThingsBoard
    // (topic separado de telemetría, se guardan como atributos estáticos)
    StaticJsonDocument<256> attrs;
    attrs["fw_model"]       = v[1];
    attrs["fw_hw_version"]  = hw_ver;
    attrs["fw_dsp_version"] = dsp_fw;
    attrs["fw_com_version"] = com_fw;
    attrs["fw_rtu_protocol"]= rtu_ver;

    char payload[256];
    serializeJson(attrs, payload, sizeof(payload));
    mqttClient.publish("v1/devices/me/attributes", payload);
    Serial.println("[Version] Atributos publicados a ThingsBoard");
}

// ---------------------------------------------------------------------------
// Secuencia de inicialización del inversor
// (replicada del EMS del fabricante, captura Wireshark)
// ---------------------------------------------------------------------------
void inverterInit() {
    Serial.println("[Init] Configurando inversor...");

    struct { uint16_t reg; int16_t val; const char* name; } cmds[] = {
        { REG_DC_MAX_DISCHG_CURRENT, 1500, "Max DC discharge current = 150A" },
        { REG_DC_MAX_CHG_CURRENT,    1500, "Max DC charge current = 150A"    },
        { REG_3PHASE_CTRL_MODE,         1, "Control por fase individual"      },
        { REG_PV_SWITCH,                0, "PV OFF"                           },
        { REG_ANTI_BACKFLOW,            1, "Anti-backflow ON"                 },
        { REG_LEAKAGE_DETECT,           0, "Leakage detection OFF"            },
        { REG_DCDC_SWITCH,              0, "DCDC OFF"                         },
    };

    for (auto& cmd : cmds) {
        bool ok = writeRegister(cmd.reg, cmd.val);
        Serial.printf("[Init] reg %d (%s): %s\n", cmd.reg, cmd.name, ok ? "OK" : "FAIL");
        delay(100);  // pausa entre writes, como hace el EMS del fabricante
    }
    Serial.println("[Init] Listo.");
}

// ---------------------------------------------------------------------------
// Verificar configuración del inversor y corregir si cambió
// Lee los registros de config, compara con valores esperados,
// y solo escribe si hay diferencia (evita writes innecesarios).
// ---------------------------------------------------------------------------
void verifyAndReinit() {
    struct ConfigReg {
        uint16_t    reg;
        int16_t     expected;
        const char* name;
    };

    static const ConfigReg cfg[] = {
        { 763, 1500, "Max DC discharge current" },
        { 764, 1500, "Max DC charge current"    },
        { 341,    1, "3-phase ctrl mode"        },
        { 652,    0, "PV switch"                },
        { 873,    1, "Anti-backflow"            },
        { 795,    0, "Leakage detection"        },
        { 656,    0, "DCDC switch"              },
    };

    bool anyFixed = false;

    for (const auto& c : cfg) {
        int16_t cur;
        if (!readRegisters(c.reg, 1, &cur)) {
            Serial.printf("[Verify] Error leyendo reg %d (%s)\n", c.reg, c.name);
            continue;
        }
        if (cur != c.expected) {
            Serial.printf("[Verify] reg %d (%s): valor=%d esperado=%d → corrigiendo\n",
                          c.reg, c.name, cur, c.expected);
            bool ok = writeRegister(c.reg, c.expected);
            Serial.printf("[Verify] Corrección reg %d: %s\n", c.reg, ok ? "OK" : "FAIL");
            anyFixed = true;
            delay(100);
        }
    }

    if (!anyFixed)
        Serial.println("[Verify] Configuración OK, sin cambios");
    else
        Serial.println("[Verify] Se corrigieron parámetros — posible reinicio del inversor");
}

// ---------------------------------------------------------------------------
// Poll Modbus — leer todos los bloques del inversor
// ---------------------------------------------------------------------------
void pollModbus() {
    // — Estado general (reg 32) —
    int16_t status[1];
    if (readRegisters(REG_STATUS, REG_STATUS_COUNT, status)) {
        telemetry["fault"]     = (status[0] >> 0) & 1;
        telemetry["alarm"]     = (status[0] >> 1) & 1;
        telemetry["running"]   = (status[0] >> 2) & 1;
        telemetry["grid_tied"] = (status[0] >> 3) & 1;
        telemetry["off_grid"]  = (status[0] >> 4) & 1;
        telemetry["derating"]  = (status[0] >> 5) & 1;   // nuevo en V2.2.2
        telemetry["standby"]   = (status[0] >> 7) & 1;
    } else {
        Serial.println("[Modbus] Error: reg 32 (status)");
    }

    // — AC inversor (reg 100–125) —
    int16_t ac[REG_AC_COUNT];
    if (readRegisters(REG_AC_START, REG_AC_COUNT, ac)) {
        telemetry["freq_hz"]      = ac[0]  * 0.01f;   // 100
        telemetry["v_ab"]         = ac[1]  * 0.1f;    // 101
        telemetry["v_bc"]         = ac[2]  * 0.1f;    // 102
        telemetry["v_ca"]         = ac[3]  * 0.1f;    // 103
        telemetry["i_a"]          = ac[4]  * 0.1f;    // 104
        telemetry["i_b"]          = ac[5]  * 0.1f;    // 105
        telemetry["i_c"]          = ac[6]  * 0.1f;    // 106
        telemetry["v_a"]          = ac[7]  * 0.1f;    // 107
        telemetry["v_b"]          = ac[8]  * 0.1f;    // 108
        telemetry["v_c"]          = ac[9]  * 0.1f;    // 109
        telemetry["p_a_kw"]       = ac[10] * 0.01f;   // 110
        telemetry["p_b_kw"]       = ac[11] * 0.01f;   // 111
        telemetry["p_c_kw"]       = ac[12] * 0.01f;   // 112
        telemetry["q_a_kvar"]     = ac[13] * 0.01f;   // 113
        telemetry["q_b_kvar"]     = ac[14] * 0.01f;   // 114
        telemetry["q_c_kvar"]     = ac[15] * 0.01f;   // 115
        telemetry["pf_a"]         = ac[19] * 0.01f;   // 119
        telemetry["pf_b"]         = ac[20] * 0.01f;   // 120
        telemetry["pf_c"]         = ac[21] * 0.01f;   // 121
        telemetry["p_inv_kw"]     = ac[22] * 0.01f;   // 122 — potencia activa inversor
        telemetry["q_inv_kvar"]   = ac[23] * 0.01f;   // 123
        telemetry["pf_total"]     = ac[25] * 0.01f;   // 125
    } else {
        Serial.println("[Modbus] Error: reg 100-125 (AC)");
    }

    // — DC (reg 141–143) —
    int16_t dc[REG_DC_COUNT];
    if (readRegisters(REG_DC_START, REG_DC_COUNT, dc)) {
        telemetry["dc_power_kw"]  = dc[0] * 0.01f;   // 141
        telemetry["dc_voltage_v"] = dc[1] * 0.1f;    // 142
        telemetry["dc_current_a"] = dc[2] * 0.1f;    // 143
    } else {
        Serial.println("[Modbus] Error: reg 141-143 (DC)");
    }

    // — Red (reg 170–179) —
    int16_t grid[REG_GRID_COUNT];
    if (readRegisters(REG_GRID_START, REG_GRID_COUNT, grid)) {
        telemetry["grid_freq_hz"] = grid[0] * 0.01f;  // 170
        telemetry["grid_v_a"]     = grid[7] * 0.1f;   // 177
        telemetry["grid_v_b"]     = grid[8] * 0.1f;   // 178
        telemetry["grid_v_c"]     = grid[9] * 0.1f;   // 179
    } else {
        Serial.println("[Modbus] Error: reg 170-179 (grid)");
    }

    // — Potencia red (reg 192) — leer sola (mismo segmento, distinto bloque)
    int16_t gridPwr[1];
    if (readRegisters(REG_GRID_POWER, 1, gridPwr)) {
        telemetry["grid_p_kw"] = gridPwr[0] * 0.01f;  // 192
    } else {
        Serial.println("[Modbus] Error: reg 192 (grid power)");
    }

    // — Lado carga V3.0 (reg 200–213) —
    int16_t load[REG_LOAD_COUNT];
    if (readRegisters(REG_LOAD_START, REG_LOAD_COUNT, load)) {
        telemetry["load_freq_hz"]  = load[0]  * 0.01f;  // 200
        telemetry["load_i_a"]      = load[1]  * 0.1f;   // 201
        telemetry["load_i_b"]      = load[2]  * 0.1f;   // 202
        telemetry["load_i_c"]      = load[3]  * 0.1f;   // 203
        telemetry["load_v_a"]      = load[4]  * 0.1f;   // 204
        telemetry["load_v_b"]      = load[5]  * 0.1f;   // 205
        telemetry["load_v_c"]      = load[6]  * 0.1f;   // 206
        telemetry["load_p_a_kw"]   = load[7]  * 0.01f;  // 207
        telemetry["load_p_b_kw"]   = load[8]  * 0.01f;  // 208
        telemetry["load_p_c_kw"]   = load[9]  * 0.01f;  // 209
        telemetry["load_p_kw"]     = load[13] * 0.01f;  // 213 — CLAVE: potencia total carga
        telemetry["load_s_kva"]    = load[14] * 0.01f;  // 214
    } else {
        Serial.println("[Modbus] Error: reg 200-213 (load V3.0)");
    }
}

// ---------------------------------------------------------------------------
// CAN — BMS
// ---------------------------------------------------------------------------
// ============================================================
// STUB: reemplazar con el protocolo real del BMS cuando esté
// disponible. Actualmente solo lee tramas CAN y las loguea.
// Para Pylontech ver CANBusProtocolPylonhighvoltageV1.24.pdf
// ============================================================
void initCAN() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    twai_timing_config_t  t_config = CAN_SPEED;
    twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        Serial.println("[CAN] Error instalando driver");
        return;
    }
    if (twai_start() != ESP_OK) {
        Serial.println("[CAN] Error iniciando driver");
        return;
    }
    Serial.println("[CAN] Driver iniciado en modo LISTEN_ONLY");
}

void pollCAN() {
    twai_message_t msg;

    // Leer todos los mensajes disponibles en el buffer
    while (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        Serial.printf("[CAN] ID: 0x%03X len=%d data:", msg.identifier, msg.data_length_code);
        for (int i = 0; i < msg.data_length_code; i++)
            Serial.printf(" %02X", msg.data[i]);
        Serial.println();

        // ----------------------------------------------------------------
        // TODO: decodificar mensajes del BMS aquí.
        // Estructura típica Pylontech (ejemplo, verificar con protocolo):
        //   0x4210 → voltaje, corriente, SOC
        //   0x4220 → temperatura
        //   0x4230 → alarmas
        //
        // Ejemplo de cómo agregar cuando tengas el protocolo:
        //
        // if (msg.identifier == 0x4210) {
        //     int16_t voltage_raw = (msg.data[0] << 8) | msg.data[1];
        //     int16_t current_raw = (msg.data[2] << 8) | msg.data[3];
        //     uint8_t soc         = msg.data[4];
        //     telemetry["bms_voltage_v"]  = voltage_raw * 0.1f;
        //     telemetry["bms_current_a"]  = (int16_t)current_raw * 0.1f;
        //     telemetry["bms_soc_pct"]    = soc;
        // }
        // ----------------------------------------------------------------

        // Por ahora publicar ID y datos crudos para observación
        char key[24];
        char val[40];
        snprintf(key, sizeof(key), "can_0x%03X", msg.identifier);
        snprintf(val, sizeof(val), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                 msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        telemetry[key] = val;
    }
}

// ---------------------------------------------------------------------------
// WiFi y MQTT
// ---------------------------------------------------------------------------
void connectWiFi() {
    Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
    mqttClient.setServer(TB_HOST, TB_PORT);
    mqttClient.setBufferSize(1024);
    while (!mqttClient.connected()) {
        Serial.print("[MQTT] Conectando...");
        if (mqttClient.connect("ESP32_PCS_MON", TB_ACCESS_TOKEN, nullptr))
            Serial.println(" OK");
        else {
            Serial.printf(" fallo rc=%d, reintentando en 5s\n", mqttClient.state());
            delay(5000);
        }
    }
}

void publishTelemetry() {
    if (!mqttClient.connected()) connectMQTT();
    char payload[1024];
    serializeJson(telemetry, payload, sizeof(payload));
    bool ok = mqttClient.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Publish %s (%d bytes)\n", ok ? "OK" : "FAIL", strlen(payload));
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial.println("\n[Boot] PCS Monitor v2 arrancando...");

    // RS-485
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    // CAN
    initCAN();

    // WiFi + MQTT
    connectWiFi();
    connectMQTT();

    // Leer versión del firmware antes de cualquier otra cosa
    readFirmwareVersion();

    // Inicializar inversor con la secuencia del fabricante
    inverterInit();

    // Primera lectura inmediata
    pollModbus();
    pollCAN();
    publishTelemetry();

    lastModbusMs  = millis();
    lastCanMs     = millis();
    lastPublishMs = millis();
    lastVerifyMs  = millis();

    Serial.println("[Boot] Listo.");
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
    if (!mqttClient.connected()) connectMQTT();
    mqttClient.loop();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconectando...");
        connectWiFi();
    }

    unsigned long now = millis();

    if (now - lastVerifyMs >= VERIFY_INIT_MS) {
        lastVerifyMs = now;
        verifyAndReinit();
    }

    if (now - lastModbusMs >= POLL_MODBUS_MS) {
        lastModbusMs = now;
        pollModbus();
    }

    if (now - lastCanMs >= POLL_CAN_MS) {
        lastCanMs = now;
        pollCAN();
    }

    if (now - lastPublishMs >= PUBLISH_MS) {
        lastPublishMs = now;
        publishTelemetry();
    }
}
