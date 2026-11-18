# Come Back Home: 경로 기록 및 복귀 안내 펌웨어 (ESP32)

## 폴더 구조
```
ComebackHome/
├─ platformio.ini          VS Code(PlatformIO) 빌드 설정
├─ src/main.cpp        ESP32 연결부 (setup/loop, 모터 핀, BLE, 버튼, 센서 어댑터)
├─ src/core/               알고리즘 본체 (Arduino 의존성 없음 → PC에서도 빌드됨)
│   ├─ config.h            튜닝 값 모음 (보폭, 회전 판정각, 병합 반경 등)
│   ├─ geo.h               각도 계산, 상대각 → 모터 번호
│   ├─ haptic.*            8채널 햅틱 신호 체계 (docs/haptic_spec.md (저장소 루트의 docs/))
│   ├─ path_graph.*        경로 그래프 + 기록기 + Dijkstra
│   ├─ return_guide.*      복귀 안내, 차단 시 백트래킹
│   ├─ step_detector.h     간단한 걸음 검출
│   ├─ pdr_selftest.*      SELFTEST 명령: 걸음 데이터 변수로 백트래킹 알고리즘만 확인
│   └─ controller.*        단계 관리(기록→복귀→도착), 버튼·명령어 처리
├─ test/sim_test.cpp       PC 시뮬레이션 테스트 (백트래킹 시나리오는 SELFTEST와 공유)
└─ docs/haptic_spec.md (저장소 루트의 docs/)     햅틱 신호 체계 (다른 팀 공유용)
```
VS Code(PlatformIO)는 폴더를 열고 빌드하면 되고, 보드는 *ESP32 Dev Module*이다. 저장소에는 Arduino IDE용 .ino가 없다.

## 범위
이 펌웨어는 **PDR 경로 기록 + 백트래킹 복귀 안내**만 담당한다. 실외 GPS 안내, ToF 장애물 자동 차단, 원격 조종(`DIR`/`SIG`), 연결 끊김 시 자율 모드, 사전 지도(`MAP`), 햅틱 인지 실험 모드(`TEST`)는 범위에서 제외했다.

버튼(BOOT)은 누를 때마다 단계를 넘긴다: 대기 → 기록 시작 → 복귀 시작 → (복귀 중) 취소.

## 알고리즘 요약
1. **기록:** 걸음마다 `x += 보폭·sin(헤딩)`, `y += 보폭·cos(헤딩)`로 위치를 갱신한다. 35° 이상 꺾인 상태가 2걸음 연속 이어지면 꺾인 지점을 노드로 저장한다. 직선 구간에서도 8m마다 노드를 하나씩 둔다.
2. **병합:** 이미 저장된 노드에서 1.5m 이내로 다시 오면 새 노드를 만들지 않고 기존 노드에 연결한다. 그래서 막다른 길을 갔다가 되돌아온 구간은 복귀할 때 자동으로 건너뛴다.
3. **복귀:** 현재 노드에서 출발 노드까지 Dijkstra로 최단경로를 구한 뒤, 다음 웨이포인트 방향과 내 헤딩의 차이를 모터 번호(0~7)로 바꿔 진동을 준다.
4. **차단:** `BLOCK` 명령이 오면 지금 가던 간선을 막힌 것으로 표시한다. 그다음 마지막으로 지나온 노드로 되돌아가 다른 경로를 다시 계산한다. 다른 경로가 없으면 `NO_ROUTE` 신호를 준다.

## 센서 없이 바로 테스트 (시리얼 모니터 115200, LF)
```
START                 ← 기록 시작
SIM STEP 0 14         ← 북쪽으로 14걸음
SIM STEP 90 10        ← 동쪽으로 10걸음
RETURN                ← 복귀 안내 시작 → GUIDE,dir=... 로그와 진동 확인
SIM HEAD 225          ← 몸을 돌리면 울리는 모터가 바뀜
SIM STEP 225 5
BLOCK                 ← 경로 차단 → 재탐색
DUMP                  ← 노드/간선/경로 CSV (실제 경로와 비교·시각화용)
```
휴대폰은 *nRF Connect*나 *Serial Bluetooth Terminal* 앱으로 `ComeBackHome`에 연결해서 같은 명령을 보내면 된다.

## 알고리즘만 확인 (SELFTEST)
시리얼 모니터에 `SELFTEST`를 입력하면 `src/core/pdr_selftest.cpp` 맨 위에 변수로 정의한 걸음 데이터 4종(ㄱ자, 막다른 길, 순환로+차단, 외길+차단)을 알고리즘에 넣고 PASS/FAIL을 출력한다. `SELFTEST NOISE 15`로 헤딩 노이즈를 바꿀 수 있다.

## PC 테스트
```
cd test
g++ -std=c++17 -O2 -I../src/core sim_test.cpp ../src/core/*.cpp -o sim && ./sim   # -v: 자세한 로그
```

## 팀원과 맞춰야 할 것
- **센서/앱 담당:** 폰 앱이 BLE로 `IMU <accG> <headDeg>`를 50Hz로 보내면 된다. ESP32에 센서를 직접 달 경우에는 `src/main.cpp`의 `readImu`를 채운다. 헤딩은 0~360° 시계방향이고, 실내에서는 지자기보다 자이로 기반 헤딩을 권장한다.
- **하드웨어 담당:** `MOTOR_PINS` 핀 번호를 맞춰야 한다. 모터는 드라이버를 거쳐 연결해야 하며, 전체 진동(STOP, NO_ROUTE) 신호에서는 8개가 동시에 켜지므로 전류 용량을 확인해야 한다.
- **앱 담당:** 펌웨어가 받는 명령은 `START`, `RETURN`, `CANCEL`, `MARK`, `BLOCK`, `SIM`, `SELFTEST`, `STATUS`, `DUMP`, `TRACK`뿐이다. 그 밖의 명령(`ENV`, `AUTO`, `DIR`, `OFF` 등)은 `ERR,unknown command`로 응답한다.

## 한계
- PDR은 걸을수록 오차가 쌓인다. 50m 정도까지는 현실적인 범위지만, 그 이상은 UWB 같은 보정 수단이 필요하다.
- 노드 병합은 PDR 좌표 기준이라서, 드리프트가 크면 실제로는 다른 지점인데 같은 노드로 합쳐질 수 있다. `MERGE_RADIUS_M`을 실험으로 조정해야 한다.
- 시뮬레이션은 사용자가 진동을 그대로 따라간다고 가정한 논리 검증이다. 실제 복귀 성공률은 필드 테스트로 측정해야 한다.
