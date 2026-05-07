# pcs_monitor — Documentación

## Descripción general

Firmware para ESP32 que actúa como EMS (Energy Management System) de monitoreo y control para el inversor SinoSoar SP6030. Lee datos del inversor via Modbus RTU (RS-485), del BMS LWS 16S300A via CAN bus, publica telemetría a ThingsBoard via MQTT sobre WiFi, y recibe comandos de control via RPC y shared attributes.

El proyecto está organizado como un proyecto PlatformIO en `esp32/pcs_monitor_pio/`. El sketch monolítico original queda en `esp32/pcs_monitor/` como referencia.

---

## Estructura del repositorio

```
esp32/
├── pcs_monitor/           # sketch Arduino original (referencia)
└── pcs_monitor_pio/       # proyecto PlatformIO (activo)
    ├── platformio.ini
    ├── include/           # headers globales (config.h, credentials.h, scales)
    ├── lib/               # módulos puros — sin dependencias Arduino/ESP-IDF
    │   ├── modbus_core/   # CRC16, frame building, response parsing
    │   ├── inverter_core/ # register scaling, init sequence, inverter_run_init
    │   └── bms_core/      # Pylontech/LWS CAN frame decoding
    ├── src/               # firmware de producción (capas hardware)
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
| `test_modbus_hw` | `pio run -e test_modbus_hw -t upload` | Prueba Modbus real → Serial |
| `test_can_hw` | `pio run -e test_can_hw -t upload` | Prueba CAN / BMS → Serial |
| `test_set_power` | `pio run -e test_set_power -t upload` | Control de potencia end-to-end |
| `test_dashboard` | `pio run -e test_dashboard -t upload` | Hardware real → pantalla + ThingsBoard |

---

## Hardware

### Board

**Ideaspark ESP32** con pantalla LCD TFT ST7789 integrada de 1.14" (135×240).

### Diagrama de conexiones

```
ESP32 GPIO17 (TX2) ──► MAX485 DI  ─┐
ESP32 GPIO16 (RX2) ◄── MAX485 RO   ├── RS-485 bus → Inversor SP6030
ESP32 GPIO5        ──► MAX485 DE+RE─┘

ESP32 GPIO21       ──► CAN TX ─┐
ESP32 GPIO22       ◄── CAN RX  ├── CAN bus → BMS LWS 16S300A
                               ┘   (módulo MCP2515+TJA1050 o SN65HVD230)

ESP32 GPIO23 (MOSI)──► LCD MOSI ─┐
ESP32 GPIO18 (SCLK)──► LCD SCLK  │
ESP32 GPIO15       ──► LCD CS    ├── ST7789 1.14" (integrada Ideaspark)
ESP32 GPIO2        ──► LCD DC    │
ESP32 GPIO4        ──► LCD RST   │
ESP32 GPIO32       ──► LCD BLK   ┘
```

> **GPIO4 está hardwired a LCD RST en la placa Ideaspark** — no puede usarse para RS-485. DE+RE del MAX485 va a **GPIO5**.

### Pines ocupados

| GPIO | Función |
|---|---|
| 2 | LCD DC |
| 4 | LCD RST (hardwired Ideaspark) |
| 5 | RS485 DE+RE |
| 15 | LCD CS |
| 16 | RS485 RX |
| 17 | RS485 TX |
| 18 | LCD SCLK |
| 21 | CAN TX |
| 22 | CAN RX |
| 23 | LCD MOSI |
| 32 | LCD backlight |

Libres: 12, 13, 14, 25, 26, 27, 33, 34 (input only), 35 (input only).

---

## Arquitectura del firmware

### lib/ — módulos puros

Los módulos en `lib/` no tienen dependencias de Arduino ni ESP-IDF. Se compilan para cualquier entorno incluyendo `native` (tests en el host).

| Módulo | Contenido |
|---|---|
| `modbus_core` | `crc16()`, `modbus_build_read/write()`, `modbus_parse_read/write()` |
| `inverter_core` | `inverter_parse_ac/dc/grid/load/status()`, `inverter_run_init()`, `inverter_init_sequence()` |
| `bms_core` | `bms_decode()` — decodifica tramas CAN del BMS |

`inverter_run_init(write_fn, read_fn)` acepta punteros a función — se puede llamar desde el firmware de producción y desde cualquier sketch sin duplicar lógica.

### src/ — capas hardware

```
src/
├── main.cpp       # setup() y loop() — orquestador
├── modbus.cpp     # RS-485 UART → llama modbus_core
├── inverter.cpp   # readFirmwareVersion, inverterInit, pollModbus, verifyAndReinit
├── bms.cpp        # TWAI init/poll → llama bms_core
├── mqtt.cpp       # WiFi, MQTT, RPC handler, LED
├── ems.cpp        # lógica EMS (stub, pendiente)
└── display.cpp    # LCD ST7789 (pendiente integración)
```

### Headers de escalas

| Header | Fuente | Contenido |
|---|---|---|
| `inverter_scales.h` | SinoSoar PCS Modbus Protocol V3.0 | Factores de escala por tipo de registro |
| `bms_scales.h` | LWS/Pylontech CAN Bus Protocol V1.24 | Escalas y offsets de tramas CAN |

Fuente única de verdad para conversiones raw → físico. Un cambio se propaga a producción, sketches y tests automáticamente.

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
 ├── inverterInit() → llama inverter_run_init()
 ├── pollModbus() + pollCAN() → primera lectura
 └── publishTelemetry() → primera publicación
```

