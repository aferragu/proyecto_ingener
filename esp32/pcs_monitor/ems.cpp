#include "ems.h"
#include "modbus.h"
#include "config.h"

// Parámetros EMS — a mover a shared attributes de ThingsBoard en producción
static float EMS_SOC_MIN        = 20.0f;   // %
static float EMS_SOC_TARGET     = 90.0f;   // %
static float EMS_MAX_DISCHARGE  = 20.0f;   // kW
static float EMS_GRID_LIMIT     = 40.0f;   // kW
static float EMS_CHARGE_POWER   = 10.0f;   // kW

void emsUpdate(JsonDocument& telemetry) {
    // TODO: implementar cuando el equipo valide la estrategia
    //
    // Lógica propuesta:
    //
    // float soc      = telemetry["bms_soc_pct"]  | 0.0f;
    // float load_p   = telemetry["load_p_kw"]    | 0.0f;
    // float grid_p   = telemetry["grid_p_kw"]    | 0.0f;
    //
    // float setPwr = 0.0f;
    //
    // if (soc > EMS_SOC_MIN) {
    //     // Batería primero — aporta hasta MAX_DISCHARGE
    //     setPwr = min(load_p, EMS_MAX_DISCHARGE);
    // }
    //
    // if (soc < EMS_SOC_TARGET && load_p < 5.0f) {
    //     // Cargar batería cuando no hay carga
    //     setPwr = -EMS_CHARGE_POWER;
    // }
    //
    // setPwr = constrain(setPwr, -100.0f, 100.0f);
    // int16_t raw = (int16_t)(setPwr * 10.0f);
    // writeRegister(REG_SET_POWER, raw);
    // telemetry["ems_setpower_kw"] = setPwr;
}
