# pcs_control — Documentación

## Descripción general

Firmware para ESP32 que actúa como EMS (Energy Management System) para inversor SinoSoar SP6030 y BMS LWS 16S300A. Lee datos vía Modbus RTU (RS-485) de ambos dispositivos en bus compartido, publica telemetría a ThingsBoard vía MQTT sobre WiFi, y recibe comandos de control vía RPC y shared attributes.

El proyecto está organizado como un proyecto PlatformIO en `esp32/pcs_monitor_pio/`.

---

## Estructura del repositorio

```
esp32/pcs_monitor_pio/
├── platformio.ini
├── include/              # headers globales (config.h, credentials.h)
├── lib/                  # módulos reutilizables
│   ├── inverter_parser/  # parsing puro registros SP6030 — sin Arduino, testeable en native
│   ├── inverter/         # capa hardware inversor — ModbusMaster FC03/FC06
│   ├── bms_parser/       # parsing puro registros LWS — sin Arduino, testeable en native
│   └── bms/              # capa hardware BMS — ModbusMaster FC04
├── src/                  # firmware de producción
│   ├── main.cpp          # setup() y loop() — orquestador
│   ├── mqtt.cpp          # WiFi, MQTT, RPC handler, LED
│   ├── ems.cpp           # lógica EMS (stub, pendiente activar)
│   └── display.cpp       # LCD ST7789
├── test/                 # tests unitarios (pio test -e native)
│   ├── test_bms/         # parseo registros LWS
│   ├── test_ems/         # lógica de decisión EMS
│   └── test_inverter/    # escalas y parseo registros SP6030
└── sketches/             # sketches de prueba de hardware
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
| `test_set_power` | `pio run -e test_set_power -t upload` | Control de potencia end-to-end |
| `test_dashboard` | `pio run -e test_dashboard -t upload` | Hardware real → pantalla + ThingsBoard |

---

## Hardware

### Board

**Ideaspark ESP32** con pantalla LCD TFT ST7789 integrada de 1.14" (135×240).

### Diagrama de conexiones

```
ESP32 GPIO17 (TX2) ──► MAX485 DI  ─┐
ESP32 GPIO16 (RX2) ◄── MAX485 RO   ├── RS-485 bus compartido
ESP32 GPIO5        ──► MAX485 DE+RE─┘   → Inversor SP6030 (ID: MODBUS_DEVICE_ID)
                                        → BMS LWS 16S300A  (ID: BMS_MODBUS_DEVICE_ID)

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
| 23 | LCD MOSI |
| 32 | LCD backlight |

Libres: 12, 13, 14, 21, 22, 25, 26, 27, 33, 34 (input only), 35 (input only).

---

## Arquitectura del firmware

### Separación de capas

```
lib/inverter_parser/  — parsing puro SP6030: parse_ac/dc/grid/load/status, init_sequence
lib/inverter/         — hardware: ModbusMaster FC03/FC06, inverterInit(serial, deRePin)
lib/bms_parser/       — parsing puro LWS: bms_parse_modbus(), BmsData
lib/bms/              — hardware: ModbusMaster FC04, bmsInit(serial, deRePin)
src/                  — main, mqtt, ems, display — el pegamento
```

Los `_parser` no dependen de Arduino — compilables en `native` para tests. Las capas `inverter/` y `bms/` instancian su propio `ModbusMaster` y reciben el serial ya inicializado como parámetro — el bus RS-485 es un recurso externo.

### Flujo de ejecución

#### setup()
```
Boot
 ├── Serial2.begin(115200)          — RS-485 compartido
 ├── inverterInit(Serial2, GPIO5)   — init Modbus + secuencia init SP6030
 ├── bmsInit(Serial2, GPIO5)        — init Modbus BMS (mismo bus, distinto device ID)
 ├── WiFi connect
 ├── MQTT connect + subscribe RPC
 ├── readFirmwareVersion()          — publica atributos ThingsBoard
 ├── pollModbus() + pollBMS()       — primera lectura
 └── publishTelemetry()             — primera publicación
```

#### loop()
```
Loop continuo
 ├── mqtt.loop()        continuo — procesa RPC y keepalive
 ├── WiFi reconnect     si desconectado
 ├── updateLed()        continuo
 ├── verifyAndReinit()  cada 60s
 ├── pollModbus()       cada 5s
 ├── pollBMS()          cada 2s
 └── publishTelemetry() cada 10s
```

---

## Módulos

### inverter_parser / inverter

