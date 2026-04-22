#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#define TFT_CS    15
#define TFT_DC     0
#define TFT_RST    2
// SCK -> GPIO18 (hardware SPI)
// SDA -> GPIO23 (hardware SPI MOSI)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando pantalla TFT...");

  tft.initR(INITR_BLACKTAB);  // Para pantallas con fondo negro (tab negro)
  tft.setRotation(1);          // Horizontal

  // Pantalla roja
  tft.fillScreen(ST77XX_RED);
  delay(500);

  // Pantalla verde
  tft.fillScreen(ST77XX_GREEN);
  delay(500);

  // Pantalla azul
  tft.fillScreen(ST77XX_BLUE);
  delay(500);

  // Fondo negro con texto
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.println("ESP32 TFT");

  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(10, 55);
  tft.println("128x160 ST7735");

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 70);
  tft.println("Pantalla OK! Como mola");

  // Dibujar un rectangulo de colores
  tft.drawRect(10, 90, 140, 40, ST77XX_WHITE);
  tft.fillRect(12, 92, 44, 36, ST77XX_RED);
  tft.fillRect(58, 92, 44, 36, ST77XX_GREEN);
  tft.fillRect(104, 92, 44, 36, ST77XX_BLUE);

  Serial.println("Pantalla inicializada correctamente.");
}

void loop() {
  // Parpadeo del texto para confirmar que el programa sigue corriendo
  tft.setTextColor(ST77XX_MAGENTA);
  tft.setTextSize(1);
  tft.setCursor(10, 145);
  tft.print("Funcionando!");
  delay(1000);

  tft.fillRect(10, 140, 140, 15, ST77XX_BLACK);
  delay(1000);
}
