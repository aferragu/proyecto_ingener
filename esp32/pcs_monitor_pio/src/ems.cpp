// =============================================================================
// ems.cpp — Energy Management System
//
// Lógica de control de potencia: decide el setpoint del inversor en función
// del SOC de la batería, la carga AC y los límites del BMS.
//
// Estrategia:
//   - SOC > SOC_MIN y carga > 0  → descarga batería hasta cubrir la carga
//   - SOC < SOC_TARGET y carga < 5kW → carga batería desde red
//   - BMS fault o forbidden       → setpoint = 0
//
// TODO: activar emsUpdate() en main.cpp cuando se valide la estrategia en hw.
// =============================================================================
#include "ems.h"
#include "inverter.h"
#include "bms.h"
#include "config.h"

// Parámetros EMS — a mover a shared attributes ThingsBoard en producción
static float EMS_SOC_MIN        = 20.0f;   // % — no descargar por debajo
static float EMS_SOC_TARGET     = 90.0f;   // % — cargar hasta aquí cuando no hay carga
static float EMS_MAX_DISCHARGE  = 20.0f;   // kW — límite de descarga por defecto
static float EMS_GRID_LIMIT     = 40.0f;   // kW — máximo a tomar de red
static float EMS_CHARGE_POWER   = 10.0f;   // kW — potencia de carga desde red

void emsUpdate(JsonDocument& telemetry) {
    // TODO: activar cuando el equipo valide la estrategia
    // El código está listo para usar, solo descomentar

    // // 1. Flags de emergencia del BMS — prioridad máxima
    // if (!bmsData.valid) return;   // sin datos del BMS, no actuar

    // if (bmsData.charge_forbidden && bmsData.discharge_forbidden) {
    //     writeRegister(REG_SET_POWER, 0);
    //     telemetry["ems_setpower_kw"] = 0;
    //     telemetry["ems_state"] = "BMS_FAULT";
    //     return;
    // }

    // if (bmsData.fault != 0 || bmsData.protection != 0) {
    //     writeRegister(REG_SET_POWER, 0);
    //     telemetry["ems_setpower_kw"] = 0;
    //     telemetry["ems_state"] = "BMS_PROTECTION";
    //     return;
    // }

    // float soc     = bmsData.soc_pct;
    // float load_p  = telemetry["load_p_kw"]  | 0.0f;

    // // Límite real de descarga: mínimo entre parámetro EMS y límite del BMS
    // float max_discharge_kw = min(EMS_MAX_DISCHARGE,
    //     bmsData.max_discharge_a * bmsData.voltage_v / 1000.0f);

    // float setPwr = 0.0f;
    // const char* state = "IDLE";

    // if (soc > EMS_SOC_MIN && !bmsData.discharge_forbidden) {
    //     // Batería primero
    //     setPwr = min(load_p, max_discharge_kw);
    //     state = "DISCHARGING";
    // }

    // if (soc < EMS_SOC_TARGET && load_p < 5.0f && !bmsData.charge_forbidden) {
    //     // Cargar desde red cuando no hay carga
    //     setPwr = -EMS_CHARGE_POWER;
    //     state = "CHARGING";
    // }

    // setPwr = constrain(setPwr, -100.0f, 100.0f);
    // int16_t raw = (int16_t)(setPwr * 10.0f);
    // writeRegister(REG_SET_POWER, raw);

    // telemetry["ems_setpower_kw"] = setPwr;
    // telemetry["ems_state"]       = state;
    // telemetry["ems_soc"]         = soc;
}