**Bus compartido** con el BMS. Inversor SP6030 responde en `MODBUS_DEVICE_ID` (config.h).

**Protocolo:** SinoSoar PCS Modbus V3.0 — FC03 lectura, FC06 escritura, 115200 bps.

**Secuencia de init** (ejecutada en `inverterInit()`):

| Paso | Registro | Valor | Descripción |
|---|---|---|---|
| 1 | 763 | 1500 | Max corriente descarga DC = 150 A |
| 2 | 764 | 1500 | Max corriente carga DC = 150 A |
| 3 | 873 | 0 | Self-use mode OFF → habilita control por reg 135 |
| 4 | 758 | 0 | AC side constant power mode — habilita reg 135 como setpoint |
| 5 | 341 | 1 | Control por fase individual |
| 6 | 652 | 0 | PV apagado |
| 7 | 795 | 0 | Detección de fuga deshabilitada |
| 8 | 656 | 0 | DCDC apagado |
| 9 | 135 | 0 | Setpoint = 0 kW antes de encender |
| 10 | 650 | 1 | Power ON |

> **reg 873 = 0 es crítico** — si está en 1 (self-use mode), el inversor gestiona la carga/descarga de la batería internamente y el reg 135 no tiene efecto. El EMS oficial del fabricante usa 873=1 con reg 353; nosotros operamos on-grid con EMS externo vía reg 135.

> **reg 758 = 0** es condición necesaria para que reg 135 funcione (AC side constant power mode). Aplica al SP60HBG2 — confirmado en protocolo V2.2.0.

> **reg 334** (BMS power scheduling) se deja en 0 — el EMS externo controla todo, sin que el inversor hable directamente con el BMS.

**Bloques Modbus leídos en cada ciclo:**

| Bloque | Registros | Keys ThingsBoard |
|---|---|---|
| Estado | 32 | running, fault, alarm, grid_tied, off_grid, derating, standby |
| AC inversor | 100–125 | freq_hz, v_a/b/c, i_a/b/c, p_a/b/c_kw, q_a/b/c_kvar, pf_total, p_inv_kw |
| DC | 141–143 | dc_power_kw, dc_voltage_v, dc_current_a |
| Red | 170–179, 192 | grid_freq_hz, grid_v_a/b/c, grid_p_kw |
| Carga (V3.0) | 200–213 | load_p_kw, load_s_kva, load_v/i_a/b/c |

> Registros 200–213 requieren firmware RTU ≥ V3.0 (reg 19 ≥ 30).

**API pública:**

| Función | Descripción |
|---|---|
| `inverterInit(serial, deRePin)` | Init Modbus + secuencia arranque |
| `pollModbus(telemetry)` | Lee todos los bloques, llena JsonDocument |
| `inverterSetPower(kw)` | Escribe setpoint AC reg 135 (0.1 kW precisión) |
| `inverterPowerOn()` | reg 650 = 1 |
| `inverterShutdown()` | reg 650 = 0 |
| `inverterReadRaw(reg, out)` | Lee un registro individual — uso diagnóstico |
| `verifyAndReinit()` | Verifica y corrige registros de config |
| `readFirmwareVersion(mqtt)` | Lee versión, publica como atributos TB |

> **reg 758** — el protocolo V3.0 no lo lista para el SP60HBG2, pero el V2.2.0 sí lo incluye para todos los modelos. Pendiente verificar en hardware. Si no responde, el modo AC constant power puede ser el default y el reg 135 funciona igual.

> **INVERTER_PROTOCOL_V3** — definir en `config.h` para habilitar la lectura de regs 200–213 (load data). Requiere firmware RTU ≥ V3.0 (reg 19 ≥ 30). Con firmware V2.88 dejar comentado — si se intenta leer, el inversor puede no responder y contaminar el ciclo de polling.

### bms_parser / bms

**Bus compartido** con el inversor. BMS LWS responde en `BMS_MODBUS_DEVICE_ID` (config.h, default 51 — configurable por DIP switch en la batería, rango 51–65).

**Protocolo:** LWS Modbus Communication Protocol V1.36 — FC04 (Input Registers), 115200 bps (configurar en el BMS antes de conectar al bus).

**Registros leídos:**

