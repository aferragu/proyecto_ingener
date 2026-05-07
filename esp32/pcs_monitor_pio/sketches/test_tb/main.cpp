// =============================================================================
// test_tb — Simulated telemetry → ThingsBoard
//
// Purpose: build and arrange the ThingsBoard dashboard without needing
// the inverter or BMS connected. Sends all keys the real firmware sends,
// with realistic values that vary slowly over time.
//
// No Modbus, no CAN, no hardware required beyond WiFi.
// Credentials: include/credentials.h (WIFI_SSID, WIFI_PASSWORD, TB_ACCESS_TOKEN)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "credentials.h"

#define TB_HOST          "thingsboard.cloud"
#define TB_PORT          1883
#define TB_TOKEN         TB_ACCESS_TOKEN
#define PUBLISH_INTERVAL_MS 5000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ---------------------------------------------------------------------------
// Simulated state — drifts slowly to make widgets feel alive
// ---------------------------------------------------------------------------
struct SimState {
    float    soc        = 75.0f;    // %
    float    bms_v      = 496.0f;   // V
    float    bms_i      = 0.0f;     // A
    float    bms_temp   = 24.0f;    // °C
    float    p_inv      = 12.0f;    // kW
    float    grid_p     = -3.0f;    // kW (negative = exporting)
    float    load_p     = 9.0f;     // kW
    float    freq       = 50.0f;    // Hz
    float    v_phase    = 231.0f;   // V
    float    pf         = 0.98f;
    bool     running    = true;
    bool     grid_tied  = true;
} sim;

// Small random drift within bounds
float drift(float val, float step, float lo, float hi) {
    val += (random(-100, 100) / 100.0f) * step;
    return constrain(val, lo, hi);
}

void updateSim() {
    sim.soc     = drift(sim.soc,     0.2f,  10.0f, 95.0f);
    sim.bms_v   = drift(sim.bms_v,   1.0f, 450.0f, 540.0f);
    sim.bms_i   = drift(sim.bms_i,   5.0f, -200.0f, 200.0f);
    sim.bms_temp= drift(sim.bms_temp,0.2f,  15.0f,  45.0f);
    sim.p_inv   = drift(sim.p_inv,   1.0f,   0.0f,  30.0f);
    sim.grid_p  = drift(sim.grid_p,  1.0f, -15.0f,  15.0f);
    sim.load_p  = drift(sim.load_p,  0.5f,   1.0f,  20.0f);
    sim.freq    = drift(sim.freq,    0.02f, 49.8f,  50.2f);
    sim.v_phase = drift(sim.v_phase, 0.5f, 220.0f, 240.0f);
    sim.pf      = drift(sim.pf,      0.01f,  0.85f,  1.0f);
}

