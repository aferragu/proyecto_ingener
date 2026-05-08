---
title: "EMS — Plan de Diseño y Control"
author: "Proyecto Ingener"
date: "Mayo 2026"
---

# EMS — Plan de Diseño y Control

## 1. Descripción del sistema

**Topología:**
```
Grid ──┐
       ├──→ PCS SinoSoar SP6030 ──→ Cargador EV Setec 60 kW
Bat  ──┘
       (Pylontech HV, CAN bus)
```

- Sin generación solar. Solo grid y batería.
- Única carga: cargador EV de hasta 60 kW.
- Objetivo: **peak shaving** — mantener `grid_p_kw ≤ GRID_LIMIT` (40 kW contractual) en todo momento.

---

## 2. Señales disponibles

| Variable | Fuente | Registro / Bus | Descripción |
|---|---|---|---|
| `grid_p_kw` | Modbus reg 192 | RS-485 | Potencia tomada de red (positivo = consumo) |
| `load_p_kw` | Modbus reg 200-213 | RS-485 | Potencia de carga EV |
| `bms.soc_pct` | CAN frame 0x421 | CAN 500kbps | SOC actual de la batería |
| `bms.max_discharge_a` × `bms.voltage_v` | CAN frame 0x422 | CAN 500kbps | Límite dinámico de descarga del BMS |
| `bms.max_charge_a` × `bms.voltage_v` | CAN frame 0x422 | CAN 500kbps | Límite dinámico de carga del BMS |
| `bms.charge_forbidden` | CAN frame 0x428 | CAN 500kbps | BMS prohíbe carga |
| `bms.discharge_forbidden` | CAN frame 0x428 | CAN 500kbps | BMS prohíbe descarga |

**Señal de control de salida:**
- `REG_SET_POWER` (reg 135): setpoint de potencia al PCS. Rango -100..+100 kW, precisión 0.1 kW. Positivo = descarga batería, negativo = carga batería.
- `ocpp_charging_profile`: límite de potencia al cargador EV vía API Setec / OCPP SetChargingProfile.

> **Incógnita pendiente:** confirmar si reg 135 es potencia AC (lado carga) o DC (lado batería). Resolver con prueba `test_set_power`.

---

## 3. Parámetros configurables (ThingsBoard shared attributes)

| Parámetro | Default | Descripción |
|---|---|---|
| `GRID_LIMIT_KW` | 40.0 | Techo contractual de potencia de red (kW) |
| `SOC_MIN` | 20.0 | SOC mínimo — no descargar por debajo (%) |
| `SOC_TARGET` | 90.0 | SOC objetivo para carga desde red (%) |
| `MAX_CHARGE_POWER_KW` | 10.0 | Potencia máxima de carga de batería desde red (kW) |
| `Ki_bat` | 0.1 | Ganancia control integral — batería (kW setpoint / kW error / s) |
| `Ki_ocpp` | 0.1 | Ganancia control integral — cargador EV (kW setpoint / kW error / s) |

---

## 4. Arquitectura de control

### 4.1 Separación de ciclos

El EMS tiene su propio ciclo de control, **independiente** del ciclo de telemetría de ThingsBoard:

```
Ciclo de control  — cada 1s:
    leer grid_p_kw directamente de reg 192
    leer bmsData (actualizado por CAN cada 1s)
    calcular setpoints
    escribir reg 135
    enviar ocpp_charging_profile si cambió

Ciclo de telemetría — cada 5s:
    pollModbus() completo (todos los bloques)
    publishTelemetry() a ThingsBoard
    incluye ems_state, ems_setpoint_bat_kw, ems_setpoint_ocpp_kw como campos extra
```

El EMS **no lee del JsonDocument de telemetría** para sus decisiones — usa registros Modbus frescos propios.

### 4.2 Control integral con saturación

El setpoint no se fija directamente sino que se actualiza en incrementos proporcionales al error — esto es un **control integral**. La planta es estática (mandás un setpoint, el PCS entrega esa potencia), por lo que el sistema en lazo cerrado es de **primer orden** con constante de tiempo:

$$\tau = \frac{1}{K_i}$$

Con `Ki = 0.1`, `τ = 10 segundos`. El sistema alcanza el **95% del valor final en 3τ = 30 segundos** ante un escalón de demanda.

**Ley de control (corrida cada 1 segundo):**

```
error = grid_p_kw - GRID_LIMIT_KW

setpoint_bat_nuevo  = setpoint_bat  + Ki_bat  * error
setpoint_ocpp_nuevo = setpoint_ocpp + Ki_ocpp * error
```

**Saturaciones simétricas:**

```
bat_upper =  min(bms_max_discharge_kw, EMS_MAX_DISCHARGE_KW)
bat_lower = -min(MAX_CHARGE_POWER_KW,  bms_max_charge_kw)
                                        // negativo = carga desde red

setpoint_bat  = clamp(setpoint_bat_nuevo,  lower = bat_lower,  upper = bat_upper)
setpoint_ocpp = clamp(setpoint_ocpp_nuevo, lower = 0,          upper = 60.0)
```

Ambos límites del BMS son **dinámicos** — el BMS reporta `max_discharge_a` y `max_charge_a` en tiempo real via CAN según temperatura, SOH y SOC instantáneo:

```
bms_max_discharge_kw = bms.max_discharge_a * bms.voltage_v / 1000.0
bms_max_charge_kw    = bms.max_charge_a    * bms.voltage_v / 1000.0
```

**Anti-windup por clamping condicional:**

Cuando el setpoint está saturado, solo se actualiza si el error va en la dirección que puede salir de la saturación — evita que el setpoint acumulado se aleje del valor real mientras está en el límite:

```
en_sat_inf = (setpoint_bat <= bat_lower)
en_sat_sup = (setpoint_bat >= bat_upper)

if (!en_sat_inf || error > 0) && (!en_sat_sup || error < 0):
    setpoint_bat = clamp(setpoint_bat + Ki_bat * error, bat_lower, bat_upper)
```

Esto garantiza respuesta inmediata cuando el error cambia de signo — por ejemplo cuando llega un EV mientras la batería estaba cargando a máximo.

### 4.3 Alternativa: control integral no lineal para protección de térmica

Con el control integral lineal, la ganancia es constante independientemente del error. Esto implica que ante un escalón grande (EV arrancando a plena potencia), la respuesta inicial es moderada y el tiempo sobre el límite de la térmica puede ser suficiente para dispararla.

Una alternativa es usar una ley de control de la forma:

$$u(e) = k \cdot e \cdot |e|$$

con ganancia efectiva `K_ef = k · |e|` — pequeña cerca del equilibrio (ajuste fino, sin overshooting) y grande lejos (respuesta agresiva en transitorio, protege la térmica).

**Análisis del sistema en lazo cerrado:**

Modelando la planta como un integrador puro (`ṡ = u`, donde `s` es el setpoint) y el error como `e = grid_p - GRID_LIMIT ≈ -s` en lazo cerrado, la dinámica del error satisface la ODE:

$$\dot{e} = -k \cdot e \cdot |e|$$

Para condición inicial `e₀ > 0` (exceso de demanda al arrancar el EV), esta es una ecuación de Bernoulli separable:

$$\frac{de}{e^2} = -k \, dt \quad \Rightarrow \quad \frac{1}{e(t)} = \frac{1}{e_0} + k \, t$$

con solución explícita:

$$\boxed{e(t) = \frac{e_0}{1 + k \, e_0 \, t}}$$

El error decae como `1/t` — más rápido que el exponencial `e^{-t/τ}` del controlador lineal para `t` pequeño, con convergencia asintótica más lenta pero sin relevancia práctica una vez que la saturación acota el setpoint.

El tiempo para reducir el error a un valor residual `ε` es:

$$t^* = \frac{1}{k} \left( \frac{1}{\varepsilon} - \frac{1}{e_0} \right)$$