| Registro | Campo | Tipo | Escala |
|---|---|---|---|
| 0x1000 | `voltage_v` | UINT16 | ×0.01 V |
| 0x1001 | `current_a` | INT16 | ×0.01 A (negativo = descarga) |
| 0x1003 | `temp_avg_c` | INT16 | ×0.1 °C |
| 0x1005 | `alarm` | HEX | bitfield |
| 0x1006 | `protection` | HEX | bitfield |
| 0x1007 | `fault` + status | HEX | byte0=fault, byte1=status |
| 0x1008 | `soc_pct` | UINT16 | ×0.1 % |
| 0x1009 | `soh_pct` | UINT16 | ×0.1 % |
| 0x100D | `cell_voltage_max_v` | UINT16 | ×0.001 V |
| 0x100E | `cell_voltage_min_v` | UINT16 | ×0.001 V |
| 0x1010 | `temp_cell_max_c` | INT16 | ×0.1 °C |
| 0x1011 | `temp_cell_min_c` | INT16 | ×0.1 °C |
| 0x1012 | `temp_fet_c` | INT16 | ×0.1 °C |
| 0x101D | `charge_cutoff_v` | UINT16 | ×0.01 V |
| 0x1020 | `discharge_cutoff_v` | UINT16 | ×0.01 V |
| 0x2500 | `max_charge_a` | UINT16 | ×0.1 A |
| 0x2501 | `max_discharge_a` | UINT16 | ×0.1 A |

**Flags derivados del reg 0x1007 byte1:**

| Bit | Campo | Descripción |
|---|---|---|
| 0 | `charging` | Estado de carga activo |
| 1 | `discharging` | Estado de descarga activo |
| 2 | `charge_forbidden` | MOSFET carga OFF → carga prohibida |
| 3 | `discharge_forbidden` | MOSFET descarga OFF → descarga prohibida |

**`force_charge_req`** — derivado de reg 0x1005 byte1 bit3 (low SOC alarm).

**Keys ThingsBoard publicados por `pollBMS()`:**
`bms_voltage_v`, `bms_current_a`, `bms_soc_pct`, `bms_soh_pct`, `bms_temp_avg_c`, `bms_temp_cell_max_c`, `bms_temp_cell_min_c`, `bms_temp_fet_c`, `bms_cell_v_max`, `bms_cell_v_min`, `bms_max_charge_a`, `bms_max_discharge_a`, `bms_charge_cutoff_v`, `bms_discharge_cutoff_v`, `bms_charging`, `bms_discharging`, `bms_charge_forbidden`, `bms_discharge_forbidden`, `bms_force_charge`, `bms_fault`, `bms_alarm`, `bms_protection`

**API pública:**

| Función | Descripción |
|---|---|
| `bmsInit(serial, deRePin)` | Init Modbus — no requiere secuencia de arranque |
| `pollBMS(telemetry)` | Lee todos los registros, llena JsonDocument |

### mqtt

- Reconexión automática WiFi y MQTT en el loop
- Buffer MQTT: 2048 bytes
- Client ID único basado en MAC

**RPCs soportados:**

| Método | Params | Descripción |
|---|---|---|
| `powerOn` | — | reg 650 = 1 |
| `shutdown` | — | reg 650 = 0 |
| `setPower` | `{"value": X}` | Setpoint potencia AC kW, −100..+100 |

### ems

Stub con lógica propuesta comentada. Ver `docs/ems_design.md` para el diseño completo.

Parámetros previstos:

| Parámetro | Default | Descripción |
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
| `test_bms` | `bms_parse_modbus()` — parseo registros LWS con valores sintéticos |
| `test_ems` | Lógica de decisión EMS con mock de `inverterSetPower` |
| `test_inverter` | Escalas y parseo registros SP6030 |

Los tests usan directamente las funciones `_parser` de `lib/` — sin hardware, sin duplicar lógica.

---

## Sketches de prueba de hardware

Ubicación: `sketches/`. Cada uno es un firmware independiente para verificar un subsistema. Todos usan `config.h` para pines y direcciones, `credentials.h` para WiFi/token.

| Sketch | Display | WiFi/TB | Inv Modbus | BMS Modbus | Propósito |
|---|---|---|---|---|---|
| `test_display` | ✓ dummy | — | — | — | Verificar pantalla y layout |
| `test_mqtt` | — | ✓ dummy | — | — | Verificar WiFi y ThingsBoard |
| `test_tb` | ✓ simulado | ✓ simulado | — | — | Construir y validar dashboard TB |
| `test_modbus_hw` | — | — | ✓ real | — | Verificar comunicación con inversor |
| `test_set_power` | — | ✓ real | ✓ real | — | Probar control de potencia end-to-end |
| `test_dashboard` | ✓ real | ✓ real | ✓ real | ✓ real | Integración completa con HW real |

