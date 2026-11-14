// 생성형 AI의 도움을 받아 작성함. PC 시험용 출력 대역.
#pragma once
struct Servo {
 bool on = false;
 void attach(int) { on = true; }
 void detach() { on = false; }
 bool attached() { return on; }
 void write(int) {}
};