Esto permite **tunear `k` directamente** a partir del transitorio deseado: dado un error inicial esperado `e₀` (ej. 20 kW si el EV arranca a 60 kW con red en 40 kW) y un tiempo objetivo `t*` para llegar a `ε = 1 kW`:

$$k = \frac{1}{t^*} \left( \frac{1}{\varepsilon} - \frac{1}{e_0} \right)^{-1}$$

**Valor de `k` para este sistema:**

El error máximo posible está acotado por la topología: `GRID_LIMIT = 40 kW` y cargador máximo de 60 kW, por lo tanto `e₀_max = 20 kW`. No existe peor caso más severo.

Con `e₀ = 20 kW`, `ε = 1 kW`, `t* = 30 s`:

$$k = \frac{1}{30} \cdot \frac{1}{\frac{1}{1} - \frac{1}{20}} = \frac{1}{30 \cdot 0.95} \approx 0.035 \; \text{kW}^{-1}\text{s}^{-1}$$

Esto garantiza convergencia en ≤ 30 segundos en el **peor caso posible**. Para errores iniciales menores (EV arrancando a menos de 60 kW) el tiempo de convergencia es estrictamente menor — ventaja respecto al P lineal donde el tiempo es siempre 30 segundos independientemente de `e₀`.

**Comparación con controlador lineal:**

| | Control integral lineal | Control integral no lineal `k·e·\|e\|` |
|---|---|---|
| Parámetro | `Ki` | `k` |
| Constante de tiempo | `τ = 1/Ki` (fija) | Variable — depende de `e₀` |
| Respuesta inicial (e grande) | Moderada | Agresiva |
| Régimen estacionario (e chico) | Puede oscilar con ruido | Suave, inmune a ruido pequeño |
| Análisis | Sistema lineal, trivial | ODE Bernoulli, solución explícita |
| Tuning | `Ki` desde `τ` | `k` desde `t*`, `e₀`, `ε` |

**Implementación discreta (Δt = 1s):**

```
delta = k * error * |error| * dt
setpoint_bat = clamp(setpoint_bat + delta, bat_lower, bat_upper)  // con anti-windup
```

> **Pendiente:** simular ambos controladores con la ODE discreta para comparar el pico de `grid_p_kw` durante el transitorio y decidir cuál implementar primero.

### 4.4 Comportamiento en régimen estacionario

Cuando `grid_p_kw = GRID_LIMIT_KW`, el error es cero y los setpoints no cambian.

Cuando la carga EV termina, `grid_p_kw` cae, el error se vuelve negativo, y `setpoint_bat` baja gradualmente hasta `bat_lower`. Si SOC < SOC_TARGET y la carga no está prohibida por el BMS, la batería carga desde red a potencia `MAX_CHARGE_POWER_KW` — limitada además por `bms_max_charge_kw` dinámico. El controlador no necesita lógica especial para este caso: la saturación simétrica lo maneja naturalmente.

Cuando llega un nuevo EV con la batería cargando, el error vuelve a ser positivo y el anti-windup garantiza que el setpoint responde de inmediato sin delay acumulado.

### 4.5 Mejoras de robustez a considerar

El control integral es teóricamente estable para esta planta — una planta estática con ganancia unitaria no tiene polos inestables y el lazo cerrado es de primer orden para cualquier `Ki > 0`. Los riesgos son prácticos, no teóricos, y se resuelven con las siguientes mejoras si la experiencia en campo lo justifica:

**Filtro sobre `grid_p_kw`:**

Si el registro 192 es ruidoso en estacionario, el integrador acumula ese ruido y el setpoint deriva lentamente. Solución: media móvil de N muestras antes de calcular el error:

```
grid_p_filtered = (grid_p[k] + grid_p[k-1] + ... + grid_p[k-N+1]) / N
error = grid_p_filtered - GRID_LIMIT_KW
```

N = 3 es un punto de partida razonable. El nivel de ruido del registro 192 se determina empíricamente monitoreando la señal en estacionario con `test_set_power`.

**Zona muerta:**

Si el ruido en estacionario es inevitable, una zona muerta evita que el integrador derive cuando el error es pequeño:

```
if |error| < DEADBAND:
    error = 0
```

Con `DEADBAND = 1 kW` el controlador ignora variaciones menores a 1 kW alrededor del límite. No afecta la dinámica en transitorio donde el error es de orden 10-20 kW.

**Dinámica interna del PCS:**

Si el PCS tiene una rampa interna lenta al seguir el setpoint (planta de primer orden en lugar de estática), dos integradores en cascada pueden oscilar. Esto se detecta en la prueba `test_set_power`: si `dc_power_kw` muestra overshooting ante un escalón de setpoint, hay dinámica interna y `Ki` debe reducirse. En ese caso el control integral no lineal (sección 4.3) es preferible por su ganancia pequeña cerca del equilibrio.

---

## 5. Máquina de estados

```
┌─────────────┐
│   NO_BMS    │  bms.valid == false → setPower=0, no actuar
└──────┬──────┘
       │ bms.valid
       ▼
┌─────────────┐
│  BMS_FAULT  │  fault || protection || (charge_forbidden && discharge_forbidden)
│             │  → setPower=0, setpoint_ocpp sin cambio
└──────┬──────┘
       │ sin fault
       ▼
┌─────────────┐
│    IDLE     │  soc >= SOC_TARGET && grid_p_kw <= GRID_LIMIT
└──────┬──────┘
       │
   ┌───┴────────────────┐
   ▼                    ▼
┌──────────────┐   ┌──────────────┐
│ DISCHARGING  │   │   CHARGING   │
│              │   │              │
│ error > 0    │   │ error < 0 && │
│ soc > SOC_MIN│   │ soc < TARGET │
│ !disch_forb  │   │ !chg_forb    │
└──────────────┘   └──────────────┘
       │
       ▼
┌──────────────┐
│  CURTAILING  │  debería descargar pero soc <= SOC_MIN || discharge_forbidden
│              │  → setPower=0, publicar curtail_target_kw para OCPP
└──────────────┘
```

**Nota sobre CURTAILING:** el EMS no intenta limitar la carga del EV directamente. Publica `curtail_target_kw` como telemetría en ThingsBoard para que la rule chain lo propague vía OCPP. El EMS no asume que el cargador obedecerá.

---

## 6. Modos de operación según disponibilidad de API Setec

### Modo A — API operativa (SetChargingProfile disponible)

Control coordinado de dos lazos integrales sobre el mismo error:

```
error = grid_p_kw - GRID_LIMIT_KW

Lazo batería:  setpoint_bat  += Ki_bat  * error  (saturado por BMS)
Lazo OCPP:     setpoint_ocpp += Ki_ocpp * error  (saturado a 60 kW)
```

Ambos lazos rampeando juntos eliminan el pico transitorio: el cargador nunca pide más de lo que grid + batería pueden dar.

### Modo B — API no disponible o solo lectura

Solo lazo de batería activo. El `setpoint_ocpp` queda fijo en 60 kW (máximo del cargador). Hay un pico transitorio inevitable al arranque del EV mientras el controlador rampa el setpoint de batería.

Aceptable si la medición tarifaria es en ventanas de 15 minutos (típico en Uruguay) — un transitorio de ~30 segundos no impacta la factura.

> **Pendiente:** confirmar modalidad de medición de demanda máxima con la distribuidora.

---

## 7. Incógnitas pendientes de resolver

