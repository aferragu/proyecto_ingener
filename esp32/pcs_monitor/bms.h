#pragma once
#include <ArduinoJson.h>

// Datos del BMS expuestos al resto del sistema
struct BmsData {
    float    voltage_v;          // tensión total del pack (V)
    float    current_a;          // corriente (A, positivo=carga, negativo=descarga)
    float    temperature_c;      // temperatura BMS (°C)
    uint8_t  soc_pct;            // State of Charge (%)
    uint8_t  soh_pct;            // State of Health (%)
    uint8_t  soe_pct;            // State of Energy disponible (%)
    float    max_charge_a;       // máxima corriente de carga permitida (A)
    float    max_discharge_a;    // máxima corriente de descarga permitida (A)
    float    charge_cutoff_v;    // tensión de corte de carga (V)
    float    discharge_cutoff_v; // tensión de corte de descarga (V)

    // Flags de control — prioridad máxima
    bool     charge_forbidden;   // BMS prohíbe carga
    bool     discharge_forbidden;// BMS prohíbe descarga

    // Flags de estado
    uint8_t  status;             // 0=sleep, 1=charging, 2=discharging, 3=idle
    bool     force_charge_req;   // BMS solicita carga forzada

    // Flags de fault/alarm/protection (cualquier bit = problema)
    uint8_t  fault;
    uint16_t alarm;
    uint16_t protection;

    bool     valid;              // true si se recibió al menos un mensaje 0x421
};

extern BmsData bmsData;

void initCAN();
void initBMS();
void pollCAN(JsonDocument& telemetry);
