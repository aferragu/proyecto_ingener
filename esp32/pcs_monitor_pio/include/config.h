#pragma once
// =============================================================================
// config.h — Parámetros configurables del sistema
// =============================================================================

// WiFi — credenciales en credentials.h
#include "credentials.h"  // nunca subir al repo — ver credentials.h.example

// ThingsBoard
#define TB_HOST          "thingsboard.cloud"
#define TB_PORT          1883

// RS-485 / Modbus
#define RS485_BAUD       115200
#define RS485_SERIAL     Serial2
#define RS485_TX_PIN     17
#define RS485_RX_PIN     16
#define RS485_DE_RE_PIN  5      // GPIO5 — DE+RE control for MAX485
                                // NOTE: GPIO4 is hardwired to LCD RST on the Ideaspark board
                                // and cannot be used for RS-485. Wire MAX485 DE+RE to GPIO5.
#define MODBUS_DEVICE_ID 1      // Dirección DIP switch del inversor (1-247)

// CAN / BMS
#define CAN_TX_PIN       GPIO_NUM_21
#define CAN_RX_PIN       GPIO_NUM_22
#define CAN_SPEED        TWAI_TIMING_CONFIG_500KBITS()  // confirmed 500kbps from CANalyzer capture
#define BMS_CAN_ADDR     1      // Dirección del BMS (1-15), embebida en CAN ID

// LED de status — GPIO2 (LED integrado NodeMCU, activo en LOW)
#define LED_PIN          2
#define LED_ON()         digitalWrite(LED_PIN, LOW)
#define LED_OFF()        digitalWrite(LED_PIN, HIGH)

// Intervalos de polling (ms)
#define POLL_MODBUS_MS   5000
#define POLL_CAN_MS      1000
#define PUBLISH_MS       10000
#define VERIFY_INIT_MS   60000

// Registros Modbus — SP6030 protocolo V3.0
// Las definiciones de registros de control están en lib/inverter_core/src/inverter_core.h
// Lectura
#define REG_STATUS                  32
#define REG_STATUS_COUNT             1
#define REG_AC_START               100
#define REG_AC_COUNT                26
#define REG_DC_START               141
#define REG_DC_COUNT                 3
#define REG_GRID_START             170
#define REG_GRID_COUNT              10
#define REG_GRID_POWER             192
#define REG_LOAD_START             200
#define REG_LOAD_COUNT              14
#define REG_VERSION_START            0
#define REG_VERSION_COUNT           22
