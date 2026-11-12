// Tft9488.cpp
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "Tft9488.h"

Tft9488::Tft9488(uint8_t cs, uint8_t dc, uint8_t rst, uint32_t hz)
  : Adafruit_GFX(320, 480), _cs(cs), _dc(dc), _rst(rst), _spi(hz, MSBFIRST, SPI_MODE0) {}

// 명령 1바이트와 그 뒤의 데이터를 보낸다
void Tft9488::command(uint8_t cmd, const uint8_t* data, uint8_t len) {
  digitalWrite(_dc, LOW);
  SPI.transfer(cmd);
  digitalWrite(_dc, HIGH);
  for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
}

void Tft9488::begin(uint8_t rotation, bool invert) {
  pinMode(_cs, OUTPUT);  digitalWrite(_cs, HIGH);
  pinMode(_dc, OUTPUT);  digitalWrite(_dc, HIGH);
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH); delay(10);
  digitalWrite(_rst, LOW);  delay(20);
  digitalWrite(_rst, HIGH); delay(150);

  static const uint8_t gammaP[] = { 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F };
  static const uint8_t gammaN[] = { 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F };
  static const uint8_t pwr1[]   = { 0x17, 0x15 };
  static const uint8_t pwr2[]   = { 0x41 };
  static const uint8_t vcom[]   = { 0x00, 0x12, 0x80 };
  static const uint8_t pixfmt[] = { 0x66 };              // 18비트 색
  static const uint8_t ifmode[] = { 0x00 };
  static const uint8_t frame[]  = { 0xA0 };
  static const uint8_t inv[]    = { 0x02 };
  static const uint8_t dfc[]    = { 0x02, 0x02, 0x3B };
  static const uint8_t entry[]  = { 0xC6 };
  static const uint8_t adj3[]   = { 0xA9, 0x51, 0x2C, 0x82 };

  SPI.beginTransaction(_spi);
  digitalWrite(_cs, LOW);
  command(0xE0, gammaP, sizeof(gammaP));
  command(0xE1, gammaN, sizeof(gammaN));
  command(0xC0, pwr1, sizeof(pwr1));
  command(0xC1, pwr2, sizeof(pwr2));
  command(0xC5, vcom, sizeof(vcom));
  command(0x3A, pixfmt, sizeof(pixfmt));
  command(0xB0, ifmode, sizeof(ifmode));
  command(0xB1, frame, sizeof(frame));
  command(0xB4, inv, sizeof(inv));
  command(0xB6, dfc, sizeof(dfc));
  command(0xB7, entry, sizeof(entry));
  command(0xF7, adj3, sizeof(adj3));
  command(invert ? 0x21 : 0x20);                         // 색 반전 켜기/끄기
  // 화면 방향. 0x08 = BGR 순서, 0x20 = 가로세로 바꿈, 0x40/0x80 = 좌우/상하 뒤집기
  uint8_t madctl;
  switch (rotation & 3) {
    case 0:  madctl = 0x48; _width = 320; _height = 480; break;
    case 1:  madctl = 0x28; _width = 480; _height = 320; break;
    case 2:  madctl = 0x88; _width = 320; _height = 480; break;
    default: madctl = 0xE8; _width = 480; _height = 320; break;
  }
  command(0x36, &madctl, 1);
  command(0x11);                                         // 절전 해제
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
  delay(120);
  SPI.beginTransaction(_spi);
  digitalWrite(_cs, LOW);
  command(0x29);                                         // 화면 켜기
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
  delay(25);
}

// 이후에 보내는 픽셀이 들어갈 사각형 영역을 정한다
void Tft9488::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t xs[] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
  uint8_t ys[] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
  command(0x2A, xs, 4);
  command(0x2B, ys, 4);
  command(0x2C);
}

void Tft9488::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0 || x >= _width || y >= _height) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _width)  w = _width - x;
  if (y + h > _height) h = _height - y;
  if (w <= 0 || h <= 0) return;

  // RGB565를 3바이트로 바꾼다
  uint8_t r = (color >> 8) & 0xF8, g = (color >> 3) & 0xFC, b = (color << 3) & 0xF8;
  const uint8_t CHUNK = 32;                 // 한 번에 보내는 픽셀 수
  uint8_t buf[CHUNK * 3];
  uint32_t left = (uint32_t)w * h;

  SPI.beginTransaction(_spi);
  digitalWrite(_cs, LOW);
  setWindow(x, y, x + w - 1, y + h - 1);
  while (left > 0) {
    uint8_t n = left > CHUNK ? CHUNK : (uint8_t)left;
    // transfer(buf, n)는 받은 값으로 buf를 덮어쓰므로 매번 다시 채운다
    for (uint8_t i = 0; i < n; i++) { buf[i * 3] = r; buf[i * 3 + 1] = g; buf[i * 3 + 2] = b; }
    SPI.transfer(buf, (size_t)n * 3);
    left -= n;
  }
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void Tft9488::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= _width || y >= _height) return;
  fillRect(x, y, 1, 1, color);
}
void Tft9488::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { fillRect(x, y, w, 1, color); }
void Tft9488::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { fillRect(x, y, 1, h, color); }
void Tft9488::fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }
