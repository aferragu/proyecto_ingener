# pcs_monitor — Documentación

## Descripción general

Firmware para ESP32 NodeMCU que actúa como EMS (Energy Management System) de monitoreo y control para el inversor SinoSoar SP6030. Lee datos del inversor via Modbus RTU (RS-485), del BMS via CAN bus, publica telemetría a ThingsBoard Cloud via MQTT sobre WiFi, y recibe comandos de control via RPC.

El proyecto tiene dos sketches:
- **`pcs_monitor_v2/`** — monolítico, para prototipo y pruebas rápidas
- **`pcs_monitor/`** — modular, arquitectura de producción

---

## Arquitectura del sistema

```
┌─────────────────────────────────────────────────────┐
│                    ESP32 NodeMCU                     │
│                                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ modbus   │  │   bms    │  │      mqtt        │  │
│  │ .cpp/.h  │  │ .cpp/.h  │  │    .cpp/.h       │  │
│  └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
│       │              │                 │             │
│  ┌────┴─────┐  ┌─────┴────┐  ┌────────┴─────────┐  │
│  │ inverter │  │  (stub)  │  │   ThingsBoard     │  │
│  │ .cpp/.h  │  │  CAN bus │  │   MQTT Cloud      │  │
│  └──────────┘  └──────────┘  └───────────────────┘  │
│                                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │   ems    │  │ display  │  │     config.h     │  │
│  │ .cpp/.h  │  │ .cpp/.h  │  │  credentials.h   │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
└─────────────────────────────────────────────────────┘
         │                │                │
    RS-485 MAX485     CAN SN65HVD230    WiFi
         │                │
  Inversor SP6030      BMS (TBD)
```

---

## Conexiones de hardware

```
ESP32 GPIO17 (TX2) ──► MAX485 DI
ESP32 GPIO16 (RX2) ◄── MAX485 RO        → RS-485 bus → Inversor
ESP32 GPIO4        ──► MAX485 DE+RE

ESP32 GPIO21 (TX)  ──► SN65HVD230 D
ESP32 GPIO22 (RX)  ◄── SN65HVD230 R     → CAN bus → BMS
             GND   ──► SN65HVD230 Rs

ESP32 GPIO2        ──► LED integrado (activo LOW)

ESP32 GPIO23 (MOSI)──► LCD MOSI
ESP32 GPIO18 (SCLK)──► LCD SCLK         → ST7789 1.14" (ideaspark)
ESP32 GPIO15 (CS)  ──► LCD CS
ESP32 GPIO32 (BLK) ──► LCD backlight
```

---

## Estructura de archivos

```
esp32/pcs_monitor/
├── pcs_monitor.ino     # setup() y loop() — orquestador
├── config.h            # todos los #define configurables
├── credentials.h       # WiFi + token (gitignored, ver .example)
├── credentials.h.example
├── modbus.h/cpp        # Modbus RTU: readRegisters, writeRegister, CRC16
├── inverter.h/cpp      # init, verify, pollModbus, readFirmwareVersion
├── bms.h/cpp           # CAN TWAI: initCAN, pollCAN (stub)
├── mqtt.h/cpp          # WiFi, MQTT, RPC, publishTelemetry, updateLed
├── ems.h/cpp           # lógica de control EMS (stub, pendiente)
└── display.h/cpp       # LCD ST7789 TFT_eSPI (pendiente integración)
```

---

## Flujo de ejecución

### setup()
```
Boot
 ├── LED OFF
 ├── RS-485 init (UART2, 115200 bps, 8N1)
 ├── CAN init (TWAI, listen-only, 250 kbps)
 ├── WiFi connect
 ├── MQTT connect + suscribe RPC
 ├── readFirmwareVersion() → publica atributos ThingsBoard
 ├── inverterInit() → escribe secuencia de configuración
 ├── pollModbus() → primera lectura
 └── publishTelemetry() → primera publicación
```

