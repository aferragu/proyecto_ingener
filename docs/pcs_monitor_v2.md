# pcs_monitor — Documentación

## Descripción general

Firmware para ESP32 que actúa como EMS (Energy Management System) de monitoreo y control para el inversor SinoSoar SP6030. Lee datos del inversor via Modbus RTU (RS-485), del BMS Pylontech via CAN bus, publica telemetría a ThingsBoard via MQTT sobre WiFi, y recibe comandos de control via RPC.

El proyecto está organizado como un proyecto PlatformIO en `esp32/pcs_monitor_pio/`. El sketch monolítico original queda en `esp32/pcs_monitor/` como referencia.

---

## Estructura del repositorio

```
esp32/
├── pcs_monitor/           # sketch Arduino original (referencia)
└── pcs_monitor_pio/       # proyecto PlatformIO (activo)
    ├── platformio.ini
    ├── include/           # headers
    ├── src/               # firmware de producción
    ├── test/              # tests unitarios (pio test -e native)
    └── sketches/          # sketches de prueba de hardware
```

### Entornos PlatformIO

| Entorno | Comando | Descripción |
|---|---|---|
| `esp32dev` | `pio run -e esp32dev -t upload` | Firmware de producción |
| `native` | `pio test -e native` | Tests unitarios en el host (sin hardware) |
| `test_display` | `pio run -e test_display -t upload` | Prueba pantalla ST7789 aislada |
| `test_mqtt` | `pio run -e test_mqtt -t upload` | Prueba WiFi + ThingsBoard aislada |
| `test_tb` | `pio run -e test_tb -t upload` | Dashboard simulado: pantalla + ThingsBoard |
| `test_set_power` | `pio run -e test_set_power -t upload` | Prueba setPower via shared attribute |
| `test_can_hw` | `pio run -e test_can_hw -t upload` | Prueba CAN / BMS → Serial |
| `test_dashboard` | `pio run -e test_dashboard -t upload` | Hardware real → pantalla + ThingsBoard |

---

## Hardware

### Board

**Ideaspark ESP32** con pantalla LCD TFT ST7789 integrada de 1.14" (135×240).

### Diagrama de conexiones

```
ESP32 GPIO17 (TX2) ──► MAX485 DI  ─┐
ESP32 GPIO16 (RX2) ◄── MAX485 RO   ├── RS-485 bus → Inversor SP6030
ESP32 GPIO5  (*)   ──► MAX485 DE+RE─┘

ESP32 GPIO21       ──► SN65HVD230 TX ─┐
ESP32 GPIO22       ◄── SN65HVD230 RX  ├── CAN bus → BMS Pylontech
             GND   ──► SN65HVD230 Rs  ┘

ESP32 GPIO23 (MOSI)──► LCD MOSI ─┐
ESP32 GPIO18 (SCLK)──► LCD SCLK  │
ESP32 GPIO15       ──► LCD CS    ├── ST7789 1.14" (integrada Ideaspark)
ESP32 GPIO2        ──► LCD DC    │
ESP32 GPIO4        ──► LCD RST   │
ESP32 GPIO32       ──► LCD BLK   ┘
```

> (*) `RS485_DE_RE_PIN` está definido como GPIO5 en `config.h`. **GPIO4 está reservado para LCD RST** en la placa Ideaspark y no puede usarse para RS-485. Al cablear el MAX485, conectar DE+RE a GPIO5.

### Pines ocupados

| GPIO | Función |
|---|---|
| 2 | LCD DC |
| 4 | LCD RST (hardwired en Ideaspark) |
| 5 | RS485 DE+RE |
| 15 | LCD CS |
| 16 | RS485 RX |
| 17 | RS485 TX |
| 18 | LCD SCLK |
| 21 | CAN TX |
| 22 | CAN RX |
| 23 | LCD MOSI |
| 32 | LCD backlight |

Libres para uso general: 12, 13, 14, 25, 26, 27, 33, 34 (input only), 35 (input only).

---

## Arquitectura del firmware

### Módulos de producción (`src/`)

```
src/
├── main.cpp          # setup() y loop() — orquestador
├── modbus.cpp        # capa hardware RS-485: UART send/receive
├── modbus_core.cpp   # lógica pura: CRC16, frame building, response parsing
├── inverter.cpp      # capa hardware: readFirmwareVersion, init, pollModbus
├── inverter_core.cpp # lógica pura: register scaling, init sequence
├── bms.cpp           # capa hardware: TWAI init, pollCAN
├── bms_core.cpp      # lógica pura: Pylontech CAN frame decoding
├── mqtt.cpp          # WiFi, MQTT, RPC handler, LED
├── ems.cpp           # lógica de control EMS (stub, pendiente)
└── display.cpp       # LCD ST7789 (pendiente integración)
```