### loop()
```
Loop continuo
 ├── mqtt.loop()        continuo — procesa RPC y shared attributes
 ├── WiFi reconnect     si desconectado
 ├── updateLed()        continuo
 ├── verifyAndReinit()  cada 60s
 ├── pollModbus()       cada 5s
 ├── pollCAN()          cada 1s
 └── publishTelemetry() cada 10s
```

---

## Módulos

### modbus / modbus_core

Modbus RTU sobre RS-485, implementación manual:
- FC 0x03 — Read Holding Registers
- FC 0x06 — Preset Single Register
- CRC16 polinomio 0xA001, byte bajo primero
- Timeout dos fases: primer byte 50ms, drena 20ms más

### inverter / inverter_core

**Secuencia de init** — `inverter_run_init()` en `inverter_core`, llamada desde `inverterInit()` y desde `test_set_power`:

| Registro | Valor | Descripción |
|---|---|---|
| 763 | 1500 | Max corriente descarga DC = 150 A |
| 764 | 1500 | Max corriente carga DC = 150 A |
| 341 | 1 | Control por fase individual |
| 652 | 0 | PV apagado |
| 795 | 0 | Detección de fuga deshabilitada |
| 656 | 0 | DCDC apagado |
| 873 | bit0=1 | Anti-backflow (read-modify-write) |

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
| `SCALE_SET_POWER_KW` | 0.1 | Reg 135 setPower (escritura — escala distinta) |

### bms / bms_core

Hardware: **LWS 16S300A** (Huizhou LWS New Energy Technology). Protocolo compatible con Pylontech High Voltage CAN Bus V1.24.

Confirmado por captura CANalyzer:
- **500 kbps**, tramas extendidas **29 bits**
- **Byte order: LSB first** (little-endian), confirmado por manual
- Dirección configurada en panel frontal — actualmente **addr=2**, `BMS_CAN_ADDR` en `config.h` debe ser 2

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

**RPCs soportados:**

| Método | Params | Registro | Descripción |
|---|---|---|---|
| `powerOn` | — | 650 | Arranque general |
| `shutdown` | — | 651 | Parada general |
| `setPower` | `{"value": X}` | 135 | Setpoint potencia activa (kW, −100..+100) |

**Shared attributes escuchados:**

| Atributo | Tipo | Descripción |
|---|---|---|
| `set_power` | float | Setpoint de potencia desde ThingsBoard (kW) |

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

Los tests llaman directamente a las funciones `_core` de `lib/` — no duplican lógica.

> Durante el desarrollo los tests detectaron un bug real: `setPower` usaba escala 0.01 en lugar de 0.1, lo que habría enviado setpoints 10× demasiado altos al inversor.

---

## Sketches de prueba de hardware

Ubicación: `sketches/`. Cada uno es un firmware independiente para verificar un subsistema sin el resto. Todos usan `config.h` para pines y direcciones, `credentials.h` para WiFi/token, y `lib/` para la lógica de protocolo.

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
Cicla 3 pantallas cada 3s con datos hardcodeados: Status, Power Flow, Battery. Sin WiFi ni hardware. Verifica cableado del display y layout.

### test_mqtt
Conecta WiFi, publica telemetría dummy a ThingsBoard cada 5s, escucha y responde RPCs. Sin Modbus ni CAN ni display. Verifica credenciales y conectividad de red.

### test_tb
Pantalla + ThingsBoard con datos simulados que derivan lentamente. Publica todos los keys del firmware real. Usar para construir y ajustar el dashboard de ThingsBoard sin hardware.

### test_modbus_hw
Conecta al inversor via RS-485 y vuelca todos los bloques a Serial cada 5s con valores escalados: Status, AC, DC, Grid, Load + dump crudo de registros 0–9. Si todos los bloques fallan, el boot imprime qué verificar.

### test_can_hw
Escucha el bus CAN en modo normal (con ACK) e imprime los primeros 50 frames en hex crudo. Después imprime resumen decodificado en cada frame `0x4210+addr` y decode completo al primer frame válido.

