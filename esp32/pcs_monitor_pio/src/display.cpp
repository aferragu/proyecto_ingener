// =============================================================================
// display.cpp — TFT_eSPI display driver (pantalla vertical 135×240)
//
// Muestra estado del sistema en pantalla vertical usando TFT_eSPI.
// Solo redibuja los valores que cambiaron para minimizar flicker.
//
// Layout (de arriba a abajo):
//   Header fijo: "PCS INVERTER MONITOR"
//   Estado inversor: badge RUNNING/FAULT/STOPPED + indicador MQTT
//   Potencia inversor (kW)
//   Potencia red (kW)
//   Potencia carga (kW)
//   DC: tensión (V) + corriente (A)
//   Batería: SOC (%) + tensión (V) + corriente (A)
//
// Llamar displayInit() en setup() y displayUpdate(telemetry, mqttOk) en loop().
// =============================================================================
#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// Layout — pantalla 135×240 en orientación vertical
// Fuentes TFT_eSPI built-in:
//   1 = 8px, 2 = 16px, 4 = 26px, 6 = 48px (solo dígitos), 7 = 7seg 48px
// ---------------------------------------------------------------------------

static TFT_eSPI tft;

// Colores
#define C_BG       TFT_BLACK
#define C_TITLE    TFT_DARKGREY
#define C_OK       0x07E0   // verde
#define C_WARN     0xFD20   // naranja
#define C_FAULT    TFT_RED
#define C_VALUE    TFT_WHITE
#define C_UNIT     TFT_DARKGREY
#define C_LABEL    0x8410   // gris medio

// Última pantalla renderizada — para evitar redraws innecesarios
static uint8_t  last_running   = 255;
static uint8_t  last_fault     = 255;
static uint8_t  last_alarm     = 255;
static bool     last_mqtt      = false;
static float    last_p_inv     = -9999;
static float    last_grid_p    = -9999;
static float    last_load_p    = -9999;
static float    last_dc_v      = -9999;
static float    last_dc_i      = -9999;
// BMS
static float    last_bms_soc   = -9999;
static float    last_bms_v     = -9999;
static float    last_bms_i     = -9999;
static float    last_bms_temp  = -9999;

void displayInit() {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void drawLabel(int16_t x, int16_t y, const char* label, uint16_t color = C_LABEL) {
    tft.setTextFont(1);
    tft.setTextColor(color, C_BG);
    tft.setCursor(x, y);
    tft.print(label);
}

static void drawValue(int16_t x, int16_t y, float val, uint8_t decimals,
                      const char* unit, uint16_t color = C_VALUE) {
    tft.setTextFont(2);
    tft.setTextColor(color, C_BG);
    tft.setCursor(x, y);
    tft.print(val, decimals);
    tft.setTextFont(1);
    tft.setTextColor(C_UNIT, C_BG);
    tft.print(" ");
    tft.print(unit);
}

static void drawStatusBadge(int16_t x, int16_t y, const char* text, uint16_t color) {
    tft.fillRoundRect(x, y, 100, 18, 4, color);
    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, color);
    tft.setCursor(x + 5, y + 5);
    tft.print(text);
}