### loop()
```
Loop continuo
 ├── mqttClient.loop()          continuo — procesa RPC entrantes
 ├── WiFi reconnect             si desconectado
 ├── updateLed()                continuo
 ├── verifyAndReinit()          cada 60s
 ├── pollModbus() + emsUpdate() cada 5s
 └── publishTelemetry()         cada 10s
```

---

## Módulos

### modbus
Implementación manual Modbus RTU sobre RS-485:
- FC 0x03 — Read Holding Registers
- FC 0x06 — Preset Single Register
- CRC16 polinomio 0xA001, byte bajo primero
- Timeout en dos fases: espera primer byte 50ms, drena 20ms más. Retorna `false` inmediatamente si no hay respuesta.

### inverter
| Función | Descripción |
|---|---|
| `readFirmwareVersion()` | Lee regs 0–21, loguea versiones, publica como atributos TB |
| `inverterInit()` | Escribe secuencia de init del fabricante (ver tabla abajo) |
| `verifyAndReinit()` | Verifica config cada 60s y corrige si el inversor se reinició |
| `pollModbus()` | Lee 6 bloques de registros y llena el documento de telemetría |

**Secuencia de init** (replicada del EMS del fabricante via Wireshark):

| Registro | Valor | Descripción |
|---|---|---|
| 763 | 1500 | Max corriente descarga DC = 150 A (×0.1) |
| 764 | 1500 | Max corriente carga DC = 150 A (×0.1) |
| 341 | 1 | Control por fase individual |
| 652 | 0 | PV apagado |
| 873 | bit0=1 | Anti-backflow habilitado (**read-modify-write**) |
| 795 | 0 | Detección de fuga deshabilitada |
| 656 | 0 | DCDC apagado |

> ⚠ Power ON (reg 650) **no está** en el init automático — debe ejecutarse manualmente via RPC `powerOn` para evitar reinicios involuntarios.

**Bloques Modbus leídos en cada ciclo:**

| Bloque | Registros | Keys ThingsBoard |
|---|---|---|
| Estado | 32 | running, fault, alarm, grid_tied, off_grid, derating, standby |
| AC inversor | 100–125 | freq_hz, v_a/b/c, v_ab/bc/ca, i_a/b/c, p_a/b/c_kw, q_a/b/c_kvar, pf_a/b/c, p_inv_kw, q_inv_kvar, pf_total |
| DC | 141–143 | dc_power_kw, dc_voltage_v, dc_current_a |
| Red | 170–179 | grid_freq_hz, grid_v_a/b/c |
| Potencia red | 192 | grid_p_kw |
| Carga (V3.0) | 200–213 | load_freq_hz, load_v/i_a/b/c, load_p_a/b/c_kw, load_p_kw, load_s_kva |

> ⚠ Registros 200–213 requieren firmware RTU V3.0 (reg 19 ≥ 30).

### mqtt
- `connectWiFi()` / `connectMQTT()` — conexión y reconexión automática
- `publishTelemetry()` — serializa JSON y publica a `v1/devices/me/telemetry`
- `onRpcMessage()` — callback MQTT para RPCs entrantes
- `updateLed()` — LED verde si WiFi + MQTT OK, apagado si cualquiera falla
- Client ID único basado en MAC para evitar conflictos entre dispositivos
- `setServer/setBufferSize/setCallback` se llaman solo una vez (flag estático)

**RPCs soportados:**

| Método | Params | Registro | Descripción |
|---|---|---|---|
| `powerOn` | — | 650 | Arranque general |
| `shutdown` | — | 651 | Parada general |
| `setPower` | `{"value": X}` | 135 | Setpoint potencia activa (kW, −100..+100) |

### bms
STUB — CAN en modo `LISTEN_ONLY`. Loguea tramas y las publica como `can_0xXXX` para observación. Estructura lista para decodificación cuando se identifique el protocolo del BMS.

