# Come Back Home

구조대원 허리 8채널 햅틱 복귀 안내 시스템이다. 폰 앱과 ESP32 펌웨어로 구성한다. 폰은 센서와 GPS를 읽고 ESP32에 BLE로 명령을 보내며, ESP32는 허리의 진동 모터 8개를 구동한다.

## 1. 저장소 구성

```
come-back-home/
├─ android/    안드로이드 앱 (BLE Central, 센서·GPS 기록, 시험 화면)
├─ firmware/   ESP32 펌웨어 (PlatformIO)
│  ├─ src/core/   경로 기록, 백트래킹, 햅틱 신호 알고리즘
│  ├─ src/hw/     BLE, 모터 핀, 버튼 등 하드웨어 연결 코드
│  ├─ src/main.cpp  core와 hw를 연결하는 setup/loop
│  └─ test/       PC에서 돌리는 알고리즘 시뮬레이션 테스트
└─ docs/       통신 규약(protocol.md), 햅틱 신호 체계(haptic_spec.md)
```

- 소프트웨어
  - 앱: `android/`
  - 경로 기록·백트래킹·햅틱 알고리즘: `firmware/src/core/`, `firmware/test/`, `docs/haptic_spec.md`
  - 통신 규약과 `firmware/src/main.cpp`: 소프트웨어 쪽에서 관리한다. 고칠 필요가 있으면 PR 설명에 이유를 적어 요청한다.
- 하드웨어
  - BLE, 모터 핀, 버튼 등 하드웨어 연결 코드: `firmware/src/hw/`

## 2. 환경 세팅 

1. Git을 설치한다. 터미널에서 `git --version`이 나오면 된 것이다.
2. 이름과 이메일을 설정한다. 이메일은 GitHub 계정에 등록한 것을 쓴다.
   ```bash
   git config --global user.name "이름"
   git config --global user.email "github에-등록한-이메일"
   ```
3. 저장소 초대를 수락한다. 저장소 관리자가 보낸 GitHub 초대 메일(또는 github.com 알림)에서 수락해야 올릴 수 있다.
4. `develop` 브랜치를 받는다.
   ```bash
   git clone -b develop https://github.com/subniin/come-back-home.git
   cd come-back-home
   ```
5. 처음 push할 때 GitHub 로그인 창이 뜬다. 브라우저로 로그인하면 된다.

터미널이 어렵다면 GitHub Desktop 앱으로도 같은 작업을 할 수 있다. 아래 설명의 `git` 명령은 앱의 Fetch, Branch, Commit, Push, Create Pull Request 버튼과 1:1로 대응한다.

## 3. 브랜치 규칙

- `main`: 항상 빌드되는 안정 버전이다. 직접 올리지 않고 PR로만 합친다.
- `develop`: 통합 브랜치다. 모든 작업 브랜치는 여기서 시작해서 여기로 합친다.
- 작업 브랜치: `develop`에서 만들고 이름은 `영역/작업내용`으로 쓴다.
  - 앱: `app/ble-nus`, `app/step-detect`
  - 알고리즘: `fw-core/backtrack-fix`
  - 하드웨어: `fw-hw/ble`, `fw-hw/motor-pins`
  - 문서: `docs/protocol`

`main`에는 마일스톤(11/23 펌웨어, 12/03 파일럿)마다 `develop`을 합치고 태그를 붙인다.

## 4. 작업 흐름 (모든 영역 공통)

### 4-1. 작업 시작
```bash
git switch develop
git pull
git switch -c fw-hw/ble          # 작업 브랜치 이름은 상황에 맞게
```

### 4-2. 파일을 고치거나 추가한 뒤 커밋
```bash
git status                       # 바뀐 파일 확인
git add firmware/src/hw/         # 올릴 폴더나 파일을 지정한다 (git add . 는 피한다)
git commit -m "feat: BLE 서비스와 특성 추가"
```
- `git add .`는 의도하지 않은 파일(로그, 키, 빌드 결과)을 올리기 쉽다. 폴더나 파일을 지정해서 추가한다.
- 커밋 메시지는 `종류: 내용` 형식이다. 종류는 `feat`(기능 추가), `fix`(버그 수정), `docs`(문서), `chore`(정리)를 쓴다.
- 커밋은 작업 단위로 자주 나누는 편이 되돌리기 쉽다.

### 4-3. 올리기
```bash
git push -u origin fw-hw/ble     # 처음 올릴 때. 이후에는 git push 만 쓴다
```

### 4-4. Pull Request (PR) 만들기
1. push한 뒤 GitHub 저장소 페이지에 뜨는 "Compare & pull request" 버튼을 누른다.
2. 대상(base) 브랜치가 `develop`인지 확인한다. `main`이 아니다.
3. 설명에 다음을 적는다. 무엇을 바꿨는지, 어떻게 확인했는지, 다른 영역과 맞춰야 할 것이 있는지.
4. 리뷰어를 지정하고 PR을 만든다. 승인을 받으면 합친다.

