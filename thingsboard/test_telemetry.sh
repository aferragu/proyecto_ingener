#!/bin/bash
# =============================================================================
# ThingsBoard — datos de prueba para PCS Monitor v2
# Uso: ./test_monitor_v2.sh <ACCESS_TOKEN>
# =============================================================================

TOKEN=${1:-"TU_ACCESS_TOKEN"}
URL="https://thingsboard.cloud/api/v1/$TOKEN/telemetry"

ok() { echo "  HTTP $1"; }

send() {
    local desc=$1
    local data=$2
    echo "[$desc]"
    curl -s -o /dev/null -w "  HTTP %{http_code}\n" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$data"
}

echo "======================================"
echo " PCS Monitor v2 — Test data"
echo " Token: $TOKEN"
echo "======================================"
echo ""

# ---------------------------------------------------------------------------
# 1. Estado general
# ---------------------------------------------------------------------------
send "Estado general" '{
    "running":   1,
    "fault":     0,
    "alarm":     0,
    "grid_tied": 1,
    "off_grid":  0,
    "derating":  0,
    "standby":   0
}'

# ---------------------------------------------------------------------------
# 2. AC inversor — dividido en dos envíos para evitar problemas con payloads grandes
# ---------------------------------------------------------------------------
send "AC inversor — tensiones y corrientes" \
    '{"freq_hz":50.01,"v_ab":397.3,"v_bc":398.1,"v_ca":397.8,"v_a":229.4,"v_b":230.1,"v_c":229.7,"i_a":36.2,"i_b":35.8,"i_c":36.0}'

send "AC inversor — potencias y PF" \
    '{"p_a_kw":8.28,"p_b_kw":8.21,"p_c_kw":8.27,"q_a_kvar":1.05,"q_b_kvar":1.08,"q_c_kvar":1.07,"pf_a":0.99,"pf_b":0.99,"pf_c":0.99,"p_inv_kw":24.76,"q_inv_kvar":3.20,"pf_total":0.99}'

# ---------------------------------------------------------------------------
# 3. DC
# ---------------------------------------------------------------------------
send "DC" '{"dc_power_kw":25.10,"dc_voltage_v":612.4,"dc_current_a":41.0}'

# ---------------------------------------------------------------------------
# 4. Red
# ---------------------------------------------------------------------------
send "Red" '{"grid_freq_hz":50.00,"grid_v_a":229.1,"grid_v_b":229.8,"grid_v_c":229.3,"grid_p_kw":18.50}'

# ---------------------------------------------------------------------------
# 5. Carga — lado load (V3.0, reg 200-213)
# ---------------------------------------------------------------------------
send "Carga V3.0 — tensiones y corrientes" \
    '{"load_freq_hz":50.01,"load_v_a":229.4,"load_v_b":230.1,"load_v_c":229.7,"load_i_a":55.3,"load_i_b":54.8,"load_i_c":55.1}'

send "Carga V3.0 — potencias" \
    '{"load_p_a_kw":12.65,"load_p_b_kw":12.52,"load_p_c_kw":12.59,"load_p_kw":37.76,"load_s_kva":38.10}'

# ---------------------------------------------------------------------------
# 6. Atributos de firmware (van al topic attributes)
# ---------------------------------------------------------------------------
echo "[Atributos firmware]"
curl -s -o /dev/null -w "  HTTP %{http_code}\n" \
    -X POST "https://thingsboard.cloud/api/v1/$TOKEN/attributes" \
    -H "Content-Type: application/json" \
    -d '{
        "fw_model":        2,
        "fw_hw_version":   131073,
        "fw_dsp_version":  196610,
        "fw_com_version":  65537,
        "fw_rtu_protocol": 30
    }'

echo ""
echo "======================================"
echo " Listo. Keys publicados:"
echo ""
echo " Estado:   running, fault, alarm, grid_tied, off_grid, derating, standby"
echo " AC inv:   freq_hz, v_a/b/c, v_ab/bc/ca, i_a/b/c"
echo "           p_a/b/c_kw, pf_a/b/c, p_inv_kw, q_inv_kvar, pf_total"
echo " DC:       dc_power_kw, dc_voltage_v, dc_current_a"
echo " Red:      grid_freq_hz, grid_v_a/b/c, grid_p_kw"
echo " Carga:    load_freq_hz, load_v_a/b/c, load_i_a/b/c"
echo "           load_p_a/b/c_kw, load_p_kw, load_s_kva"
echo " Attrs:    fw_model, fw_hw_version, fw_dsp_version,"
echo "           fw_com_version, fw_rtu_protocol"
echo "======================================"