| # | Incógnita | Cómo resolverla | Impacto en diseño |
|---|---|---|---|
| 1 | ¿Reg 135 es potencia AC o DC? | Prueba `test_set_power` con carga conocida | Escala del setpoint, compensación de eficiencia |
| 2 | ¿PCS clampea silenciosamente si setpoint > capacidad? | Prueba con batería a SOC bajo | Determina si la saturación del EMS es suficiente o hay que leer el estado del PCS |
| 3 | ¿Qué tan ruidoso es reg 192 en estacionario? | Monitoreo con `test_set_power` sin cambios | Define si se necesita filtro sobre `grid_p_kw` antes de calcular el error |
| 4 | ¿Latencia y capacidad de la API Setec? | Acceso a API + documentación | Determina si Modo A es viable y con qué frecuencia de actualización |
| 5 | ¿Modalidad de medición tarifaria (ventana 15 min)? | Consulta con distribuidora | Determina urgencia de eliminar transitorios |
| 6 | ¿Rampa interna del PCS al cambiar reg 135? | Prueba `test_set_power` con escalón a 1 kW | Determina si la planta es estática (Caso 1) o primer orden (Caso 2). Ver incógnita 7 para caracterización completa |
| 7 | ¿Cuál es $\tau_{pcs}$, la constante de tiempo interna del PCS? | Prueba con carga pesada (20-30 kW), escalón de setpoint, medir curva de `dc_power_kw` | Si $\tau_{pcs}$ es grande puede ser necesario reducir $K_i$ para mantener margen de estabilidad. El delay de 1 ciclo ($T = 1$ s) agrega fase adicional $-\omega_c T$ que se vuelve relevante si $K_i$ se ajusta hacia arriba para compensar la dinámica del PCS |

---

## 8. OCPP — lo relevante para este sistema

### Comando clave: SetChargingProfile

Permite fijar la potencia máxima del cargador dinámicamente. Es el `setpoint_ocpp` del controlador.

- **maxChargingRate** — potencia máxima en kW. Esto es lo que escribe el lazo OCPP del controlador cada segundo.
- **Tipo TxProfile** — aplica a la transacción activa y se descarta al terminar. Es el tipo correcto para control dinámico.
- **Tipo ChargePointMaxProfile** — límite absoluto permanente del punto de carga. No usar para control dinámico.

### Lectura relevante

- **StatusNotification** — notifica cambios de estado del cargador: `Preparing` (EV conectado, esperando), `Charging`, `Finishing`. El estado `Preparing` es la señal de anticipación — llega antes de que arranque la carga y permite pre-activar la batería (Modo A).
- **MeterValues** — el cargador reporta potencia activa periódicamente. Frecuencia configurable. Alternativa a `load_p_kw` del Modbus con posiblemente mejor resolución temporal.
- **SOC del vehículo** — solo disponible con ISO 15118 en cargador y auto. No asumir disponible.

### Lo que OCPP no puede hacer

- No se puede arrancar la carga a una potencia específica desde cero de forma coordinada — el `StartTransaction` lo inicia el EV o el usuario, no el backend.
- La latencia de aplicación de un `SetChargingProfile` depende del intervalo de heartbeat del cargador — típicamente 30-60s en instalaciones estándar. Con heartbeat corto (5-10s) es aceptable para el controlador.

### Preguntas a confirmar con la API Setec

| Pregunta | Impacto |
|---|---|
| ¿Qué versión de OCPP corre el Setec? | OCPP 2.0.1 tiene Smart Charging más completo |
| ¿La API expone SetChargingProfile directamente? | Determina si Modo A es viable |
| ¿Cuál es el intervalo de heartbeat / latencia de comandos? | Determina frecuencia máxima del lazo OCPP |
| ¿Los MeterValues se pueden configurar a 1s? | Determina si se puede usar como señal de control en lugar de Modbus |
| ¿StatusNotification llega en tiempo real a la API? | Determina si el feedforward por `Preparing` es viable |

---

## 9. Lo que NO cambia entre escenarios

Independientemente de las incógnitas anteriores, estos elementos del diseño están definidos:

- Ciclo de control a 1 segundo, independiente del ciclo de telemetría de 5 segundos
- Control integral con `Ki = 0.1` (τ = 10s, 95% en 30s) como default, configurable vía shared attribute
- Saturación dinámica por límites del BMS en tiempo real
- Protecciones BMS con prioridad máxima sobre cualquier setpoint
- EMS lee `grid_p_kw` directamente de reg 192, no del JsonDocument de telemetría
- Estado del EMS publicado a ThingsBoard cada 5s: `ems_state`, `ems_setpoint_bat_kw`, `ems_setpoint_ocpp_kw`, `curtail_target_kw`
