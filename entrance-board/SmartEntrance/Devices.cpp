// Devices.cpp
// [생성형 AI 활용] 이 파일은 Claude의 도움을 받아 작성했다.
#include "Devices.h"
#include <Servo.h>
#include "Arduino_LED_Matrix.h"

static ArduinoLEDMatrix matrix;
static Servo dial;
static uint32_t dialDetachAt = 0;         // 이 시각이 지나면 서보 신호를 끊는다 (0 = 이미 끊김)

// ---- 문 센서 디바운스 ----
static bool doorOpen = false;
static bool doorRaw = false;
static uint32_t doorChangedAt = 0;
static bool doorOpenedFlag = false;
static uint32_t doorOpenedAt = 0;

void devicesBegin() {
  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_DHT, INPUT_PULLUP);
  matrix.begin();
  doorRaw = doorOpen = (digitalRead(PIN_DOOR) == DOOR_OPEN_LEVEL);
  doorOpenedFlag = false;
}

void devicesLoop() {
  // 값이 50ms 동안 그대로일 때만 문 상태로 인정한다
  bool raw = (digitalRead(PIN_DOOR) == DOOR_OPEN_LEVEL);
  if (raw != doorRaw) { doorRaw = raw; doorChangedAt = millis(); }
  if (raw != doorOpen && millis() - doorChangedAt >= 50) {
    doorOpen = raw;
    if (doorOpen && !doorOpenedFlag) {
      doorOpenedAt = doorChangedAt;       // 조회가 끝난 시각 대신 실제 감지 시각을 보존
      doorOpenedFlag = true;
    }
  }
  if (dialDetachAt != 0 && (int32_t)(millis() - dialDetachAt) >= 0) {
    dial.detach();                        // 떨림과 대기 전류를 줄인다
    dialDetachAt = 0;
  }
}

bool pirActive() { return analogRead(PIN_PIR) > PIR_THRESHOLD; }
bool doorIsOpen() { return doorOpen; }
bool doorJustOpened(uint32_t* openedAtMs) {
  bool f = doorOpenedFlag;
  if (f && openedAtMs) *openedAtMs = doorOpenedAt;
  doorOpenedFlag = false;
  return f;
}

// ---- DHT11 ----
// 핀이 level 상태로 머문 시간을 마이크로초로 잰다. 너무 길면 0
static uint32_t dhtPulse(uint8_t level, uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(PIN_DHT) == level) {
    if (micros() - start > timeoutUs) return 0;
  }
  return micros() - start;
}

bool dhtRead(int8_t& tempC, uint8_t& humidity) {
  uint8_t data[5] = { 0, 0, 0, 0, 0 };
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);             // 시작 신호: 18ms 이상 LOW
  delay(20);
  pinMode(PIN_DHT, INPUT_PULLUP);
  delayMicroseconds(40);
  if (dhtPulse(HIGH, 200) == 0 && digitalRead(PIN_DHT) == HIGH) return false;
  if (dhtPulse(LOW, 200) == 0) return false;     // 센서 응답 LOW 약 80us
  if (dhtPulse(HIGH, 200) == 0) return false;    // 센서 응답 HIGH 약 80us
  for (uint8_t i = 0; i < 40; i++) {
    if (dhtPulse(LOW, 200) == 0) return false;   // 비트 시작 LOW 약 50us
    uint32_t high = dhtPulse(HIGH, 200);         // HIGH가 길면 1, 짧으면 0
    if (high == 0) return false;
    data[i / 8] <<= 1;
    if (high > 45) data[i / 8] |= 1;
  }
  if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) return false;
  humidity = data[0];
  tempC = (int8_t)data[2];
  return true;
}

// ---- LED 매트릭스 (세로 8, 가로 12) ----
// 3x5 숫자 글꼴. 각 줄의 아래 3비트가 왼쪽부터 켜질 칸이다
static const uint8_t DIGIT[10][5] = {
  { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
  { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
};
static uint8_t frame[8][12];

static void frameClear() { memset(frame, 0, sizeof(frame)); }
static void frameDigit(uint8_t d, uint8_t col) {
  for (uint8_t r = 0; r < 5; r++)
    for (uint8_t c = 0; c < 3; c++)
      if (DIGIT[d][r] & (4 >> c)) frame[r + 1][col + c] = 1;
}

void matrixShowTemp(int8_t tempC) {
  frameClear();
  int v = tempC;
  if (v < -99) v = -99;
  if (v > 99) v = 99;
  if (v < 0) { frame[3][0] = 1; frame[3][1] = 1; v = -v; }   // 음수 부호
  if (v >= 10) frameDigit(v / 10, 3);
  frameDigit(v % 10, 7);
  frame[1][11] = 1;                                          // 도(°) 표시 점
  matrix.renderBitmap(frame, 8, 12);
}

void matrixShowNone() {
  frameClear();
  for (uint8_t c = 3; c < 6; c++) frame[3][c] = 1;
  for (uint8_t c = 7; c < 10; c++) frame[3][c] = 1;
  matrix.renderBitmap(frame, 8, 12);
}

// ---- 서보 다이얼 ----
void dialSet(WeatherKind kind) {
  if (kind >= WX_UNKNOWN) return;
  if (!dial.attached()) dial.attach(PIN_SERVO);
  dial.write(DIAL_ANGLE[kind]);
  dialDetachAt = millis() + 700;          // 바늘이 다 움직인 뒤 신호를 끊는다
  if (dialDetachAt == 0) dialDetachAt = 1;
}
