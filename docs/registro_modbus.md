# Registro Modbus — SinoSoar SP6030 (Protocolo V3.0)

## Parámetros físicos RS-485
- Baudrate: 115200 bps
- Formato: 8N1
- Protocolo: Modbus RTU
- CRC16: byte bajo primero

## Segmentos de registros
Los registros están divididos en segmentos. **No se pueden leer registros cruzando límites de segmento.**

| Segmento | Rango | Descripción |
|---|---|---|
| 0 | 0–24 | Información del dispositivo y versión |
| 1 | 25–99 | Estado y control |
| 2 | 100–299 | Mediciones AC, DC, red y carga |
| 3 | 300–649 | Parámetros de configuración |
| 4 | 650–699 | Comandos de control |
| 5 | 700–999 | Parámetros avanzados |
| 6 | 6000–6099 | BMS / Batería |

---

## Registros de versión (seg. 0)

| Dirección | Nombre | Tipo | Acceso | Precisión |
|---|---|---|---|---|
| 0 | Manufacturer / model | string (16 words) | RO | — |
| 1 | Model code | uint16 | RO | — |
| 10–11 | DSP bootloader version | uint32 | RO | — |
| 12–13 | Hardware version | uint32 | RO | — |
| 14–15 | DSP firmware version | uint32 | RO | — |
| 17–18 | COM software version | uint32 | RO | — |
| **19** | **RTU protocol version** | uint16 | RO | — |
| 20–21 | COM bootloader version | uint32 | RO | — |

> El reg 19 confirma si el firmware soporta el protocolo V3.0 (valor ≥ 30).

---

## Estado general (seg. 1)

| Dirección | Bit | Nombre | Acceso | Valores |
|---|---|---|---|---|
| 32 | 0 | Fault | RO | 1=Fault, 0=Normal |
| 32 | 1 | Alarm | RO | 1=Alarm, 0=Normal |
| 32 | 2 | Running | RO | 1=Running, 0=Stopped |
| 32 | 3 | Grid tied | RO | 1=Grid-tied |
| 32 | 4 | Off-grid | RO | 1=Off-grid (island) |
| 32 | 5 | Derating | RO | 1=Derating (nuevo V2.2.2) |
| 32 | 7 | Standby | RO | 1=Standby |

---

## Mediciones AC inversor (seg. 2, reg 100–125)

| Dirección | Nombre | Key TB | Tipo | Precisión |
|---|---|---|---|---|
| 100 | Inverter frequency | freq_hz | int16 | ×0.01 Hz |
| 101 | Line voltage AB | v_ab | int16 | ×0.1 V |
| 102 | Line voltage BC | v_bc | int16 | ×0.1 V |
| 103 | Line voltage CA | v_ca | int16 | ×0.1 V |
| 104 | Phase A current | i_a | int16 | ×0.1 A |
| 105 | Phase B current | i_b | int16 | ×0.1 A |
| 106 | Phase C current | i_c | int16 | ×0.1 A |
| 107 | Phase A voltage | v_a | int16 | ×0.1 V |
| 108 | Phase B voltage | v_b | int16 | ×0.1 V |
| 109 | Phase C voltage | v_c | int16 | ×0.1 V |
| 110 | Phase A active power | p_a_kw | int16 | ×0.01 kW |
| 111 | Phase B active power | p_b_kw | int16 | ×0.01 kW |
| 112 | Phase C active power | p_c_kw | int16 | ×0.01 kW |
| 113 | Phase A reactive power | q_a_kvar | int16 | ×0.01 kVAR |
| 114 | Phase B reactive power | q_b_kvar | int16 | ×0.01 kVAR |
| 115 | Phase C reactive power | q_c_kvar | int16 | ×0.01 kVAR |
| 119 | Phase A power factor | pf_a | int16 | ×0.01 |
| 120 | Phase B power factor | pf_b | int16 | ×0.01 |
| 121 | Phase C power factor | pf_c | int16 | ×0.01 |
| 122 | Total active power | p_inv_kw | int16 | ×0.01 kW |
| 123 | Total reactive power | q_inv_kvar | int16 | ×0.01 kVAR |
| 124 | Total apparent power | s_inv_kva | int16 | ×0.01 kVA |
| 125 | Total power factor | pf_total | int16 | ×0.01 |

---

## DC (seg. 2, reg 141–143)

| Dirección | Nombre | Key TB | Tipo | Precisión |
|---|---|---|---|---|
| 141 | DC power | dc_power_kw | int16 | ×0.01 kW |
| 142 | DC voltage | dc_voltage_v | int16 | ×0.1 V |
| 143 | DC current | dc_current_a | int16 | ×0.1 A |

---

## Red / Grid (seg. 2, reg 170–192)

| Dirección | Nombre | Key TB | Tipo | Precisión |
|---|---|---|---|---|
| 170 | Grid frequency | grid_freq_hz | int16 | ×0.01 Hz |
| 177 | Grid phase A voltage | grid_v_a | int16 | ×0.1 V |
| 178 | Grid phase B voltage | grid_v_b | int16 | ×0.1 V |
| 179 | Grid phase C voltage | grid_v_c | int16 | ×0.1 V |
| 192 | Grid total active power | grid_p_kw | int16 | ×0.01 kW |

---

## Lado carga — V3.0 only (seg. 2, reg 200–214)

> ⚠ Solo disponible en firmware con protocolo RTU V3.0 (reg 19 ≥ 30)

| Dirección | Nombre | Key TB | Tipo | Precisión |
|---|---|---|---|---|
| 200 | Load frequency | load_freq_hz | int16 | ×0.01 Hz |
| 201 | Load phase A current | load_i_a | int16 | ×0.1 A |
| 202 | Load phase B current | load_i_b | int16 | ×0.1 A |
| 203 | Load phase C current | load_i_c | int16 | ×0.1 A |
| 204 | Load phase A voltage | load_v_a | int16 | ×0.1 V |
| 205 | Load phase B voltage | load_v_b | int16 | ×0.1 V |
| 206 | Load phase C voltage | load_v_c | int16 | ×0.1 V |
| 207 | Load phase A active power | load_p_a_kw | int16 | ×0.01 kW |
| 208 | Load phase B active power | load_p_b_kw | int16 | ×0.01 kW |
| 209 | Load phase C active power | load_p_c_kw | int16 | ×0.01 kW |
| **213** | **Load total active power** | **load_p_kw** | int16 | ×0.01 kW |
| 214 | Load total apparent power | load_s_kva | int16 | ×0.01 kVA |

---

## Registros de configuración (seg. 3–5) — usados en init

| Dirección | Nombre | Valor usado | Descripción |
|---|---|---|---|
| 341 | 3-phase control mode | 1 | Control por fase individual |
| 652 | PV switch | 0 | PV apagado |
| 656 | DCDC switch | 0 | DCDC apagado |
| 763 | Max DC discharge current | 1500 | 150.0 A (×0.1) |
| 764 | Max DC charge current | 1500 | 150.0 A (×0.1) |
| 795 | Leakage detection | 0 | Deshabilitado |
| 873 | Anti-backflow (bit0) | 1 | Habilitado |

---

## Comandos de control (seg. 4)

| Dirección | Nombre | Acceso | Valor |
|---|---|---|---|
| 650 | Power ON | WO | 1 = arranque |
| 651 | Shutdown | WO | 1 = parada |
| 653 | Grid connection | WO | 1 = conectar red |
| 654 | Off-grid mode | WO | 1 = modo isla |

---

## Notas de decodificación

- **int16 con signo:** si raw > 32767 → raw − 65536 (potencias pueden ser negativas)
- **uint32:** (reg[n] << 16) | reg[n+1]
- **Bitfield:** (valor >> bit) & 0x01
- **CRC16:** polinomio 0xA001, byte bajo primero en el frame

## Secuencia de inicialización (replicada del EMS del fabricante)
1. reg 763 = 1500
2. reg 764 = 1500
3. reg 341 = 1
4. reg 652 = 0
5. reg 873 = 1
6. reg 795 = 0
7. reg 656 = 0
8. reg 650 = 1 → Power ON