Cada módulo está dividido en dos capas:

- **`_core.cpp`** — lógica pura sin dependencias de hardware. Puede compilarse en el host para tests unitarios.
- **`.cpp`** — capa hardware que delega la lógica a `_core` y maneja periféricos (UART, TWAI, WiFi).

### Headers de escalas (`include/`)

| Header | Fuente | Contenido |
|---|---|---|
| `inverter_scales.h` | SinoSoar PCS Modbus Protocol V3.0 | Factores de escala por tipo de registro |
| `bms_scales.h` | Pylontech CAN Bus Protocol V1.24 | Escalas y offsets de tramas CAN |

Estos headers son la única fuente de verdad para las conversiones de valores crudos a físicos. Si un factor de escala cambia, se corrige aquí y el cambio se propaga a producción y tests.

---

## Flujo de ejecución

### setup()
```
Boot
 ├── RS-485 init (UART2, 115200 bps, 8N1)
 ├── CAN init (TWAI, 500 kbps, 29-bit extended)
 ├── WiFi connect
 ├── MQTT connect + subscribe RPC
 ├── readFirmwareVersion() → publica atributos ThingsBoard
 ├── inverterInit() → escribe secuencia de configuración
 ├── pollModbus() + pollCAN() → primera lectura
 └── publishTelemetry() → primera publicación
```

### loop()
```
Loop continuo
 ├── mqtt.loop()           continuo — procesa RPC entrantes
 ├── WiFi reconnect        si desconectado
 ├── updateLed()           continuo
 ├── verifyAndReinit()     cada 60s
 ├── pollModbus()          cada 5s
 ├── pollCAN()             cada 1s
 └── publishTelemetry()    cada 10s
```

---

## Módulos

### modbus / modbus_core

Implementación manual Modbus RTU sobre RS-485:
- FC 0x03 — Read Holding Registers
- FC 0x06 — Preset Single Register
- CRC16 polinomio 0xA001, byte bajo primero
- Timeout en dos fases: espera primer byte 50ms, drena 20ms más

`modbus_core` expone: `crc16()`, `modbus_build_read()`, `modbus_build_write()`, `modbus_parse_read()`, `modbus_parse_write()`.

### inverter / inverter_core

**Secuencia de init** (replicada del EMS del fabricante via Wireshark):

| Registro | Valor | Descripción |
|---|---|---|
| 763 | 1500 | Max corriente descarga DC = 150 A |
| 764 | 1500 | Max corriente carga DC = 150 A |
| 341 | 1 | Control por fase individual |
| 652 | 0 | PV apagado |
| 795 | 0 | Detección de fuga deshabilitada |
| 656 | 0 | DCDC apagado |
| 873 | bit0=1 | Anti-backflow (**read-modify-write**) |

> ⚠ Power ON (reg 650) no está en el init automático — debe ejecutarse via RPC `powerOn`.

**Bloques Modbus leídos en cada ciclo:**

| Bloque | Registros | Keys ThingsBoard |
|---|---|---|
| Estado | 32 | running, fault, alarm, grid_tied, off_grid, derating, standby |
| AC inversor | 100–125 | freq_hz, v_a/b/c, i_a/b/c, p_a/b/c_kw, q_a/b/c_kvar, pf_total, p_inv_kw |
| DC | 141–143 | dc_power_kw, dc_voltage_v, dc_current_a |
| Red | 170–179, 192 | grid_freq_hz, grid_v_a/b/c, grid_p_kw |
| Carga (V3.0) | 200–213 | load_p_kw, load_s_kva, load_v/i_a/b/c |

> ⚠ Registros 200–213 requieren firmware RTU ≥ V3.0 (reg 19 ≥ 30).

**Factores de escala** (`inverter_scales.h`):

| Constante | Valor | Aplica a |
|---|---|---|
| `SCALE_FREQ_HZ` | 0.01 | Frecuencia |
| `SCALE_VOLTAGE_V` | 0.1 | Tensiones AC y DC |
| `SCALE_CURRENT_A` | 0.1 | Corrientes AC y DC |
| `SCALE_POWER_KW` | 0.01 | Potencias activa y reactiva (lectura) |
| `SCALE_PF` | 0.01 | Factor de potencia |
| `SCALE_SET_POWER_KW` | 0.1 | Reg 135 setPower (escritura, distinto del resto) |

### bms / bms_core

Hardware: **LWS 16S300A** (Huizhou LWS New Energy Technology), not Pylontech. The CAN protocol is compatible with the Pylontech High Voltage V1.24 spec.

Protocolo confirmado por captura CANalyzer:
- **500 kbps**, tramas extendidas **29 bits**
- **Byte order: LSB first** (little-endian), confirmado por el manual
- Dirección configurada en **panel frontal** — actualmente addr=2, `BMS_CAN_ADDR` debe actualizarse antes de conectar

