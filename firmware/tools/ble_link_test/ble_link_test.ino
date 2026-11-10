// ESP32 BLE 연결 테스트 스케치 (앱 <-> ESP32 링크 확인용)
//
// 목적: 안드로이드 앱의 BleLink 와 통신이 되는지 단독으로 확인한다.
//       모터 구동, 경로 계산은 하지 않는다. (그건 firmware/src/main.cpp)
// 업로드: Arduino IDE 에서 이 파일을 열고 보드 "ESP32 Dev Module" 선택 후 업로드.
//         시리얼 모니터 115200.
// 프로토콜: 앱 BleLink.java / core/GuidancePacket.java 와 같아야 한다.
//   - 서비스 UUID, 가이던스 특성(앱 -> ESP32, WRITE), 버튼 특성(ESP32 -> 앱, NOTIFY)
//   - 패킷 6바이트 little-endian: int16 turnAngle, uint16 distanceCm, uint8 state, uint8 seq
//   - 버튼 notify 1바이트: 0 = 짧게, 1 = 길게(1초 이상)
// 주의: firmware/src/main.cpp 는 Nordic UART(NUS) 방식이라 UUID 가 다르다.
//       둘 중 어느 쪽으로 통일할지는 팀 결정 후 정리한다.

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstring>

// ===== 앱(BleLink.java)과 동일한 UUID =====
static const char* SERVICE_UUID  = "8c5a8775-542e-4447-a6d6-572d509d4938";
static const char* GUIDANCE_UUID = "8b2e38a4-d5bb-41a2-9378-7dd07e3d64dc";
static const char* BUTTON_UUID   = "383de663-08ec-437e-b633-58292aec7c29";

// ===== 설정 =====
static const char*    DEVICE_NAME        = "CBH-ESP32";
static const uint32_t RX_TIMEOUT_MS      = 500;   // 이 시간 동안 패킷이 없으면 끊긴 것으로 본다
static const uint32_t READVERTISE_MS     = 300;   // 연결 해제 후 광고 재시작까지 대기
static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t BUTTON_LONG_MS     = 1000;
static const int      BUTTON_PIN         = 0;     // BOOT 버튼

// ===== 앱에서 보내는 6바이트 패킷 (ESP32 는 little-endian 이라 그대로 복사한다) =====
struct __attribute__((packed)) GuidancePacket {
  int16_t  turnAngle;
  uint16_t distanceCm;
  uint8_t  state;
  uint8_t  seq;
};
static_assert(sizeof(GuidancePacket) == 6, "Packet size must be 6 bytes");

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

GuidancePacket pkt = {0, 0, 0, 0};
volatile bool     hasNew            = false;
volatile uint32_t lastRxMs          = 0;      // 0 = 아직 받은 적 없음
volatile bool     connected         = false;
volatile bool     connectionChanged = false;
volatile uint32_t disconnectedAtMs  = 0;      // 0 = 재광고 필요 없음

BLECharacteristic* buttonChar = nullptr;

// ===== BLE 연결 이벤트 =====
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    lastRxMs = 0;
    connectionChanged = true;
  }
  void onDisconnect(BLEServer*) override {
    connected = false;
    connectionChanged = true;
    disconnectedAtMs = millis() ? millis() : 1;
  }
};

// ===== 안드로이드 데이터 수신 =====
class GuidanceCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (c->getLength() != sizeof(GuidancePacket)) return;  // 로그는 loop 에서 (콜백 안 출력 피함)
    portENTER_CRITICAL(&mux);
    memcpy(&pkt, c->getData(), sizeof(pkt));
    lastRxMs = millis();
    hasNew = true;
    portEXIT_CRITICAL(&mux);
  }
};

// 방향(앱 기준 각도)을 8개 구역으로 변환. 0 = 정면, 시계 방향 45도씩.
int sectorOf(int16_t turnAngle) {
  int a = ((turnAngle % 360) + 360) % 360;
  return ((a + 22) / 45) % 8;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println("======================");
  Serial.println("ComeBackHome BLE link test");
  Serial.println("======================");

  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService* service = server->createService(SERVICE_UUID);

  // 휴대폰 -> ESP32
  BLECharacteristic* guidance = service->createCharacteristic(
      GUIDANCE_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  guidance->setCallbacks(new GuidanceCb());

  // ESP32 -> 휴대폰
  buttonChar = service->createCharacteristic(BUTTON_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  buttonChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);   // 앱은 이 서비스 UUID 로 스캔한다
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.print("광고 시작, 기기 이름: ");
  Serial.println(DEVICE_NAME);
}

void loop() {
  const uint32_t now = millis();

  if (connectionChanged) {
    connectionChanged = false;
    Serial.println(connected ? "BLE 연결 성공" : "BLE 연결 해제");
  }

  // 연결 해제 후 일정 시간 뒤 광고 재시작 (delay 로 막지 않는다)
  if (!connected && disconnectedAtMs && now - disconnectedAtMs >= READVERTISE_MS) {
    disconnectedAtMs = 0;
    BLEDevice::startAdvertising();
    Serial.println("광고 재시작");
  }

  // 수신 데이터 확인
  GuidancePacket p;
  bool fresh;
  uint32_t rx;
  portENTER_CRITICAL(&mux);
  p = pkt;
  fresh = hasNew;
  hasNew = false;
  rx = lastRxMs;
  portEXIT_CRITICAL(&mux);

  if (fresh) {
    Serial.printf("RX seq=%u angle=%d dist=%ucm state=%u sector=%d\n",
                  p.seq, p.turnAngle, p.distanceCm, p.state, sectorOf(p.turnAngle));
  }

  // 패킷 수신 상태 변화만 출력
  static bool previousAlive = false;
  const bool alive = connected && rx != 0 && (now - rx <= RX_TIMEOUT_MS);
  if (alive != previousAlive) {
    previousAlive = alive;
    if (alive) Serial.println("가이던스 수신 정상");
    else if (connected) Serial.println("500ms 이상 수신 없음");
  }

  // BOOT 버튼: 떼는 순간 짧게(0)/길게(1) 구분해서 notify
  static bool wasDown = false;
  static uint32_t downMs = 0;
  const bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !wasDown) downMs = now;
  if (!down && wasDown && now - downMs > BUTTON_DEBOUNCE_MS) {
    uint8_t type = (now - downMs >= BUTTON_LONG_MS) ? 1 : 0;
    if (connected) {
      buttonChar->setValue(&type, 1);
      buttonChar->notify();
    }
    Serial.printf("버튼 이벤트: %u\n", type);
  }
  wasDown = down;

  delay(5);
}
