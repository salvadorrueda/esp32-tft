#!/usr/bin/env bash
# Compila y flashea cualquiera de los sketches del repo (por defecto
# mikrotik_dashboard/) usando arduino-cli. Tambien puede instalar el core
# ESP-32 y las librerias necesarias, y abrir el monitor serie al terminar.
#
# Uso:
#   ./flash.sh                         # compila y flashea mikrotik_dashboard
#   ./flash.sh tft_test                # compila y flashea tft_test
#   ./flash.sh --install               # instala core ESP-32 + librerias y sale
#   ./flash.sh --compile               # solo compila, no flashea
#   ./flash.sh --monitor               # tras flashear, abre el monitor serie
#   PORT=/dev/ttyACM0 ./flash.sh       # puerto alternativo
#   FQBN=esp32:esp32:esp32s3 ./flash.sh
#
# Variables de entorno reconocidas:
#   PORT   puerto serie (auto-detectado si no se define)
#   FQBN   placa (por defecto esp32:esp32:esp32)
#   BAUD   baudrate del monitor serie (por defecto 115200)

set -euo pipefail

FQBN="${FQBN:-esp32:esp32:esp32}"
BAUD="${BAUD:-115200}"
SKETCH=""
DO_INSTALL=0
DO_COMPILE_ONLY=0
DO_MONITOR=0

for arg in "$@"; do
  case "$arg" in
    --install) DO_INSTALL=1 ;;
    --compile) DO_COMPILE_ONLY=1 ;;
    --monitor) DO_MONITOR=1 ;;
    -h|--help)
      sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    -*)
      echo "Opcion desconocida: $arg" >&2; exit 2 ;;
    *)
      if [[ -z "$SKETCH" ]]; then SKETCH="$arg"
      else echo "Solo se acepta un sketch"; exit 2
      fi ;;
  esac
done

SKETCH="${SKETCH:-mikrotik_dashboard}"
SKETCH="${SKETCH%/}"

cd "$(dirname "$0")"

if ! command -v arduino-cli >/dev/null 2>&1; then
  cat >&2 <<'EOF'
arduino-cli no encontrado. Instalalo con:
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
y asegurate de que el binario este en el PATH.
EOF
  exit 1
fi

install_deps() {
  echo ">> Actualizando indices de cores"
  arduino-cli core update-index
  if ! arduino-cli core list | grep -q '^esp32:esp32'; then
    echo ">> Instalando core esp32:esp32"
    arduino-cli core install esp32:esp32
  else
    echo ">> Core esp32:esp32 ya instalado"
  fi
  echo ">> Instalando librerias"
  arduino-cli lib install \
    "Adafruit GFX Library" \
    "Adafruit ST7735 and ST7789 Library" \
    "ArduinoJson"
}

if (( DO_INSTALL )); then
  install_deps
  exit 0
fi

# Validaciones del sketch seleccionado
if [[ ! -d "$SKETCH" ]]; then
  echo "No existe la carpeta $SKETCH/" >&2
  exit 1
fi
if [[ ! -f "$SKETCH/${SKETCH}.ino" ]]; then
  echo "No existe $SKETCH/${SKETCH}.ino" >&2
  exit 1
fi
if [[ "$SKETCH" == "mikrotik_dashboard" && ! -f "$SKETCH/secrets.h" ]]; then
  echo "Falta $SKETCH/secrets.h. Crealo con:"
  echo "  cp $SKETCH/secrets.h.example $SKETCH/secrets.h"
  echo "y rellena Wi-Fi y credenciales del router."
  exit 1
fi

# Deteccion de puerto si no viene definido
detect_port() {
  local candidates=(/dev/ttyUSB* /dev/ttyACM*)
  for c in "${candidates[@]}"; do
    [[ -e "$c" ]] || continue
    echo "$c"
    return 0
  done
  return 1
}

if [[ -z "${PORT:-}" ]]; then
  if ! PORT="$(detect_port)"; then
    if (( DO_COMPILE_ONLY )); then
      PORT=""
    else
      echo "No se detecto ningun puerto serie. Conecta el ESP-32 o define PORT=..." >&2
      exit 1
    fi
  fi
fi

echo ">> Sketch: $SKETCH"
echo ">> FQBN:   $FQBN"
if [[ -n "${PORT:-}" ]]; then echo ">> Puerto: $PORT"; fi

echo ">> Compilando"
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

if (( DO_COMPILE_ONLY )); then
  echo ">> Compile-only OK"
  exit 0
fi

echo ">> Flasheando"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

if (( DO_MONITOR )); then
  echo ">> Monitor serie ($BAUD baud). Ctrl+C para salir."
  arduino-cli monitor -p "$PORT" --config "baudrate=$BAUD"
fi