### test_display
Cicla 3 pantallas cada 3s con datos hardcodeados: Status, Power Flow, Battery (campos LWS completos). Sin WiFi ni hardware. Verifica cableado del display y layout.

### test_mqtt
Conecta WiFi, publica telemetría dummy a ThingsBoard cada 5s, escucha y responde RPCs. Sin Modbus ni display. Verifica credenciales y conectividad de red.

### test_tb
Pantalla + ThingsBoard con datos simulados que derivan lentamente. Publica todos los keys del firmware real — inversor SP6030 + BMS LWS completos. Usar para construir y ajustar el dashboard de ThingsBoard sin hardware.

### test_modbus_hw
Conecta al inversor via RS-485 y vuelca todos los bloques a Serial cada 5s con valores escalados: Status, AC, DC, Grid, Load + dump crudo de registros 0–9. Instancia propia de `ModbusMaster` para diagnóstico granular. No requiere WiFi.

### test_set_power
Prueba el control de potencia end-to-end con verificación explícita de registros.

Al arrancar:
1. Corre `inverterInit()` — setea 873=0, 758=0, 135=0, demás registros de config
2. Lee y printea por Serial los registros clave (873, 758, 135, 334, 650, 341, 763, 764) para verificar que quedaron correctamente escritos
3. Lee el `set_power` actual de ThingsBoard y lo aplica

Al cambiar el knob: aplica el nuevo valor inmediatamente vía reg 135.

Rango del testbed: **−2 a +2 kW** (cambiar a −100/+100 para producción en `applySetPower()`).

### test_dashboard
Firmware de integración completo: lee inversor y BMS via Modbus, publica a ThingsBoard, muestra en pantalla. Los dots de status se ponen verdes/rojos según si la última lectura fue exitosa. Sin datos simulados como fallback.

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
#define MODBUS_DEVICE_ID     1    // DIP switch del inversor (1–247)
#define BMS_MODBUS_DEVICE_ID 51   // DIP switch del BMS (51–65)
#define RS485_DE_RE_PIN      5    // GPIO5 — NO usar GPIO4 (LCD RST en Ideaspark)
#define RS485_BAUD           115200

#define TB_HOST              "thingsboard.cloud"
#define TB_PORT              1883

#define POLL_MODBUS_MS   5000
#define POLL_BMS_MS      2000
#define PUBLISH_MS       10000
#define VERIFY_INIT_MS   60000

// Descomentar si el firmware del inversor es >= V3.0 (reg 19 >= 30)
// Habilita lectura de regs 200-213 (load data)
// El firmware actual es V2.88 — dejar comentado
// #define INVERTER_PROTOCOL_V3
```

---

## Dependencias

| Librería | Uso |
|---|---|
| PubSubClient (Nick O'Leary) | MQTT |
| ArduinoJson v7 (Benoit Blanchon) | JSON payload — usar `JsonDocument` |
| ModbusMaster (Doc Walker) | Modbus RTU sobre RS-485 |
| Adafruit ST7789 + Adafruit GFX | Display |

---

## Failsafe y robustez

- Si el ESP32 falla, el inversor sigue operando con los últimos parámetros escritos
- `verifyAndReinit()` restaura la config si el inversor se reinicia (corre cada 60s)
- Reconexión automática WiFi y MQTT en el loop
- ⚠ Conocido: `connectMQTT()` puede bloquearse si el broker no responde — pendiente timeout explícito

---

## TODO

### Hardware pendiente
- Confirmar dirección Modbus del inversor via DIP switch → `MODBUS_DEVICE_ID` en `config.h`
- Configurar BMS a 115200 bps antes de conectar al bus compartido
- Confirmar `BMS_MODBUS_DEVICE_ID` (DIP switch en la batería, rango 51–65)
- Verificar firmware inversor RTU ≥ V3.0 (reg 19 ≥ 30) para registros de carga 200–213
- Verificar los 150A de max DC current (regs 763/764) contra datasheet de la batería

### Pruebas de hardware pendientes
- `test_modbus_hw` — verificar lectura de registros del inversor
- `test_set_power` — verificar control de potencia con carga real, confirmar si reg 135 es AC o DC
- `test_dashboard` — integración completa Modbus inversor + BMS + display + ThingsBoard

### Firmware
- Activar y validar `emsUpdate()` con estrategia acordada (ver `ems_design.md`)
- Agregar shared attributes ThingsBoard para configurar parámetros EMS remotamente
- Resolver bloqueo de `connectMQTT()` con timeout explícito
- Integrar display en loop principal con datos reales