### ems
STUB — función `emsUpdate()` vacía con lógica propuesta comentada. Se activa cuando el equipo valide la estrategia de control.

### display
Módulo para LCD ST7789 1.14" via TFT_eSPI. **No integrado aún** — pendiente recepción del hardware. Muestra: estado del inversor, potencia inversor/red/carga, tensión y corriente DC, indicador de conexión MQTT.

---

## Configuración

### credentials.h
```cpp
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASSWORD    "TU_PASSWORD"
#define TB_ACCESS_TOKEN  "TU_ACCESS_TOKEN"
```
Nunca subir al repo. Crear desde `credentials.h.example`.

### config.h — parámetros clave
```cpp
#define MODBUS_DEVICE_ID 1    // confirmar con DIP switch del inversor
#define BMS_CAN_ADDR     1    // confirmar con configuración del BMS
#define CAN_SPEED        TWAI_TIMING_CONFIG_250KBITS()  // ajustar según BMS

#define POLL_MODBUS_MS   5000   // poll inversor
#define PUBLISH_MS       10000  // publicación ThingsBoard
#define VERIFY_INIT_MS   60000  // verificación config inversor
```

---

## Failsafe y robustez

- Si el ESP32 falla, el inversor sigue operando con los últimos parámetros escritos
- `verifyAndReinit()` restaura la config si el inversor se reinicia
- El ESP32 tiene watchdog de hardware integrado (TWDT) — reinicia si el loop se bloquea
- Reconexión automática de WiFi y MQTT en el loop
- ⚠ Conocido: `connectMQTT()` puede bloquearse si el broker no responde — pendiente timeout explícito o migración a AsyncMqttClient

---

## Dependencias

| Librería | Autor | Instalación |
|---|---|---|
| PubSubClient | Nick O'Leary | Arduino Library Manager |
| ArduinoJson | Benoit Blanchon | Arduino Library Manager |
| TFT_eSPI | Bodmer | Arduino Library Manager (para display) |
| driver/twai.h | Espressif | Incluida en ESP32 core ≥ 3.x |

**ESP32 core:** 3.3.x o superior

---

## TODO

### Hardware pendiente
- Confirmar dirección Modbus del inversor via DIP switch → `MODBUS_DEVICE_ID`
- Confirmar dirección CAN del BMS → `BMS_CAN_ADDR`
- Verificar firmware inversor soporta RTU V3.0 (reg 19 ≥ 30)
- Recibir y conectar módulo LCD ideaspark ST7789 1.14"
- Identificar modelo y protocolo CAN del BMS

### Firmware
- Implementar `pollCAN()` con protocolo real del BMS
- Integrar `display.h/cpp` en `pcs_monitor.ino`
- Implementar `emsUpdate()` — estrategia validada con el equipo:
  - Batería primero: `setPower = min(load_p_kw, MAX_DISCHARGE)` cuando `SOC > SOC_MIN`
  - Cargar batería desde red cuando no hay carga y `SOC < SOC_TARGET`
- Agregar shared attributes ThingsBoard para configurar remotamente: `SOC_MIN`, `SOC_TARGET`, `MAX_DISCHARGE`, `GRID_LIMIT`, `CHARGE_POWER`
- Resolver bloqueo de `connectMQTT()` con timeout explícito o AsyncMqttClient
- Migrar a dual-core FreeRTOS (Core 0: MQTT/WiFi, Core 1: Modbus/CAN/EMS)
- Migrar credenciales y parámetros a NVS para configuración sin reflashear

### ThingsBoard
- Conectar rule chain de alarmas al Root Rule Chain
- Agregar widgets RPC al dashboard: Action buttons (powerOn/shutdown), slider (setPower)
- Agregar widget de alarmas activas al dashboard

### Investigación
- Captura Wireshark del EMS del fabricante en modo **self-use + backup power** — es el modo más relevante para la topología actual (red + batería → carga, sin PV)
- Verificar comportamiento de reg 353 en modo grid-tied vs off-grid
