// ============================================================================
//  Come Back Home - 구조대원 햅틱 복귀 안내 (ESP32, Arduino 코어)
//  담당: 경로 기록 및 복귀 안내 알고리즘
//
//  - 알고리즘 본체는 src/core/ (하드웨어 독립, PC에서 시뮬레이션 테스트 가능)
//  - 이 파일(src/main.cpp)은 ESP32 하드웨어 연결부: 모터 핀, BLE(휴대폰), 버튼, 센서 어댑터
//  - .ino가 아닌 .cpp라서 PlatformIO/Arduino IDE 모두 변환 과정 없이 그대로 빌드됨
//  - 센서 연결 전에도 시리얼 모니터(115200, 줄바꿈 LF)에서 SIM 명령으로 전체 흐름 확인 가능
// ============================================================================
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "core/controller.h"
#include "core/step_detector.h"

using namespace chh;

// ---------------- 핀 배치 (하드웨어 담당과 맞출 것) ----------------
// 모터 번호: 0=정면(배꼽) 1=오른앞 2=오른쪽 3=오른뒤 4=뒤(등) 5=왼뒤 6=왼쪽 7=왼앞
// ※ 모터는 반드시 트랜지스터/드라이버(예: ULN2803)를 거쳐 연결. GPIO 직결 금지
static const uint8_t MOTOR_PINS[8] = {25, 14, 26, 13, 27, 33, 32, 23};
// ※ 각 모터 핀에는 작동 확인용 LED가 같이 연결되어 있다(모터와 함께 켜짐). 코드는 모터만 제어하면 된다.
static const uint8_t BUTTON_PIN = 0;  // DevKit의 BOOT 버튼 (짧게: 시작/복귀/취소, 길게: 실내↔실외)

// 허리 유닛 조작 스위치 4개 (INPUT_PULLUP, 누르면 LOW). 21/22는 ESP32 기본 I2C 핀이므로 I2C 센서를 쓰면 핀을 옮길 것
static const uint8_t SWITCH_PINS[4] = {18, 19, 21, 22};
// 0: 경로 기록 시작(START)  1: 복귀 안내 시작(RETURN)  2: 실내↔실외 전환  3: 복귀 취소(CANCEL)

// ---------------- BLE (Nordic UART Service: 'nRF Connect', 'Serial Bluetooth Terminal' 앱으로 바로 테스트) ----
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // 폰 → 장치
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // 장치 → 폰

Controller   ctrl;
StepDetector stepDet;

BLEServer*         bleServer = nullptr;
BLECharacteristic* txChar = nullptr;
volatile bool      bleConnected = false;  // 한 대라도 연결되어 있으면 true
volatile uint8_t   bleClients = 0;

struct CmdMsg { char s[96]; };
QueueHandle_t cmdQueue;  // BLE 콜백(다른 태스크) → loop()로 명령 전달

// ============================================================================
//  하드웨어 콜백
// ============================================================================
void motorWrite(uint8_t i, bool on) {
  digitalWrite(MOTOR_PINS[i], on ? HIGH : LOW);
}

void logLine(const char* s) {
  Serial.println(s);
  if (bleConnected && txChar) {  // 기본 MTU(20바이트) 단위로 쪼개서 전송
    size_t len = strlen(s);
    for (size_t off = 0; off < len; off += 20) {
      size_t n = (len - off > 20) ? 20 : len - off;
      txChar->setValue((uint8_t*)(s + off), n);
      txChar->notify();
    }
    txChar->setValue((uint8_t*)"\n", 1);
    txChar->notify();
  }
}

class ServerCB : public BLEServerCallbacks {
  // 폰 2대(센싱 앱 + 대시보드 앱)가 동시에 붙는다. 연결 수를 세고 광고를 계속 켜 둔다.
  void onConnect(BLEServer* s) override {
    bleClients++;
    bleConnected = true;
    s->startAdvertising();  // 이미 한 대 붙어도 다음 폰이 찾을 수 있게
  }
  void onDisconnect(BLEServer* s) override {
    if (bleClients > 0) bleClients--;
    bleConnected = bleClients > 0;
    s->startAdvertising();  // 재연결 허용
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue().c_str();  // 코어 2.x(std::string) / 3.x(String) 모두 호환
    int start = 0;
    while (start < (int)v.length()) {
      int end = v.indexOf('\n', start);
      if (end < 0) end = v.length();
      String line = v.substring(start, end);
      line.trim();
      if (line.length()) {
        CmdMsg m;
        strncpy(m.s, line.c_str(), sizeof(m.s) - 1);
        m.s[sizeof(m.s) - 1] = 0;
        xQueueSend(cmdQueue, &m, 0);
      }
      start = end + 1;
    }
  }
};

void setupBle() {
  BLEDevice::init("ComeBackHome");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCB());
  BLEService* svc = bleServer->createService(NUS_SERVICE_UUID);
  txChar = svc->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  BLECharacteristic* rx = svc->createCharacteristic(
      NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  svc->start();
  bleServer->getAdvertising()->addServiceUUID(NUS_SERVICE_UUID);
  bleServer->getAdvertising()->start();
}

