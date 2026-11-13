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
bool pirActive(){return false;} bool doorIsOpen(){return openedFlag;}
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
void travelOnCancel(uint8_t){} bool travelPhoneOnline(uint8_t){return true;}
static void reset(){
 clockMs=1000; openedFlag=failApis=false; requests=openOnFetch=departures=signalAttempts=0;
 weatherTried=true; weatherAt=clockMs; weather.ok=true;
 armedUser=curUser=departedUser=-1; state=ST_IDLE;
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
 puts("현관판 통합 흐름 회귀 시험 통과");
}