### test_set_power
Prueba el control de potencia end-to-end: ThingsBoard knob → shared attribute `set_power` → reg 135 → telemetría de confirmación.

Al arrancar: corre la secuencia de init del inversor (`inverter_run_init`), lee el `set_power` actual de ThingsBoard y lo aplica. Al cambiar el knob: aplica el nuevo valor inmediatamente.

Rango del testbed: **−2 a +2 kW** (cambiar a −100/+100 para producción).

Publica cada 5s:

| Key | Descripción |
|---|---|
| `set_power_requested` | Valor recibido del knob |
| `set_power_active` | Valor leído de vuelta del reg 135 |
| `p_inv_kw` | Potencia AC de salida del inversor |
| `dc_power_kw` | Potencia DC (batería) |
| `dc_current_a` | Corriente DC — indicador más sensible |
| `grid_p_kw` | Potencia importada de la red |
| `load_p_kw` | Potencia consumida por la carga |

Caso de prueba típico: `set_power=1`, carga=1kW → esperado `p_inv_kw≈1`, `grid_p_kw≈0`, `dc_current_a>0`.

### test_dashboard
Firmware de integración sin control ni RPC. Flags independientes:
- `MODBUS_ENABLED` — lee inversor via RS-485
- `CAN_ENABLED` — lee BMS via CAN

Si un flag está en 0, esa fuente se omite y el punto en la pantalla queda rojo. No hay datos simulados como fallback. Útil para validar en etapas: primero Modbus, después CAN.

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
#define RS485_DE_RE_PIN  5      // GPIO5 — NO usar GPIO4 (LCD RST en Ideaspark)
#define BMS_CAN_ADDR     2      // dirección configurada en panel frontal del BMS
#define CAN_SPEED        TWAI_TIMING_CONFIG_500KBITS()
#define TB_HOST          "thingsboard.cloud"
#define TB_PORT          1883

#define POLL_MODBUS_MS   5000
#define POLL_CAN_MS      1000
#define PUBLISH_MS       10000
#define VERIFY_INIT_MS   60000
```

---

## Dependencias

| Librería | Uso |
|---|---|
| PubSubClient (Nick O'Leary) | MQTT |
| ArduinoJson (Benoit Blanchon) | JSON payload — usar v7 (`JsonDocument`) |
| Adafruit ST7789 + Adafruit GFX | Display |
| driver/twai.h | CAN bus (incluida en ESP32 Arduino core) |

---

## Failsafe y robustez

- Si el ESP32 falla, el inversor sigue operando con los últimos parámetros escritos
- `verifyAndReinit()` restaura la config si el inversor se reinicia
- `inverter_run_init()` disponible en `lib/` — cualquier sketch puede inicializar el inversor correctamente
- Reconexión automática WiFi y MQTT en el loop
- ⚠ Conocido: `connectMQTT()` puede bloquearse si el broker no responde — pendiente timeout explícito

---

## TODO

### Hardware pendiente
- Cablear MAX485 DE+RE a **GPIO5**
- Confirmar dirección Modbus del inversor via DIP switch → `MODBUS_DEVICE_ID` en `config.h`
- `BMS_CAN_ADDR` = 2 en `config.h` (ya configurado en panel frontal del BMS)
- Conseguir módulo CAN (MCP2515+TJA1050 en camino) para pruebas BMS
- Verificar firmware inversor RTU ≥ V3.0 (reg 19 ≥ 30) para registros de carga 200–213

### Pruebas de hardware pendientes
- `test_modbus_hw` — verificar lectura de registros del inversor
- `test_set_power` — verificar control de potencia con carga real
- `test_can_hw` — verificar decodificación de tramas BMS
- `test_dashboard` — integración completa Modbus + display + ThingsBoard

### Firmware
- Integrar display en firmware de producción (`main.cpp` + `display.cpp`)
- Activar y validar `emsUpdate()` con estrategia acordada
- Agregar shared attributes ThingsBoard para configurar parámetros EMS remotamente
- Resolver bloqueo de `connectMQTT()` con timeout explícito
- Migrar a FreeRTOS dual-core (Core 0: MQTT/WiFi, Core 1: Modbus/CAN/EMS)
- ⚠ Renombrar `bms_current_a` → `bms_current` en telemetría (sufijo `_a` ambiguo con fase A del lado AC)

### ThingsBoard
- Dashboard de producción final (post pruebas de hardware)
- Agregar widgets RPC: botones powerOn/shutdown
- Conectar rule chain de alarmas al Root Rule Chain
- Investigar protocolo SacredSun General V1.34 (RS-485 del BMS) para eventual migración de CAN a RS-485

### Investigación
- Captura Wireshark del EMS del fabricante en modo self-use + backup power
- Verificar comportamiento de reg 353 en modo grid-tied vs off-grid
