#pragma once
#include <ArduinoJson.h>

// TODO: implementar lógica de control EMS cuando el equipo valide la estrategia
// Parámetros a configurar via ThingsBoard shared attributes:
//   EMS_SOC_MIN        — SOC mínimo antes de dejar de descargar (%)
//   EMS_SOC_TARGET     — SOC objetivo al cargar desde red (%)
//   EMS_MAX_DISCHARGE  — potencia máxima de descarga (kW)
//   EMS_GRID_LIMIT     — potencia máxima de toma de red (kW)
//   EMS_CHARGE_POWER   — potencia de carga desde red cuando no hay carga (kW)

void emsUpdate(JsonDocument& telemetry);