static void drawHLine(int16_t y) {
    tft.drawFastHLine(0, y, 135, C_TITLE);
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void displayInit() {
    tft.init();
    tft.setRotation(0);         // vertical, USB abajo
    tft.fillScreen(C_BG);
    tft.setTextWrap(false);

    // Header fijo
    tft.setTextFont(1);
    tft.setTextColor(C_TITLE, C_BG);
    tft.setCursor(5, 5);
    tft.print("PCS INVERTER MONITOR");
    drawHLine(16);

    Serial.println("[Display] Inicializado");
}

// ---------------------------------------------------------------------------
// Update — dibuja solo lo que cambió
// ---------------------------------------------------------------------------
void displayUpdate(JsonDocument& telemetry, bool mqttConnected) {

    uint8_t running   = telemetry["running"]   | 0;
    uint8_t fault     = telemetry["fault"]     | 0;
    uint8_t alarm     = telemetry["alarm"]     | 0;
    float   p_inv     = telemetry["p_inv_kw"]  | 0.0f;
    float   grid_p    = telemetry["grid_p_kw"] | 0.0f;
    float   load_p    = telemetry["load_p_kw"] | 0.0f;
    float   dc_v      = telemetry["dc_voltage_v"] | 0.0f;
    float   dc_i      = telemetry["dc_current_a"] | 0.0f;

    // — Fila 1: Estado del inversor —
    if (running != last_running || fault != last_fault) {
        if (fault)
            drawStatusBadge(5, 22, "  FAULT", C_FAULT);
        else if (running)
            drawStatusBadge(5, 22, "  RUNNING", C_OK);
        else
            drawStatusBadge(5, 22, "  STOPPED", C_WARN);
        last_running = running;
        last_fault   = fault;
    }

    // — Fila 1b: Alarm + MQTT —
    if (alarm != last_alarm) {
        if (alarm)
            drawStatusBadge(110, 22, "ALM", C_WARN);
        else {
            tft.fillRoundRect(110, 22, 20, 18, 4, C_BG);
        }
        last_alarm = alarm;
    }
    if (mqttConnected != last_mqtt) {
        tft.setTextFont(1);
        tft.setTextColor(mqttConnected ? C_OK : C_FAULT, C_BG);
        tft.setCursor(118, 5);
        tft.print(mqttConnected ? "W" : "X");   // W=wifi ok, X=sin conexión
        last_mqtt = mqttConnected;
    }

    drawHLine(44);

    // — Fila 2: Potencia inversor —
    drawLabel(5, 50, "INVERSOR");
    if (p_inv != last_p_inv) {
        tft.fillRect(5, 60, 130, 20, C_BG);
        uint16_t col = p_inv > 0 ? C_OK : (p_inv < 0 ? C_WARN : C_VALUE);
        drawValue(5, 60, p_inv, 2, "kW", col);
        last_p_inv = p_inv;
    }

    drawHLine(84);

    // — Fila 3: Potencia red —
    drawLabel(5, 90, "RED");
    if (grid_p != last_grid_p) {
        tft.fillRect(5, 100, 130, 20, C_BG);
        drawValue(5, 100, grid_p, 2, "kW");
        last_grid_p = grid_p;
    }

    drawHLine(124);

    // — Fila 4: Potencia carga —
    drawLabel(5, 130, "CARGA");
    if (load_p != last_load_p) {
        tft.fillRect(5, 140, 130, 20, C_BG);
        drawValue(5, 140, load_p, 2, "kW");
        last_load_p = load_p;
    }

    drawHLine(164);

    // — Fila 5: DC —
    drawLabel(5, 170, "DC");
    if (dc_v != last_dc_v) {
        tft.fillRect(5, 180, 65, 20, C_BG);
        drawValue(5, 180, dc_v, 1, "V");
        last_dc_v = dc_v;
    }
    if (dc_i != last_dc_i) {
        tft.fillRect(70, 180, 65, 20, C_BG);
        drawValue(70, 180, dc_i, 1, "A");
        last_dc_i = dc_i;
    }

    drawHLine(204);

    // — Fila 6: BMS —
    float bms_soc  = telemetry["bms_soc_pct"]   | 0.0f;
    float bms_v    = telemetry["bms_voltage_v"]  | 0.0f;
    float bms_i    = telemetry["bms_current_a"]  | 0.0f;
    float bms_temp = telemetry["bms_temp_avg_c"] | 0.0f;

    drawLabel(5, 210, "BATERIA");
    if (bms_soc != last_bms_soc) {
        tft.fillRect(5, 220, 130, 16, C_BG);
        uint16_t col = bms_soc > 50 ? C_OK : (bms_soc > 20 ? C_WARN : C_FAULT);
        drawValue(5, 220, bms_soc, 1, "% SOC", col);
        last_bms_soc = bms_soc;
    }
    if (bms_v != last_bms_v) {
        tft.fillRect(5, 236, 65, 16, C_BG);
        drawValue(5, 236, bms_v, 1, "V");
        last_bms_v = bms_v;
    }
    if (bms_i != last_bms_i) {
        tft.fillRect(70, 236, 65, 16, C_BG);
        uint16_t col = bms_i > 0 ? C_OK : (bms_i < 0 ? C_WARN : C_VALUE);
        drawValue(70, 236, bms_i, 1, "A", col);
        last_bms_i = bms_i;
    }
}
