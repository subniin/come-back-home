// ComeBackHome ESP32 BLE 스켈레톤 (허리 유닛, BLE Peripheral). 정의서 3~6장 기준.
// 보드: ESP32 Dev Module. Arduino-ESP32 코어 2.x와 3.x 둘 다 컴파일되도록
// 값 읽기는 getData()/getLength()를 쓴다 (getValue()의 반환형이 코어 버전마다 다르다).
//
// 동작
//  - 서비스 UUID를 광고한다. 앱이 이 UUID로 스캔해서 찾는다.
//  - GuidanceChar로 6바이트 패킷(v1)을 받아 시리얼에 출력하고, state 1·3에서만 구동한다.
//  - 0.5초 동안 패킷이 없거나 연결이 끊기면 정지한다 (기준안 5.2, 정의서 3.1).
//  - BOOT 버튼(GPIO0)을 누르면 ButtonEventChar로 Notify한다 (0 = 짧게, 1 = 길게).
//  - 연결이 끊기면 광고를 다시 시작한다. 앱의 자동 재연결에 필요하다.
//
// 미구현 (TODO)
//  - 모터 8개 핀과 sector 번호 매핑. 지금은 내장 LED로 구동 여부만 표시한다.
//  - v2 패킷(7바이트, cue). 길이가 6이 아니면 무시한다.

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char* SERVICE_UUID  = "8c5a8775-542e-4447-a6d6-572d509d4938";
static const char* GUIDANCE_UUID = "8b2e38a4-d5bb-41a2-9378-7dd07e3d64dc";  // 폰 -> ESP32, Write Without Response
static const char* BUTTON_UUID   = "383de663-08ec-437e-b633-58292aec7c29";  // ESP32 -> 폰, Notify

const uint32_t RX_TIMEOUT_MS = 500;  // 무수신 정지
const int BUTTON_PIN = 0;            // DevKitC BOOT 버튼. 실제 버튼 핀이 정해지면 바꾼다

// 정의서 4장. little-endian은 ESP32 기본 순서와 같다.
struct __attribute__((packed)) GuidancePacket {
  int16_t turnAngle;    // -180~180, 음수 = 좌, 양수 = 우
  uint16_t distanceCm;
  uint8_t state;        // 0 대기, 1 아웃사이드, 2 인사이드 기록, 3 백트래킹
  uint8_t seq;
};
static_assert(sizeof(GuidancePacket) == 6, "packet must be 6 bytes");

// BLE 콜백은 다른 태스크에서 실행되므로 공유 값은 mux로 보호한다.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
GuidancePacket pkt = {0, 0, 0, 0};
volatile bool hasNew = false;
volatile uint32_t lastRxMs = 0;
volatile bool connected = false;
volatile bool needAdvertise = false;
BLECharacteristic* buttonChar = nullptr;

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected = true; }
  void onDisconnect(BLEServer*) override {
    connected = false;
    needAdvertise = true;  // 광고 재시작은 loop()에서 한다
  }
};

class GuidanceCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (c->getLength() != sizeof(GuidancePacket)) return;  // v1 6바이트만 처리
    portENTER_CRITICAL(&mux);
    memcpy(&pkt, c->getData(), sizeof(pkt));
    lastRxMs = millis();
    hasNew = true;
    portEXIT_CRITICAL(&mux);
  }
};

// 0 = 정면, 시계 방향으로 45도마다 +1. 모터 번호와의 대응은 회의에서 정한 뒤 확정한다.
int sectorOf(int16_t turnAngle) {
  int a = ((turnAngle % 360) + 360) % 360;
  return ((a * 2 + 45) / 90) % 8;
}

void motorsOff() {
  digitalWrite(LED_BUILTIN, LOW);
  // TODO: 모터 8개 핀 모두 LOW
}

void driveSector(int sector) {
  digitalWrite(LED_BUILTIN, HIGH);
  // TODO: sector번 모터만 구동하고 나머지는 LOW
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  motorsOff();

  BLEDevice::init("CBH-ESP32");  // 앱은 이름을 쓰지 않는다. 아무 이름이어도 된다
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService* svc = server->createService(SERVICE_UUID);

  BLECharacteristic* guidance = svc->createCharacteristic(
      GUIDANCE_UUID, BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_WRITE);
  guidance->setCallbacks(new GuidanceCb());

  buttonChar = svc->createCharacteristic(BUTTON_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  buttonChar->addDescriptor(new BLE2902());  // CCCD. 코어 3.x에서는 deprecated 경고가 나올 수 있으나 동작한다

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);  // 앱 스캔 필터가 광고에서 이 UUID를 본다
  adv->setScanResponse(true);         // 기기 이름은 스캔 응답으로 (광고 패킷 31바이트 제한)
  BLEDevice::startAdvertising();
  Serial.println("advertising");
}

void loop() {
  uint32_t now = millis();

  if (needAdvertise && !connected) {  // 끊긴 뒤 광고 재시작
    delay(300);
    BLEDevice::startAdvertising();
    needAdvertise = false;
    Serial.println("re-advertising");
  }

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
    Serial.printf("rx seq=%u turn=%d dist=%ucm state=%u sector=%d\n",
                  p.seq, p.turnAngle, p.distanceCm, p.state, sectorOf(p.turnAngle));
  }

  bool alive = connected && (now - rx) <= RX_TIMEOUT_MS;
  if (alive && (p.state == 1 || p.state == 3)) driveSector(sectorOf(p.turnAngle));
  else motorsOff();

  // 버튼: 뗄 때 누른 시간으로 짧게/길게를 정한다 (정의서 6장)
  static bool wasDown = false;
  static uint32_t downMs = 0;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !wasDown) downMs = now;
  if (!down && wasDown && now - downMs > 30) {  // 간단한 디바운스
    uint8_t type = (now - downMs >= 1000) ? 1 : 0;
    if (connected) {
      buttonChar->setValue(&type, 1);
      buttonChar->notify();
    }
    Serial.printf("button type=%u\n", type);
  }
  wasDown = down;

  delay(5);
}
