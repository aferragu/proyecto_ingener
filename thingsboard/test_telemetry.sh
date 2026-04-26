#!/bin/bash
# =============================================================================
# ThingsBoard — datos de prueba para PCS Monitor v2
# Uso: ./test_telemetry.sh <ACCESS_TOKEN> [--loop]
# Con --loop envía datos cada 5 segundos con variaciones aleatorias
# =============================================================================

TOKEN=${1:-"TU_ACCESS_TOKEN"}
URL="https://thingsboard.cloud/api/v1/$TOKEN/telemetry"
LOOP=${2:-""}

send() {
    local desc=$1
    local data=$2
    echo "[$desc]"
    curl -s -o /dev/null -w "  HTTP %{http_code}\n" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$data"
}

# Genera un float con variación aleatoria: base ± delta
rand() {
    local base=$1
    local delta=$2
    local decimals=${3:-2}
    python3 -c "import random; print(round($base + random.uniform(-$delta, $delta), $decimals))"
}

# Genera 0 o 1 con probabilidad de 1 siendo prob/100
rand_bit() {
    local prob=${1:-5}  # % de probabilidad de ser 1
    python3 -c "import random; print(1 if random.random() < $prob/100 else 0)"
}

send_snapshot() {
    # Estado — fault y alarm tienen baja probabilidad de activarse
    send "Estado" "{\"running\":1,\"fault\":$(rand_bit 3),\"alarm\":$(rand_bit 8),\"grid_tied\":1,\"off_grid\":0,\"derating\":$(rand_bit 5),\"standby\":0}"

    # AC inversor
    V_A=$(rand 229.4 2.0); V_B=$(rand 230.1 2.0); V_C=$(rand 229.7 2.0)
    I_A=$(rand 36.2 1.5);  I_B=$(rand 35.8 1.5);  I_C=$(rand 36.0 1.5)
    P_A=$(rand 8.28 0.5);  P_B=$(rand 8.21 0.5);  P_C=$(rand 8.27 0.5)
    P_TOT=$(python3 -c "print(round($P_A + $P_B + $P_C, 2))")
    PF=$(rand 0.99 0.01 3)
    FREQ=$(rand 50.01 0.05 3)

    send "AC inversor — tensiones y corrientes" \
        "{\"freq_hz\":$FREQ,\"v_ab\":$(rand 397.3 3),\"v_bc\":$(rand 398.1 3),\"v_ca\":$(rand 397.8 3),\"v_a\":$V_A,\"v_b\":$V_B,\"v_c\":$V_C,\"i_a\":$I_A,\"i_b\":$I_B,\"i_c\":$I_C}"

    send "AC inversor — potencias y PF" \
        "{\"p_a_kw\":$P_A,\"p_b_kw\":$P_B,\"p_c_kw\":$P_C,\"q_a_kvar\":$(rand 1.05 0.1),\"q_b_kvar\":$(rand 1.08 0.1),\"q_c_kvar\":$(rand 1.07 0.1),\"pf_a\":$PF,\"pf_b\":$PF,\"pf_c\":$PF,\"p_inv_kw\":$P_TOT,\"q_inv_kvar\":$(rand 3.20 0.3),\"pf_total\":$PF}"

    # DC
    DC_V=$(rand 612.4 5.0)
    DC_I=$(rand 41.0 2.0)
    DC_P=$(python3 -c "print(round($DC_V * $DC_I / 1000, 2))")
    send "DC" "{\"dc_power_kw\":$DC_P,\"dc_voltage_v\":$DC_V,\"dc_current_a\":$DC_I}"

    # Red
    GRID_P=$(rand 18.50 2.0)
    send "Red" "{\"grid_freq_hz\":$(rand 50.00 0.05 3),\"grid_v_a\":$(rand 229.1 2),\"grid_v_b\":$(rand 229.8 2),\"grid_v_c\":$(rand 229.3 2),\"grid_p_kw\":$GRID_P}"

    # Carga V3.0
    LOAD_P=$(python3 -c "print(round($P_TOT + $GRID_P, 2))")
    send "Carga V3.0 — tensiones y corrientes" \
        "{\"load_freq_hz\":$FREQ,\"load_v_a\":$V_A,\"load_v_b\":$V_B,\"load_v_c\":$V_C,\"load_i_a\":$(rand 55.3 2),\"load_i_b\":$(rand 54.8 2),\"load_i_c\":$(rand 55.1 2)}"

    send "Carga V3.0 — potencias" \
        "{\"load_p_a_kw\":$(rand 12.65 0.5),\"load_p_b_kw\":$(rand 12.52 0.5),\"load_p_c_kw\":$(rand 12.59 0.5),\"load_p_kw\":$LOAD_P,\"load_s_kva\":$(rand 38.10 0.5)}"
}

# Atributos firmware — solo una vez al inicio
echo "[Atributos firmware]"
curl -s -o /dev/null -w "  HTTP %{http_code}\n" \
    -X POST "https://thingsboard.cloud/api/v1/$TOKEN/attributes" \
    -H "Content-Type: application/json" \
    -d '{"fw_model":2,"fw_hw_version":131073,"fw_dsp_version":196610,"fw_com_version":65537,"fw_rtu_protocol":30}'

echo ""

if [ "$LOOP" == "--loop" ]; then
    echo "Modo loop — enviando datos cada 5s. Ctrl+C para detener."
    echo "======================================"
    COUNT=1
    while true; do
        echo ""
        echo "--- Snapshot #$COUNT ---"
        send_snapshot
        COUNT=$((COUNT + 1))
        sleep 5
    done
else
    echo "======================================"
    echo " PCS Monitor v2 — Test data (snapshot único)"
    echo " Uso con loop: ./test_telemetry.sh TOKEN --loop"
    echo "======================================"
    echo ""
    send_snapshot
    echo ""
    echo "Listo."
fi
