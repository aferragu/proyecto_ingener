# pcs_monitor_v2.ino — Documentación

## Descripción general

Firmware para ESP32 NodeMCU que actúa como EMS (Energy Management System) de monitoreo para el inversor SinoSoar SP6030. Lee datos del inversor via Modbus RTU (RS-485), del BMS via CAN bus, y publica telemetría a ThingsBoard Cloud via MQTT sobre WiFi.

---

## Arquitectura

```
                    ┌─────────────────────────────┐
                    │           ESP32              │
                    │                              │
  Inversor SP6030 ──┤ RS-485 (UART2)   WiFi       ├──► ThingsBoard Cloud
                    │ GPIO16/17/4       MQTT        │    (telemetría + attrs)
  BMS (CAN) ────────┤ TWAI CAN                     │
                    │ GPIO21/22                     │
                    └─────────────────────────────┘
```

---

## Flujo de ejecución

### setup()
1. Inicializa RS-485 (UART2, 115200 bps, 8N1)
2. Inicializa CAN bus (TWAI, modo listen-only, 250 kbps)
3. Conecta WiFi
4. Conecta MQTT a ThingsBoard Cloud
5. Lee versión del firmware del inversor (`readFirmwareVersion()`) y la publica como atributos del dispositivo
6. Ejecuta secuencia de inicialización del inversor (`inverterInit()`)
7. Realiza primera lectura completa y publica

### loop()
Ejecuta cuatro tareas periódicas:

| Tarea | Función | Intervalo |
|---|---|---|
| Verificar configuración | `verifyAndReinit()` | 60 s |
| Poll inversor | `pollModbus()` | 5 s |
| Poll BMS | `pollCAN()` | 1 s |
| Publicar telemetría | `publishTelemetry()` | 10 s |

---

## Módulos principales

### Modbus RTU (`readRegisters`, `writeRegister`)
Implementación manual del protocolo Modbus RTU sobre RS-485:
- `readRegisters(startReg, count, out[])` — FC 0x03, lee N registros contiguos
- `writeRegister(reg, value)` — FC 0x06, escribe un registro
- CRC16 calculado con polinomio 0xA001, byte bajo primero
- Control de dirección del MAX485 via pin DE/RE (GPIO4)

### Inicialización del inversor (`inverterInit`)
Replica la secuencia de configuración del EMS del fabricante (obtenida por captura Wireshark):

| Registro | Valor | Descripción |
|---|---|---|
| 763 | 1500 | Max corriente descarga DC = 150 A |
| 764 | 1500 | Max corriente carga DC = 150 A |
| 341 | 1 | Control por fase individual |
| 652 | 0 | PV apagado |
| 873 | 1 | Anti-backflow habilitado |
| 795 | 0 | Detección de fuga deshabilitada |
| 656 | 0 | DCDC apagado |

### Verificación periódica (`verifyAndReinit`)
Cada 60 segundos lee los registros de configuración y los compara con los valores esperados. Si detecta una diferencia (indicador de que el inversor se reinició y perdió la config) los corrige automáticamente.

### Lectura del inversor (`pollModbus`)
Lee los siguientes bloques en cada ciclo de 5 segundos:

| Bloque | Registros | Keys ThingsBoard |
|---|---|---|
| Estado general | 32 | running, fault, alarm, grid_tied, off_grid, derating, standby |
| AC inversor | 100–125 | freq_hz, v_a/b/c, v_ab/bc/ca, i_a/b/c, p_a/b/c_kw, q_a/b/c_kvar, pf_a/b/c, p_inv_kw, q_inv_kvar, pf_total |
| DC | 141–143 | dc_power_kw, dc_voltage_v, dc_current_a |
| Red | 170–179 | grid_freq_hz, grid_v_a/b/c |
| Potencia red | 192 | grid_p_kw |
| Carga (V3.0) | 200–213 | load_freq_hz, load_v_a/b/c, load_i_a/b/c, load_p_a/b/c_kw, load_p_kw, load_s_kva |

> ⚠ Los registros 200–213 requieren firmware con protocolo RTU V3.0 (reg 19 ≥ 30).

### Lectura del BMS (`pollCAN`)
Arranca el driver TWAI en modo `LISTEN_ONLY` para no interferir con el bus. Por ahora loguea todas las tramas CAN recibidas y las publica como `can_0xXXX` en ThingsBoard para observación.

La función tiene la estructura lista para agregar decodificación cuando se conozca el protocolo del BMS:
```cpp
if (msg.identifier == 0xXXXX) {
    // decodificar bytes y agregar a telemetry[]
}
```

### Telemetría (`publishTelemetry`)
Publica el snapshot completo de mediciones como un único JSON al topic MQTT:
```
v1/devices/me/telemetry
```

Los atributos estáticos (versión de firmware, modelo) se publican una sola vez en el arranque al topic:
```
v1/devices/me/attributes
```

---

## Parámetros configurables

```cpp
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASSWORD    "TU_PASSWORD"
#define TB_ACCESS_TOKEN  "TU_ACCESS_TOKEN"  // Access token del dispositivo en ThingsBoard
#define MODBUS_DEVICE_ID 1                  // Dirección Modbus del inversor (1–247)
#define CAN_SPEED        TWAI_TIMING_CONFIG_250KBITS()  // Velocidad CAN del BMS

#define POLL_MODBUS_MS   5000   // Intervalo poll inversor (ms)
#define POLL_CAN_MS      1000   // Intervalo poll BMS (ms)
#define PUBLISH_MS       10000  // Intervalo publicación ThingsBoard (ms)
#define VERIFY_INIT_MS   60000  // Intervalo verificación config inversor (ms)
```

---

## Dependencias

| Librería | Autor | Instalación |
|---|---|---|
| PubSubClient | Nick O'Leary | Arduino Library Manager |
| ArduinoJson | Benoit Blanchon | Arduino Library Manager |
| driver/twai.h | Espressif | Incluida en ESP32 core ≥ 3.x |

**ESP32 core requerido:** 3.3.x o superior (Arduino IDE → Boards Manager → esp32)
