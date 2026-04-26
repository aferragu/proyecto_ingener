# Proyecto Ingener — PCS Inverter Monitor

Control y monitoreo de inversor SinoSoar SP6030 via ESP32.

## Estructura

```
esp32/
└── pcs_monitor_v2/
    └── pcs_monitor_v2.ino   # Firmware ESP32 (Modbus RTU + CAN + MQTT)

thingsboard/
└── test_telemetry.sh        # Script de prueba para llenar el dashboard

docs/
└── registro_modbus.md       # Referencia completa de registros Modbus V3.0
```

## Hardware
- **Inversor:** SinoSoar SP6030HBG2PS
- **Controlador:** ESP32 NodeMCU
- **RS-485:** Módulo MAX485 (GPIO16/17/4)
- **CAN:** SN65HVD230 (GPIO21/22)

## Dependencias Arduino
- PubSubClient (Nick O'Leary)
- ArduinoJson (Benoit Blanchon)
- ESP32 core 3.3.x (TWAI incluido)

## Configuración
Editar en `pcs_monitor_v2.ino`:
```cpp
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASSWORD    "TU_PASSWORD"
#define TB_ACCESS_TOKEN  "TU_ACCESS_TOKEN"
#define MODBUS_DEVICE_ID 1
```