| CAN ID | Contenido |
|---|---|
| 0x4210+addr | Tensión pack, corriente, temperatura, SOC, SOH |
| 0x4220+addr | Límites carga/descarga (tensión y corriente máxima) |
| 0x4250+addr | Estado, fault, alarm, protection |
| 0x4280+addr | Flags forbidden, SOE |

**Factores de escala** (`bms_scales.h`):

| Campo | Scale | Offset |
|---|---|---|
| Tensión (0x4210) | 0.1 V | 0 |
| Corriente (0x4210, 0x4220) | 0.1 A | −3000 A |
| Temperatura (0x4210) | 0.1 °C | −100 °C |
| Tensión de corte (0x4220) | 0.1 V | 0 |
| Forbidden mark (0x4280) | — | 0xAA = forbidden |

### mqtt

- Reconexión automática WiFi y MQTT en el loop
- Buffer MQTT: 2048 bytes
- Client ID único basado en MAC
- `setServer/setBufferSize/setCallback` se inicializan una sola vez

**RPCs soportados:**

| Método | Params | Registro | Descripción |
|---|---|---|---|
| `powerOn` | — | 650 | Arranque general |
| `shutdown` | — | 651 | Parada general |
| `setPower` | `{"value": X}` | 135 | Setpoint potencia activa (kW, −100..+100, escala 0.1 kW) |

### ems

Stub con lógica propuesta comentada. Parámetros previstos:

| Parámetro | Valor default | Descripción |
|---|---|---|
| `EMS_SOC_MIN` | 20% | No descargar por debajo |
| `EMS_SOC_TARGET` | 90% | Cargar hasta aquí cuando no hay carga |
| `EMS_MAX_DISCHARGE` | 20 kW | Límite de descarga |
| `EMS_CHARGE_POWER` | 10 kW | Potencia de carga desde red |

---

## Tests unitarios

Ubicación: `test/`. Se ejecutan en el host sin hardware con `pio test -e native`.

| Suite | Cubre |
|---|---|
| `test_modbus` | CRC16, frame building, response parsing, escalas de registro |
| `test_bms` | Decodificación de tramas CAN con valores reales del protocolo |
| `test_ems` | Lógica de decisión EMS con mock de writeRegister |

Los tests llaman directamente a las funciones `_core` — no duplican lógica. Un cambio en `modbus_core.cpp` o `bms_core.cpp` se refleja inmediatamente en los tests.

> Durante el desarrollo se detectó con los tests un bug real: `setPower` usaba escala 0.01 (×100) en lugar de 0.1 (×10), lo que habría enviado setpoints 10× demasiado altos al inversor.

---

## Sketches de prueba de hardware

Ubicación: `sketches/`. Cada uno es un firmware independiente para verificar un subsistema sin el resto. Todos usan `config.h` para pines y direcciones, y `credentials.h` para WiFi/token cuando aplica. La lógica de protocolo viene de `lib/` — cambiar `modbus_core`, `inverter_core` o `bms_core` se refleja en todos automáticamente.

| Sketch | Display | WiFi/TB | Modbus | CAN | Propósito |
|---|---|---|---|---|---|
| `test_display` | ✓ dummy | — | — | — | Verificar pantalla y layout |
| `test_mqtt` | — | ✓ dummy | — | — | Verificar WiFi y ThingsBoard |
| `test_tb` | ✓ simulado | ✓ simulado | — | — | Construir y validar dashboard |
| `test_modbus_hw` | — | — | ✓ real | — | Verificar comunicación con inversor |
| `test_can_hw` | — | — | — | ✓ real | Verificar comunicación con BMS |
| `test_set_power` | — | ✓ real | ✓ real | — | Probar control de potencia end-to-end |
| `test_dashboard` | ✓ real | ✓ real | flag | flag | Integración completa con HW real |

### test_display
Cicla 3 pantallas cada 3 segundos con datos hardcodeados: Status (indicadores), Power Flow, Battery. Sin WiFi ni hardware. Útil para verificar el cableado del display y el layout antes de integrar datos reales.

### test_mqtt
Conecta WiFi, publica telemetría dummy a ThingsBoard cada 5s, escucha y responde RPCs. Sin Modbus ni CAN ni display. Útil para verificar credenciales y conectividad de red de forma aislada.

### test_tb
Combina pantalla + ThingsBoard con datos simulados que derivan lentamente. Publica todos los keys que el firmware real publica. Usar para construir y ajustar el dashboard de ThingsBoard sin necesitar hardware. La pantalla cicla las mismas 3 pantallas mostrando los valores simulados y el estado de WiFi/MQTT.

### test_set_power
Prueba el control de potencia end-to-end: ThingsBoard knob → shared attribute `set_power` → reg 135 inversor → telemetría de confirmación.

