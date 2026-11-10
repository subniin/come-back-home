// ESP32 BLE 연결 테스트 스케치 (앱 <-> ESP32 링크 확인용)
//
// 목적: 안드로이드 앱의 BleLink 와 통신이 되는지 단독으로 확인한다.
//       경로 기록, 모터 구동은 하지 않는다. (그건 firmware/src/main.cpp)
// 업로드: Arduino IDE 에서 이 파일을 열고 보드 "ESP32 Dev Module" 선택 후 업로드.
//         시리얼 모니터 115200.
// 프로토콜: firmware/src/main.cpp 와 앱 BleLink.java 가 쓰는 Nordic UART Service(NUS) 텍스트 방식.
//   - 서비스 6E400001, RX 6E400002(앱 -> ESP32, WRITE), TX 6E400003(ESP32 -> 앱, NOTIFY)
//   - 한 줄은 '\n' 으로 끝나는 텍스트. TX 는 20바이트 조각으로 나눠 보낸다.
//   - 앱이 보내는 줄: 'TRACK ON' (연결 직후 1회), 모드 버튼 명령, 'IMU <accG> <headDeg>' (50Hz)
//   - 이 스케치가 돌려주는 줄: 'TRACK,ON' (TRACK ON 응답), 'ECHO,<받은 줄>' (IMU 제외),
//     'IMU,<초당 수신 수>' (1초마다), 'BTN,SHORT' / 'BTN,LONG' (BOOT 버튼)
// 앱 화면의 수신 로그에 위 줄이 보이면 양방향 링크가 정상이다.

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstring>

// ===== Nordic UART Service (앱 BleLink.java 와 동일) =====
static const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // 앱 -> ESP32
static const char* TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // ESP32 -> 앱

// ===== 설정 =====
static const char*    DEVICE_NAME        = "ComeBackHome";
static const uint32_t READVERTISE_MS     = 300;   // 연결 해제 후 광고 재시작까지 대기
static const uint32_t BUTTON_DEBOUNCE_MS = 30;
static const uint32_t BUTTON_LONG_MS     = 1500;  // main.cpp 와 같은 길게 누름 기준
static const int      BUTTON_PIN         = 0;     // BOOT 버튼
static const size_t   CHUNK              = 20;    // 기본 MTU(23) 기준 notify 최대 길이
static const size_t   LINE_MAX           = 96;

// ===== 상태 (BLE 콜백 스레드와 loop 가 함께 쓴다) =====
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
char     rxBuf[512];            // 콜백이 받은 바이트를 loop 로 넘기는 링 버퍼
volatile size_t rxHead = 0, rxTail = 0;

volatile bool     connected         = false;
volatile bool     connectionChanged = false;
volatile uint32_t disconnectedAtMs  = 0;      // 0 = 재광고 필요 없음

BLECharacteristic* txChar = nullptr;

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    connectionChanged = true;
  }
  void onDisconnect(BLEServer*) override {
    connected = false;
    connectionChanged = true;
    uint32_t now = millis();
    disconnectedAtMs = now ? now : 1;
  }
};

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    const uint8_t* d = c->getData();
    size_t n = c->getLength();
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < n; i++) {
      size_t next = (rxHead + 1) % sizeof(rxBuf);
      if (next == rxTail) break;  // 가득 차면 버린다
      rxBuf[rxHead] = (char)d[i];
      rxHead = next;
    }
    portEXIT_CRITICAL(&mux);
  }
};

// 한 줄을 20바이트씩 나눠 notify 한다 (끝에 '\n' 을 붙인다)
void sendLine(const char* s) {
  if (!connected || !txChar) return;
  char line[LINE_MAX + 2];
  snprintf(line, sizeof(line), "%s\n", s);
  size_t len = strlen(line);
  for (size_t off = 0; off < len; off += CHUNK) {
    size_t n = min(CHUNK, len - off);
    txChar->setValue((uint8_t*)line + off, n);
    txChar->notify();
    delay(5);  // 연속 notify 가 버퍼에서 밀리지 않게 한다
  }
}

// loop 에서 한 줄이 완성될 때마다 호출된다
void onLine(const char* line, uint32_t now) {
  static uint32_t imuCount = 0, windowStart = 0;
  if (strncmp(line, "IMU", 3) == 0) {   // 50Hz 라서 하나하나 출력하지 않고 초당 개수만 센다
    imuCount++;
  } else {
    Serial.printf("RX: %s\n", line);
    char out[LINE_MAX + 8];
    if (strcmp(line, "TRACK ON") == 0) snprintf(out, sizeof(out), "TRACK,ON");
    else snprintf(out, sizeof(out), "ECHO,%.80s", line);
    sendLine(out);
  }
  if (now - windowStart >= 1000) {
    if (imuCount) {
      Serial.printf("IMU %u/s\n", (unsigned)imuCount);
      char out[24];
      snprintf(out, sizeof(out), "IMU,%u", (unsigned)imuCount);
      sendLine(out);
    }
    imuCount = 0;
    windowStart = now;
  }
}

void pollRx(uint32_t now) {
  static char line[LINE_MAX];
  static size_t len = 0;
  for (;;) {
    char ch;
    portENTER_CRITICAL(&mux);
    bool has = rxTail != rxHead;
    if (has) {
      ch = rxBuf[rxTail];
      rxTail = (rxTail + 1) % sizeof(rxBuf);
    }
    portEXIT_CRITICAL(&mux);
    if (!has) break;
    if (ch == '\n') {
      if (len) { line[len] = 0; onLine(line, now); len = 0; }
    } else if (ch != '\r' && len < LINE_MAX - 1) {
      line[len++] = ch;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println("======================");
  Serial.println("ComeBackHome BLE link test (NUS)");
  Serial.println("======================");

  BLEDevice::init(DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService* service = server->createService(SERVICE_UUID);

  // 앱 -> ESP32
  BLECharacteristic* rx = service->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCb());

  // ESP32 -> 앱
  txChar = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

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

  pollRx(now);

  // BOOT 버튼: 떼는 순간 짧게/길게 구분해서 notify
  static bool wasDown = false;
  static uint32_t downMs = 0;
  const bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !wasDown) downMs = now;
  if (!down && wasDown && now - downMs > BUTTON_DEBOUNCE_MS) {
    bool isLong = now - downMs >= BUTTON_LONG_MS;
    Serial.printf("버튼 이벤트: %s\n", isLong ? "LONG" : "SHORT");
    sendLine(isLong ? "BTN,LONG" : "BTN,SHORT");
  }
  wasDown = down;

  delay(2);
}