// ============================================================================
//  센서 어댑터  ← 센서 담당 코드와 연결할 부분 (지금은 비어 있음 → SIM 명령으로 테스트)
// ============================================================================
// accMagG : 가속도 벡터 크기 (g 단위, 정지 시 약 1.0)
// headingDeg : 0~360, 시계방향.
//   - 실외: 지자기 기반 자북 헤딩 (진북 보정은 config의 MAG_DECLINATION_DEG에서 처리)
//   - 실내: 철골 때문에 지자기가 틀어지므로 자이로 적분/센서 융합 헤딩 권장
bool readImu(float& accMagG, float& headingDeg) {
  // TODO: 예) BNO055/MPU9250 등에서 읽기
  (void)accMagG; (void)headingDeg;
  return false;
}
// TODO: TinyGPSPlus 등으로 파싱. 새 데이터가 있을 때 true
bool readGps(double& lat, double& lon, bool& fix) {
  (void)lat; (void)lon; (void)fix;
  return false;
}
// 전방 거리(m). ToF/초음파 미사용이면 false 반환 그대로 두면 됨
bool readTof(float& distM) {
  (void)distM;
  return false;
}

// ============================================================================
void setup() {
  Serial.begin(115200);
  for (uint8_t p : MOTOR_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  for (uint8_t p : SWITCH_PINS) pinMode(p, INPUT_PULLUP);

  cmdQueue = xQueueCreate(8, sizeof(CmdMsg));
  ctrl.begin(motorWrite, logLine);
  setupBle();
}

void handleButton(uint32_t now) {
  static bool last = HIGH;
  static uint32_t downAt = 0;
  static bool longFired = false;
  bool cur = digitalRead(BUTTON_PIN);
  if (cur != last) {
    delay(15);  // 간단 디바운스
    cur = digitalRead(BUTTON_PIN);
    if (cur != last) {
      if (cur == LOW) { downAt = now; longFired = false; }
      else if (!longFired) ctrl.onButtonShort(now);
      last = cur;
    }
  }
  if (cur == LOW && !longFired && now - downAt > 1500) {
    longFired = true;
    ctrl.onButtonLong(now);
  }
}

// 스위치 4개: 누르는 순간(LOW) 한 번만 동작, 30ms 디바운스. 눌림도 시리얼/BLE 로그로 남김
void handleSwitches(uint32_t now) {
  static bool stable[4] = {HIGH, HIGH, HIGH, HIGH}, raw[4] = {HIGH, HIGH, HIGH, HIGH};
  static uint32_t changedAt[4] = {0};
  for (int i = 0; i < 4; i++) {
    bool r = digitalRead(SWITCH_PINS[i]);
    if (r != raw[i]) { raw[i] = r; changedAt[i] = now; }
    if (now - changedAt[i] < 30 || raw[i] == stable[i]) continue;
    stable[i] = raw[i];
    if (stable[i] != LOW) continue;
    char msg[32];
    snprintf(msg, sizeof(msg), "SW,%d,GPIO%d", i + 1, SWITCH_PINS[i]);
    logLine(msg);
    if (i == 0) ctrl.onCommand("START", now);
    else if (i == 1) ctrl.onCommand("RETURN", now);
    else if (i == 2) ctrl.onButtonLong(now);
    else ctrl.onCommand("CANCEL", now);
  }
}

// 줄 하나 처리 (시리얼/BLE 공용)
// IMU <accG> <headDeg>: 50Hz 센서 입력이라 로그 없이 조용히 처리, 잘못된 줄은 무시
// 그 외 줄: 텍스트 명령으로 ctrl에 넘김
void handleLine(const char* line, uint32_t now) {
  if (strncmp(line, "IMU", 3) == 0 && (line[3] == ' ' || line[3] == 0)) {
    const char* p = line + 3;
    char* end;
    float acc = strtof(p, &end);
    if (end == p) return;
    p = end;
    float head = strtof(p, &end);
    if (end == p) return;
    ctrl.onHeading(head);
    if (stepDet.update(acc, now)) ctrl.onStep(head, now);
    return;
  }
  ctrl.onCommand(line, now);
}

void readSerial(uint32_t now) {
  static char buf[96];
  static int len = 0;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (len) { buf[len] = 0; handleLine(buf, now); len = 0; }
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = ch;
    }
  }
}

void loop() {
  uint32_t now = millis();

  // 1) 명령 (시리얼 / BLE)
  readSerial(now);
  CmdMsg m;
  while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE) handleLine(m.s, now);

  // 2) 연결 상태
  ctrl.onLink(bleConnected, now);

  // 3) 센서 (50Hz)
  static uint32_t lastImu = 0;
  if (now - lastImu >= 20) {
    lastImu = now;
    float acc, head;
    if (readImu(acc, head)) {
      ctrl.onHeading(head);
      if (stepDet.update(acc, now)) ctrl.onStep(head, now);
    }
    float d;
    if (readTof(d)) ctrl.onObstacle(d, now);
  }
  double lat, lon;
  bool fix;
  if (readGps(lat, lon, fix)) ctrl.onGps(lat, lon, fix, now);

  // 4) 버튼
  handleButton(now);
  handleSwitches(now);

  // 5) 상태 갱신 + 진동 출력
  ctrl.tick(now);
}
