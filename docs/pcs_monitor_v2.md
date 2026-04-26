# pcs_monitor_v2.ino — Documentación

## Descripción general

Firmware para ESP32 NodeMCU que actúa como EMS (Energy Management System) de monitoreo y control básico para el inversor SinoSoar SP6030. Lee datos del inversor via Modbus RTU (RS-485), del BMS via CAN bus, publica telemetría a ThingsBoard Cloud via MQTT sobre WiFi, y recibe comandos de control via RPC.

---

## Arquitectura

```
                    ┌─────────────────────────────┐
                    │           ESP32              │
                    │                              │
  Inversor SP6030 ──┤ RS-485 (UART2)   WiFi       ├──► ThingsBoard Cloud
                    │ GPIO16/17/4       MQTT        │    (telemetría + attrs + RPC)
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
4. Conecta MQTT a ThingsBoard Cloud, setea callback RPC y se suscribe a `v1/devices/me/rpc/request/+`
5. Lee versión del firmware del inversor (`readFirmwareVersion()`) y la publica como atributos del dispositivo
6. Ejecuta secuencia de inicialización del inversor (`inverterInit()`)
7. Realiza primera lectura completa y publica

### loop()
Ejecuta cuatro tareas periódicas más el procesamiento de mensajes MQTT entrantes (RPC):

| Tarea | Función | Intervalo |
|---|---|---|
| Procesar MQTT (RPC) | `mqttClient.loop()` | continuo |
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
- Timeout en dos fases: espera primer byte hasta 200ms, luego drena 50ms adicionales. Si no llega nada, retorna false inmediatamente sin bloquear el loop.

### Inicialización del inversor (`inverterInit`)
Replica la secuencia de configuración del EMS del fabricante (obtenida por captura Wireshark). El reg 873 usa read-modify-write para preservar otros bits:

| Registro | Valor | Descripción |
|---|---|---|
| 763 | 1500 | Max corriente descarga DC = 150 A |
| 764 | 1500 | Max corriente carga DC = 150 A |
| 341 | 1 | Control por fase individual |
| 652 | 0 | PV apagado |
| 873 | bit0=1 | Anti-backflow habilitado (read-modify-write) |
| 795 | 0 | Detección de fuga deshabilitada |
| 656 | 0 | DCDC apagado |

### Verificación periódica (`verifyAndReinit`)
Cada 60 segundos lee los registros de configuración y los compara con los valores esperados. Si detecta una diferencia (indicador de que el inversor se reinició y perdió la config) los corrige automáticamente. El reg 873 se verifica y corrige con read-modify-write.

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

### RPC (`onRpcMessage`)
Recibe comandos desde ThingsBoard via MQTT y los ejecuta via Modbus. Responde en `v1/devices/me/rpc/response/{id}` con `{"result":"ok"}` o `{"result":"error","message":"..."}`.

| Método | Params | Registro | Descripción |
|---|---|---|---|
| `powerOn` | ninguno | 650 | Arranque general del inversor |
| `shutdown` | ninguno | 651 | Parada general del inversor |
| `setPower` | `{"value": X}` | 135 | Setpoint potencia activa (kW, -100..+100) |

Para probar desde ThingsBoard: **Devices → dispositivo → pestaña RPC** → mandar two-way RPC con el JSON correspondiente.

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
#define MODBUS_DEVICE_ID 1                  // Dirección DIP switch del inversor
#define BMS_CAN_ADDR     1                  // Dirección del BMS (1-15), embebida en CAN ID
#define CAN_SPEED        TWAI_TIMING_CONFIG_250KBITS()  // Velocidad CAN del BMS

#define POLL_MODBUS_MS   5000   // Intervalo poll inversor (ms)
#define POLL_CAN_MS      1000   // Intervalo poll BMS (ms)
#define PUBLISH_MS       10000  // Intervalo publicación ThingsBoard (ms)
#define VERIFY_INIT_MS   60000  // Intervalo verificación config inversor (ms)
```

---

## Comportamiento sin hardware conectado

El firmware arranca y funciona parcialmente sin inversor ni BMS conectados:
- WiFi, MQTT y RPC funcionan normalmente
- Los reads Modbus fallan con timeout y se loguean como error — el sistema sigue funcionando
- Los writes Modbus (init, RPC) fallan y responden `modbus write failed`
- La telemetría se publica con los campos en 0 o vacíos
- Útil para verificar conectividad y RPC antes de tener el hardware

---

## Dependencias

| Librería | Autor | Instalación |
|---|---|---|
| PubSubClient | Nick O'Leary | Arduino Library Manager |
| ArduinoJson | Benoit Blanchon | Arduino Library Manager |
| driver/twai.h | Espressif | Incluida en ESP32 core ≥ 3.x |

**ESP32 core requerido:** 3.3.x o superior (Arduino IDE → Boards Manager → esp32)

---

## TODO

### Hardware
- Identificar modelo y protocolo CAN del BMS para implementar `pollCAN()`
- Confirmar dirección Modbus del inversor via DIP switch (`MODBUS_DEVICE_ID`)
- Confirmar dirección CAN del BMS (`BMS_CAN_ADDR`)
- Verificar que el firmware del inversor soporta protocolo RTU V3.0 (reg 19 ≥ 30) para registros 200–213
- Agregar soporte pantalla LCD ST7789 1.14" (ideaspark ESP32) — pines compatibles con esquema actual (MOSI=23, SCLK=18, CS=15, BLK=32)

### Firmware
- Implementar protocolo CAN del BMS en `pollCAN()` — estructura lista, falta decodificación de IDs
- Implementar lógica de control EMS:
  - Batería primero: `setPower = min(load_p_kw, MAX_DISCHARGE)` cuando `SOC > SOC_MIN`
  - Peak shaving: limitar `grid_p_kw` a `GRID_LIMIT` via rampa en `setPower`
  - Scheduler horario: franjas de carga/descarga configurables (Time of Use)
- Agregar shared attributes de ThingsBoard para configurar parámetros remotamente sin reflashear: `SOC_MIN`, `SOC_TARGET`, `GRID_LIMIT`, `MAX_DISCHARGE`, `CHARGE_POWER`
- Migrar a arquitectura dual-core FreeRTOS para producción:
  - Core 0: WiFi, MQTT, RPC, publicación telemetría
  - Core 1: Modbus polling, CAN polling, lógica EMS
  - Mutex para acceso compartido al documento de telemetría
- Migrar configuración (SSID, token, parámetros EMS) a NVS (Non-Volatile Storage) para cambiar sin reflashear
- Refactorizar a estructura modular para producción (ver sección siguiente)

### ThingsBoard
- Conectar rule chain de alarmas al Root Rule Chain
- Agregar widgets RPC al dashboard (Action buttons para powerOn/shutdown, slider para setPower)
- Agregar widget de alarmas activas al dashboard

### Arquitectura para producción
Refactorizar el `.ino` monolítico a estructura modular:
```
pcs_monitor/
├── pcs_monitor.ino        # solo setup() y loop()
├── config.h               # todos los #define configurables
├── modbus.h / modbus.cpp  # readRegisters, writeRegister, CRC16
├── inverter.h / inverter.cpp  # inverterInit, verifyAndReinit, pollModbus
├── bms.h / bms.cpp        # initCAN, pollCAN, protocolo BMS
├── mqtt.h / mqtt.cpp      # connectMQTT, publishTelemetry, onRpcMessage
└── ems.h / ems.cpp        # lógica de control (setPower, peak shaving, scheduler)
```