// ---------------------------------------------------------------------------
// WiFi / MQTT
// ---------------------------------------------------------------------------
void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
    mqtt.setServer(TB_HOST, TB_PORT);
    mqtt.setBufferSize(2048);
    String clientId = "ESP32_test_tb_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr))
            Serial.println(" OK");
        else {
            Serial.printf(" failed rc=%d, retrying in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

// ---------------------------------------------------------------------------
// Publish all keys
// ---------------------------------------------------------------------------
void publishTelemetry() {
    updateSim();

    JsonDocument doc;

    // --- Status flags ---
    doc["running"]    = sim.running  ? 1 : 0;
    doc["fault"]      = 0;
    doc["alarm"]      = 0;
    doc["grid_tied"]  = sim.grid_tied ? 1 : 0;
    doc["off_grid"]   = 0;
    doc["derating"]   = 0;
    doc["standby"]    = 0;

    // --- AC inverter ---
    doc["freq_hz"]    = sim.freq;
    doc["v_a"]        = sim.v_phase;
    doc["v_b"]        = sim.v_phase - 0.3f;
    doc["v_c"]        = sim.v_phase + 0.2f;
    doc["v_ab"]       = sim.v_phase * 1.732f;
    doc["v_bc"]       = sim.v_phase * 1.732f - 0.5f;
    doc["v_ca"]       = sim.v_phase * 1.732f + 0.3f;
    doc["i_a"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f;
    doc["i_b"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f - 0.1f;
    doc["i_c"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f + 0.1f;
    doc["p_a_kw"]     = sim.p_inv / 3.0f;
    doc["p_b_kw"]     = sim.p_inv / 3.0f - 0.1f;
    doc["p_c_kw"]     = sim.p_inv / 3.0f + 0.1f;
    doc["p_inv_kw"]   = sim.p_inv;
    doc["q_a_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_b_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_c_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_inv_kvar"] = sim.p_inv * 0.1f;
    doc["pf_a"]       = sim.pf;
    doc["pf_b"]       = sim.pf - 0.01f;
    doc["pf_c"]       = sim.pf + 0.01f;
    doc["pf_total"]   = sim.pf;

    // --- DC ---
    doc["dc_voltage_v"] = sim.bms_v;
    doc["dc_current_a"] = sim.p_inv / sim.bms_v * 1000.0f;
    doc["dc_power_kw"]  = sim.p_inv;

    // --- Grid ---
    doc["grid_freq_hz"] = sim.freq;
    doc["grid_v_a"]     = sim.v_phase;
    doc["grid_v_b"]     = sim.v_phase - 0.3f;
    doc["grid_v_c"]     = sim.v_phase + 0.2f;
    doc["grid_p_kw"]    = sim.grid_p;

    // --- Load ---
    doc["load_freq_hz"] = sim.freq;
    doc["load_v_a"]     = sim.v_phase;
    doc["load_v_b"]     = sim.v_phase - 0.3f;
    doc["load_v_c"]     = sim.v_phase + 0.2f;
    doc["load_i_a"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_i_b"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_i_c"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_p_a_kw"]  = sim.load_p / 3.0f;
    doc["load_p_b_kw"]  = sim.load_p / 3.0f;
    doc["load_p_c_kw"]  = sim.load_p / 3.0f;
    doc["load_p_kw"]    = sim.load_p;
    doc["load_s_kva"]   = sim.load_p / sim.pf;

    // --- BMS ---
    doc["bms_soc_pct"]            = (int)sim.soc;
    doc["bms_soh_pct"]            = 98;
    doc["bms_soe_pct"]            = (int)sim.soc;
    doc["bms_voltage_v"]          = sim.bms_v;
    doc["bms_current_a"]          = sim.bms_i;
    doc["bms_temperature_c"]      = sim.bms_temp;
    doc["bms_max_charge_a"]       = 200.0f;
    doc["bms_max_discharge_a"]    = 200.0f;
    doc["bms_status"]             = sim.bms_i > 0 ? 1 : (sim.bms_i < 0 ? 2 : 3);
    doc["bms_charge_forbidden"]   = 0;
    doc["bms_discharge_forbidden"]= 0;
    doc["bms_fault"]              = 0;
    doc["bms_alarm"]              = 0;
    doc["bms_protection"]         = 0;
    doc["bms_force_charge"]       = 0;

    char payload[2048];
    serializeJson(doc, payload, sizeof(payload));
    Serial.printf("[MQTT] Payload size: %d bytes\n", strlen(payload));

    bool ok = mqtt.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Publish %s — SOC=%d%% P_inv=%.1fkW Grid=%.1fkW\n",
                  ok ? "OK" : "FAIL", (int)sim.soc, sim.p_inv, sim.grid_p);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_tb starting...");
    randomSeed(esp_random());

    connectWiFi();
    connectMQTT();
    Serial.println("[Boot] Ready — publishing every 5s");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected())             connectMQTT();
    mqtt.loop();

    static unsigned long lastPublish = 0;
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();
        publishTelemetry();
    }
}
