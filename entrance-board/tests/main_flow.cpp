// 생성형 AI의 도움을 받아 작성함. 실제 스케치의 조회·문 열림·화면 흐름 회귀 시험.
#include <cassert>
#include <cstdint>
uint32_t clockMs = 1000;
int doorLevel = 0;
#include "../SmartEntrance/SmartEntrance.ino"
static bool openedFlag, failApis;
static unsigned requests, openOnFetch, departures, signalAttempts;
static uint32_t openedAt;
static DepartureEvent captured;
static uint16_t shown[2];
void devicesBegin(){} void devicesLoop(){}
static bool pirNow; bool pirActive(){return pirNow;}
static bool doorOpenNow; bool doorIsOpen(){return doorOpenNow;}
bool doorJustOpened(uint32_t* at){
 bool r=openedFlag; if(r && at) *at=openedAt; openedFlag=false; return r;
}
bool dhtRead(int8_t&,uint8_t&){return false;}
void matrixShowTemp(int8_t){} void matrixShowNone(){} void dialSet(WeatherKind){}
bool netBegin(){return true;} void netLoop(){} bool netReady(){return true;}
uint32_t netEpoch(){return 1700000000+clockMs/1000;} bool netBusy(){return false;}
static void fetchDelay(){
 ++requests;
 if(requests==openOnFetch){openedAt=clockMs+1000;openedFlag=true;}
 clockMs+=8000;
}
bool fetchWeather(WeatherInfo& w){fetchDelay();w.ok=!failApis;return w.ok;}
bool fetchArrival(const Stop& s,TransitArrival& out){
 fetchDelay();out.mode=s.mode;out.etaSec[0]=120;out.etaSec[1]=300;return !failApis;
}
void screensBegin(){} void screenIdle(bool,int8_t,uint8_t){}
void screenIdleUpdate(bool,int8_t,uint8_t){} void screenPrompt(){}
void screenMessage(const char*,const char*,uint16_t){}
void screenInfo(const User&,const WeatherInfo&){}
void screenInfoRow(uint8_t i,const Stop&,const TransitArrival& a,bool,int16_t){shown[i]=a.etaSec[0];}
void screenInfoCountdown(uint16_t){} void screenInfoPhone(bool){}
void travelBegin(){} void travelLoop(){if(departures) signalAttempts++;}
void travelOnDeparture(const DepartureEvent& e){captured=e;departures++;}
uint16_t travelGetWalkSec(uint8_t,const char*){return 300;}
int16_t travelGetSlackSec(uint8_t,const char*,uint16_t){return 0;}
static unsigned cancels; void travelOnCancel(uint8_t){cancels++;}
bool travelPhoneOnline(uint8_t){return true;}
static void reset(){
 clockMs=1000; openedFlag=failApis=false; requests=openOnFetch=departures=signalAttempts=0;
 weatherTried=true; weatherAt=clockMs; weather.ok=true;
 armedUser=curUser=departedUser=-1; state=ST_IDLE;
 pirNow=doorOpenNow=false; cancels=0; shimCardLen=0; armedDoorOpen=false; weatherOkAt=clockMs;
}
int main(){
 reset(); openOnFetch=1; handleUser(0);
 assert(captured.uptimeMs==2000 && captured.epochSec==1700000002 && departures==1);
 assert(requests==1 && signalAttempts==1 && state==ST_MESSAGE);
 reset(); failApis=true; weather.ok=false; handleUser(0);
 assert(armedUser==0 && departures==0);
 openedAt=clockMs+1000; clockMs=openedAt+50; openedFlag=true; loop();
 assert(departures==1 && captured.arrivalCount==0);
 reset(); failApis=true; weather.ok=false; openOnFetch=1; handleUser(0);
 assert(departures==1 && captured.uptimeMs==2000 && requests==1);
 reset(); weatherTried=false; openOnFetch=1; handleUser(0);
 assert(departures==1 && requests==1); // 날씨 조회 중에도 나머지 조회를 기다리지 않는다
 reset(); handleUser(0);
 assert(shown[0]==112 && shown[1]==120);
 clockMs+=3000; drawRows(); assert(shown[0]==109 && shown[1]==117);
 assert(etaNow(TRAVEL_UNKNOWN_SEC, 0)==TRAVEL_UNKNOWN_SEC);
 reset(); openedFlag=true; openedAt=500; handleUser(0); assert(departures==0);
 reset(); armedUser=0; armedAt=1000; openedAt=121000; clockMs=130000; openedFlag=true;
 assert(processDeparture() && captured.uptimeMs==121000); // 처리 지연이 출발 허용 창을 바꾸지 않는다
 reset(); armedUser=0; armedAt=1000; openedAt=121001; clockMs=130000; openedFlag=true;
 assert(!processDeparture() && departures==0);
 reset(); clockMs=UINT32_MAX-2000; weatherAt=clockMs; openOnFetch=1; handleUser(0);
 assert(departures==1 && captured.uptimeMs==UINT32_MAX-1000);
 // B01: 사용자1 정보 표시 중 사용자2가 태그하면 사용자2로 바뀐다
 reset(); handleUser(0); assert(state==ST_SHOW && armedUser==0);
 memcpy(shimCardUid, USERS[1].uid, USERS[1].uidLen); shimCardLen=USERS[1].uidLen;
 loop(); assert(armedUser==1 && curUser==1);
 // N3: 출발 뒤 90초 안에 같은 사람이 다시 태그하면 취소하고, 정보를 다시 보여 주며 감시한다
 reset(); openOnFetch=1; handleUser(0); assert(departures==1 && departedUser==0 && state==ST_MESSAGE);
 clockMs+=10000;
 memcpy(shimCardUid, USERS[0].uid, USERS[0].uidLen); shimCardLen=USERS[0].uidLen;
 loop(); assert(cancels==1 && departedUser==-1 && armedUser==0 && state==ST_SHOW);
 // N2: 태그 직전(50ms 안)에 열린 문도 출발로 본다
 reset(); armedUser=0; armedAt=1000; openedAt=990; clockMs=1100; openedFlag=true;
 assert(processDeparture() && departures==1);
 // N2: 문이 열린 채 태그하면 문이 닫힐 때를 출발로 본다
 reset(); doorOpenNow=true; handleUser(0); assert(departures==0 && armedUser==0);
 clockMs+=5000; doorOpenNow=false; loop(); assert(departures==1);
 // N5: 날씨 조회가 실패한 상태면 30초 뒤 다시 시도한다
 reset(); weather.ok=false; failApis=true; clockMs+=WEATHER_RETRY_MS; loop(); assert(requests==1);
 // N5: 실패해도 30분 안에는 마지막 정상 날씨를 유지한다
 reset(); failApis=true; refreshWeather(); assert(weather.ok);
 // N6: 사람이 감지되는 동안에는 대기 중 날씨 갱신을 미룬다
 reset(); weather.ok=false; pirNow=true; clockMs+=WEATHER_RETRY_MS; loop(); assert(requests==0);
 puts("현관판 통합 흐름 회귀 시험 통과");
}
