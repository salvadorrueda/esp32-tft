Sí, hay varias opciones para flashear el ESP32 desde la línea de comandos en Linux:

## Opciones principales

### 1. `esptool.py` (herramienta oficial de Espressif)
Es la opción más directa y es la misma que usa Arduino IDE internamente.

```bash
pip install esptool

# Flashear un binario
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash 0x1000 firmware.bin
```

### 2. `arduino-cli` (Arduino sin IDE)
Si quieres seguir compilando sketches de Arduino pero desde terminal:

```bash
# Instalar
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Instalar core ESP32
arduino-cli core install esp32:esp32

# Compilar
arduino-cli compile --fqbn esp32:esp32:esp32 MiSketch/

# Subir
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 MiSketch/
```

### 3. `idf.py` (ESP-IDF, el framework nativo de Espressif)
Para proyectos más serios sin depender de Arduino:

```bash
# Compilar y flashear en un solo comando
idf.py -p /dev/ttyUSB0 flash
```

---

## Pasos previos comunes

Asegúrate de tener permisos en el puerto serie:
```bash
sudo usermod -a -G dialout $USER
# (requiere cerrar sesión y volver a entrar)
```

Verificar el puerto:
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

---

**Recomendación:** Si ya usas Arduino IDE y quieres la transición más suave, empieza con `arduino-cli`. Si quieres más control y es un proyecto serio, considera ESP-IDF con `idf.py`.

¿Qué tipo de proyecto tienes? ¿Sketches de Arduino o algo más personalizado?