// Tft9488.h  ILI9488 3.5인치 SPI 화면(480x320) 드라이버
// Adafruit_GFX를 상속해서 글자와 도형 그리기는 그 라이브러리 기능을 쓴다.
// ILI9488은 SPI에서 픽셀 하나를 3바이트(R, G, B)로 받는다.
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

class Tft9488 : public Adafruit_GFX {
public:
  Tft9488(uint8_t cs, uint8_t dc, uint8_t rst, uint32_t hz);
  void begin(uint8_t rotation, bool invert);

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
  void fillScreen(uint16_t color) override;

private:
  uint8_t _cs, _dc, _rst;
  SPISettings _spi;
  void command(uint8_t cmd, const uint8_t* data = nullptr, uint8_t len = 0);
  void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
};
