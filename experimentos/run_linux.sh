#!/usr/bin/env bash
# =============================================================================
# run_linux.sh — coleta COMPLETA dos dados no Linux
# =============================================================================
# Uso (na raiz do repositório, depois de compilar em Release):
#     bash experimentos/run_linux.sh
#
# Variáveis opcionais (ex.: REPS=2 bash experimentos/run_linux.sh para um teste rápido):
#     BENCH  caminho do executável   (padrão: build/bench)
#     SO     rótulo no CSV            (padrão: linux)  — ex.: SO=linux-mint
#     TOTAL  itens por execução       (padrão: 400000)
#     REPS   repetições               (padrão: 30)
#
# Gera em dados/:
#     <SO>_raw.csv        uma linha por execução (12 configurações × REPS)
#     <SO>_latencia.csv   amostras de latência (só a configuração 1×1, buffer 1)
#     <SO>_ambiente.txt   descrição da máquina (vai para o README/relatório)
#
# DESENHO DO EXPERIMENTO (ver o guia de benchmark controlado):
#   - 1 execução de AQUECIMENTO descartada;
#   - repetições INTERCALADAS: rep 1 de todas as configurações, depois rep 2...
#     e dentro de cada rep a ordem das 12 configurações é SORTEADA (shuf).
#     Assim, uma variação lenta da máquina (aquecimento, tarefa de fundo) se
#     espalha por todas as configurações em vez de prejudicar só uma;
#   - o script PARA se alguma execução falhar no checksum (dado inválido).
# =============================================================================
set -euo pipefail      # para no primeiro erro, variável indefinida ou falha em pipe

BENCH=${BENCH:-build/bench}
SO=${SO:-linux}
TOTAL=${TOTAL:-400000}
REPS=${REPS:-30}

RAW=dados/${SO}_raw.csv
LAT=dados/${SO}_latencia.csv
AMB=dados/${SO}_ambiente.txt

[ -x "$BENCH" ] || { echo "Executavel '$BENCH' nao encontrado. Compile antes (README)."; exit 1; }

# Protege os dados brutos: nunca sobrescreve uma coleta já feita.
if [ -e "$RAW" ] || [ -e "$LAT" ]; then
    echo "ERRO: $RAW ou $LAT ja existe. Mova/apague manualmente se quiser coletar de novo."
    exit 1
fi
mkdir -p dados

# ---- Registro do ambiente -----------------------------------------------------
{
    echo "data: $(date -Iseconds)"
    echo "uname: $(uname -a)"
    echo "distro: $(grep PRETTY_NAME /etc/os-release 2>/dev/null | cut -d= -f2)"
    echo "glibc: $(ldd --version 2>/dev/null | head -1)"
    echo "gcc: $(gcc --version 2>/dev/null | head -1)"
    echo "nproc: $(nproc)"
    echo "--- lscpu"; lscpu
    echo "--- memoria"; free -h
    echo "--- virtualizacao"; (systemd-detect-virt 2>/dev/null || echo "desconhecido")
    echo "parametros: TOTAL=$TOTAL REPS=$REPS"
} > "$AMB"

# ---- Cabeçalhos ---------------------------------------------------------------
echo "so,versao,prod,cons,buf,total,rep,tempo_s,vazao,checksum_ok" > "$RAW"
echo "so,versao,rep,amostra,lat_ns" > "$LAT"

# ---- As 12 configurações: versão × (P×C) × buffer ------------------------------
CONFIGS=()
for v in A B; do
    for pc in 1 4; do                  # 1×1 e 4×4
        for b in 1 16 256; do
            CONFIGS+=("$v $pc $pc $b")
        done
    done
done

# ---- Aquecimento (resultado descartado) ---------------------------------------
"$BENCH" "$SO" A 1 1 16 "$TOTAL" 0 > /dev/null

# ---- Coleta -------------------------------------------------------------------
inicio=$(date +%s)
for rep in $(seq 1 "$REPS"); do
    # sorteia a ordem das configurações nesta repetição
    mapfile -t ORDEM < <(printf '%s\n' "${CONFIGS[@]}" | shuf)
    for cfg in "${ORDEM[@]}"; do
        read -r v p c b <<< "$cfg"
        # o arquivo de latência é passado sempre; o bench só grava no 1×1 buffer 1
        "$BENCH" "$SO" "$v" "$p" "$c" "$b" "$TOTAL" "$rep" "$LAT" >> "$RAW" \
            || { echo "ERRO na execucao: $cfg rep $rep (checksum ou falha)"; exit 1; }
    done
    echo "repeticao $rep/$REPS concluida ($(( $(date +%s) - inicio )) s)" >&2
done

echo "Pronto: $RAW ($(($(wc -l < "$RAW") - 1)) linhas), $LAT, $AMB"
