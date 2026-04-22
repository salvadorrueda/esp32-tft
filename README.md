# esp32-tft

Dos proyectos Arduino para un ESP-32 conectado a una pantalla TFT de 1,8"
(controlador ST7735, 128x160, SPI hardware):

- `tft_test/` — sketch de prueba que valida el cableado con colores y texto.
- `mikrotik_dashboard/` — dashboard en tiempo real del router Mikrotik
  **hAP ax3**, consultando la REST API de RouterOS v7 por HTTP.

## Cableado

Idéntico para los dos sketches:

| TFT   | ESP-32           |
|-------|------------------|
| VCC   | 3V3              |
| GND   | GND              |
| CS    | GPIO 15          |
| DC/RS | GPIO 0           |
| RST   | GPIO 2           |
| SCK   | GPIO 18 (SPI HW) |
| SDA   | GPIO 23 (MOSI)   |
| BL    | 3V3              |

## Librerías necesarias (Arduino Library Manager)

- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- ArduinoJson (≥ 6.21) — solo para `mikrotik_dashboard`

Con `arduino-cli`:

```bash
arduino-cli lib install "Adafruit GFX Library" \
                        "Adafruit ST7735 and ST7789 Library" \
                        "ArduinoJson"
arduino-cli core install esp32:esp32
```

## Compilar y flashear

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 mikrotik_dashboard/
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 mikrotik_dashboard/
```

Monitor serie a `115200` baudios.

## Configuración del dashboard

1. Copiar la plantilla de credenciales:

   ```bash
   cp mikrotik_dashboard/secrets.h.example mikrotik_dashboard/secrets.h
   ```

2. Editar `mikrotik_dashboard/secrets.h` con el SSID/clave Wi-Fi, la IP del
   router, usuario/clave del Mikrotik y la interfaz WAN a monitorizar.
   Este fichero está en `.gitignore`.

### Preparar el Mikrotik hAP ax3

Hay un script RouterOS listo en
[`mikrotik_dashboard/setup-mikrotik.rsc`](mikrotik_dashboard/setup-mikrotik.rsc)
que crea el grupo de solo lectura, el usuario dedicado y habilita el servicio
`www`. Es idempotente: si ya existen, actualiza la configuración.

1. Edita las variables al principio del fichero (al menos `dashPass` y,
   opcionalmente, `dashClient` para restringir el acceso a la IP del ESP-32).
2. Sube el fichero al router (Files en WinBox/WebFig, o por `scp`/`fetch`).
3. Desde la terminal del router:

   ```
   /import file-name=setup-mikrotik.rsc
   ```

Comprobación desde un PC de la LAN:

```bash
curl -u dashboard:UN_PASSWORD_FUERTE \
     http://192.168.88.1/rest/system/resource
```

Debe devolver un JSON con `cpu-load`, `free-memory`, etc. Si prefieres hacerlo
a mano, los comandos equivalentes son:

```
/user/group add name=readonly policy=read,api,rest-api,!write,!policy,!sensitive
/user add name=dashboard group=readonly password="UN_PASSWORD_FUERTE"
/ip/service enable www
# Opcional: restringir el servicio a la IP del ESP-32
# /ip/service set www address=192.168.88.50/32
```

## Funcionamiento

El sketch alterna cada ~6 s entre tres páginas:

1. **Sistema** — CPU (%), RAM usada/total, uptime.
2. **Tráfico WAN** — Rx/Tx actuales y sparkline autoescalada de la interfaz
   definida en `WAN_IFACE`.
3. **Clientes Wi-Fi** — MAC (cola), señal (dBm) e interfaz de hasta 8
   estaciones asociadas.

En la cabecera hay un punto verde/rojo que indica si la última petición REST
fue correcta. Si el router utiliza todavía el paquete legacy `wireless` en
lugar de `wifi`, el sketch hace fallback automáticamente al endpoint
`/interface/wireless/registration-table`.
