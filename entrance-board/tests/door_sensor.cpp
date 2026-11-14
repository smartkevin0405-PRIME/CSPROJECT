// 생성형 AI의 도움을 받아 작성함. 실제 문 센서 디바운스·원시 시각 보존 시험.
#include <cassert>
#include <cstdint>
uint32_t clockMs=1000;
int doorLevel=0;
#include "../SmartEntrance/Devices.cpp"
int main(){
 devicesBegin(); doorLevel=HIGH; devicesLoop();
 clockMs+=49; devicesLoop(); assert(!doorJustOpened());
 clockMs++; devicesLoop();
 clockMs+=8000; devicesLoop();
 uint32_t at=0; assert(doorJustOpened(&at) && at==1000);
 assert(!doorJustOpened());
 // 소비하기 전 닫혔다 다시 열려도 첫 열림 시각을 보존한다.
 doorLevel=LOW; devicesLoop(); clockMs+=50; devicesLoop();
 doorLevel=HIGH; devicesLoop(); uint32_t first=clockMs; clockMs+=50; devicesLoop();
 doorLevel=LOW; devicesLoop(); clockMs+=50; devicesLoop();
 doorLevel=HIGH; devicesLoop(); clockMs+=50; devicesLoop();
 assert(doorJustOpened(&at) && at==first);
 puts("문 센서 시각·디바운스 시험 통과");
}
