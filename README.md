# Come Back Home

구조대원 허리 8채널 햅틱 복귀 안내 시스템. 폰 앱과 ESP32 펌웨어로 구성한다.

## 폴더
- `android/` 안드로이드 앱 (BLE Central, 센서·GPS 기록, 시험 화면)
- `firmware/` ESP32 펌웨어 (PlatformIO). 알고리즘은 `src/core/`, 연결부는 `src/main.cpp`
- `docs/` 통신 규약(`protocol.md`), 햅틱 신호 체계(`haptic_spec.md`)
- `firmware/esp32_ble_v1/` 앱 시험용 BLE 스켈레톤 (자체 UUID, 6바이트 패킷). 통신 방식이 확정되면 정리한다

## 브랜치
- `main` 항상 빌드되는 상태. PR로만 합친다
- `develop` 통합용
- 작업 브랜치는 `develop`에서 만든다: `app/...`, `fw-core/...`, `fw-hw/...`, `docs/...`

## 빌드
- 앱: Android Studio로 `android/` 열기
- 펌웨어: VS Code + PlatformIO로 `firmware/` 열기
- 알고리즘 PC 테스트: `cd firmware/test && g++ -std=c++17 -O2 -I../src/core sim_test.cpp ../src/core/*.cpp -o sim && ./sim`