### 4-5. 다른 사람의 변경 받기
작업이 길어질 때는 가끔 `develop`의 변경을 가져온다.
```bash
git switch develop
git pull
git switch fw-hw/ble
git merge develop
```
충돌(conflict) 메시지가 나오면 혼자 해결하려 하지 말고 해당 파일 담당자에게 물어본다.

## 5. 올리는 방법

### 5-1. 앱 (`android/`)
- Android Studio로 `android/` 폴더를 연다.
- 고친 소스는 `android/core`, `android/sensor`, `android/dashboard` 아래에 있다. 이 폴더들만 `git add` 한다.
- 올리지 않는 것: `build/`, `.gradle/`, `local.properties`, `*.apk`, `*.jks`(서명 키). `.gitignore`가 막아 주지만 `git status`로 한 번 더 확인한다.
- 실기기 확인은 폰을 USB 또는 무선 디버깅으로 연결한 뒤 Android Studio에서 Run 한다.

### 5-2. 펌웨어 알고리즘 (`firmware/src/core/`)
- 이 폴더는 Arduino에 의존하지 않아서 PC에서 빌드와 테스트가 된다. 올리기 전에 테스트를 돌린다.
  ```bash
  cd firmware/test
  g++ -std=c++17 -O2 -I../src/core sim_test.cpp ../src/core/*.cpp -o sim
  ./sim
  ```
- `ALL PASSED`가 나오면 올린다. 실패하면 PR 설명에 이유를 적는다.
- 설정값(보폭, 회전 판정각 등)은 `config.h`에서 바꾼다. 값을 바꾼 이유를 커밋 메시지에 적는다.

### 5-3. 하드웨어·BLE (`firmware/src/hw/`)
- BLE, 모터 핀, 버튼 코드는 `firmware/src/hw/` 안에 둔다. `src/core/`와 `src/main.cpp`는 고치지 않는다.
- Arduino IDE로 만든 스케치(`.ino`)도 그대로 올려도 된다. 폴더 구조에 맞추는 작업은 소프트웨어 쪽에서 한다.
- PR 설명에 반드시 적을 것:
  - 서비스 UUID와 특성(Characteristic) UUID
  - 앱이 보내는 데이터 형식 (바이트 구조 또는 텍스트 명령)
  - ESP32가 앱으로 보내는 알림(Notify)의 형식
  - 사용한 모터 핀 번호
- 펌웨어 빌드는 VS Code + PlatformIO로 `firmware/` 폴더를 열어서 한다. 보드는 ESP32 Dev Module이다. 용량이 부족하면 `platformio.ini`의 `huge_app.csv` 파티션을 쓴다 (이미 설정돼 있다).
- 업로드 후 시리얼 모니터는 115200, 줄 끝 LF로 연다. 폰이 없어도 시리얼로 `START`, `SIM STEP 0 14`, `RETURN` 같은 명령을 넣어 동작을 확인할 수 있다.
- 폰 연결 확인은 nRF Connect 앱에서 ESP32의 서비스와 특성이 보이는지로 먼저 점검한다.

### 5-4. 문서 (`docs/`)
- 통신 규약(`protocol.md`)이 바뀌는 PR은 코드보다 문서를 먼저 고친다. 앱과 펌웨어가 이 문서를 기준으로 맞춘다.
- 회의록과 개인 보고서는 Notion에서 관리한다. 이 저장소에는 올리지 않는다.

## 6. 올리면 안 되는 것

- 서명 키(`*.jks`, `*.keystore`), 비밀번호, API 키
- 센서 기록 파일(`*.csv`)과 로그
- 빌드 결과물(`build/`, `.pio/`, `*.apk`)
- `.hwp`, `.pdf` 같은 문서 원본

실수로 올렸다면 지우기 전에 저장소 관리자에게 먼저 알린다. 키나 비밀번호는 지워도 기록에 남기 때문에 폐기하고 새로 만들어야 한다.

## 7. 유의점 

- `develop`이 아니라 `main`으로 PR을 만들었다. PR 화면 위쪽의 base 브랜치를 `develop`으로 바꾼다.
- push가 거부된다. 초대를 수락했는지, 로그인한 계정이 맞는지 확인한다. `main`에 직접 push하면 거부될 수 있다.
- 작업 브랜치를 만들지 않고 `develop`에서 바로 고쳤다. 아직 커밋 전이라면 `git switch -c 새브랜치`로 지금 변경을 그대로 새 브랜치로 옮길 수 있다.
- 다른 사람의 파일을 고쳤다. 담당 폴더가 아니면 PR 설명에 이유를 적고 담당자를 리뷰어로 지정한다.
- 어떻게 해야 할지 모르겠다. `git status` 결과를 복사해서 팀 채팅에 보내면 상황을 보고 알려 줄 수 있다.