Al arrancar lee el valor actual de `set_power` desde ThingsBoard y lo aplica. Cuando el knob cambia, el ESP32 recibe la notificación via MQTT y escribe inmediatamente en reg 135.

Publica cada 5s:
- `set_power_requested` — valor recibido desde el knob
- `set_power_active` — valor leído de vuelta del reg 135 (confirmación)
- `p_inv_kw` — potencia AC de salida
- `dc_power_kw` — potencia DC
- `dc_current_a` — corriente DC

Graficar `set_power_requested` vs `set_power_active` permite ver el lag y confirmar que el inversor responde. `dc_current_a` es el indicador más sensible para ver si la batería está cargando o descargando.

### test_modbus_hw
Conecta al inversor via RS-485 y vuelca todos los bloques de registros por Serial cada 5 segundos con valores escalados: Status, AC, DC, Grid, Load + dump crudo de registros 0–9. Sin WiFi ni display. Si todos los bloques fallan, el boot imprime exactamente qué verificar.

### test_can_hw
Escucha el bus CAN en modo normal (con ACK) e imprime los primeros 50 frames en hex crudo. A partir de ahí imprime un resumen decodificado cada vez que llega el frame principal del BMS (`0x4210+addr`). Imprime decode completo al primer frame válido. Sin WiFi ni display.

### test_dashboard
Firmware de integración — el más cercano al firmware de producción pero sin control ni RPC. Dos flags independientes:
- `MODBUS_ENABLED` — lee inversor via RS-485
- `CAN_ENABLED` — lee BMS via CAN

Si un flag está en 0, esa fuente se omite y el punto correspondiente en la pantalla queda rojo. No se publican datos simulados en su lugar — lo que no funciona se ve claramente. Útil para validar el hardware en etapas: primero solo Modbus, después agregar CAN cuando llegue el converter.

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
#define MODBUS_DEVICE_ID 1      // confirmar con DIP switch del inversor
#define RS485_DE_RE_PIN  5      // GPIO5 — NO usar GPIO4 (reservado LCD RST en Ideaspark)
#define BMS_CAN_ADDR     1      // confirmar con configuración del BMS
#define CAN_SPEED        TWAI_TIMING_CONFIG_500KBITS()

#define POLL_MODBUS_MS   5000   // poll inversor cada 5s
#define POLL_CAN_MS      1000   // poll BMS cada 1s
#define PUBLISH_MS       10000  // publicación ThingsBoard cada 10s
#define VERIFY_INIT_MS   60000  // verificación config inversor cada 60s
```

---

## Dependencias

| Librería | Uso |
|---|---|
| PubSubClient (Nick O'Leary) | MQTT |
| ArduinoJson (Benoit Blanchon) | JSON payload |
| Adafruit ST7789 + Adafruit GFX | Display |
| driver/twai.h | CAN bus (incluida en ESP32 Arduino core) |

---

## Failsafe y robustez

- Si el ESP32 falla, el inversor sigue operando con los últimos parámetros escritos
- `verifyAndReinit()` restaura la config si el inversor se reinicia
- Reconexión automática de WiFi y MQTT en el loop
- ⚠ Conocido: `connectMQTT()` puede bloquearse si el broker no responde — pendiente timeout explícito

---

## TODO

### Hardware
- Cablear MAX485 DE+RE a **GPIO5** (no GPIO4)
- Confirmar dirección Modbus del inversor via DIP switch → `MODBUS_DEVICE_ID`
- `BMS_CAN_ADDR` actualmente en 1 — cambiar a 2 cuando se conecte el CAN converter (dirección configurada en panel frontal)
- Conseguir módulo CAN-TTL (SN65HVD230) para pruebas BMS
- Verificar firmware inversor RTU ≥ V3.0 (reg 19 ≥ 30)

### Firmware
- Integrar display en firmware de producción (`main.cpp` + `display.cpp`)
- Activar y validar `emsUpdate()` con estrategia acordada
- Agregar shared attributes ThingsBoard para configurar EMS remotamente sin reflashear
- Resolver bloqueo de `connectMQTT()` con timeout explícito
- Migrar a FreeRTOS dual-core (Core 0: MQTT/WiFi, Core 1: Modbus/CAN/EMS)
- ⚠ Renombrar `bms_current_a` → `bms_current` en telemetría — el sufijo `_a` es ambiguo (puede confundirse con fase A en el lado AC)

### ThingsBoard
- Configurar dashboard con datos reales via `test_dashboard`
- Agregar widgets RPC: botones powerOn/shutdown, slider setPower
- Conectar rule chain de alarmas al Root Rule Chain

### Investigación
- Captura Wireshark del EMS del fabricante en modo **self-use + backup power**
- Verificar comportamiento de reg 353 en modo grid-tied vs off-grid
