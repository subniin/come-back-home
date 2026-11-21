// PC 시뮬레이션 테스트 (ESP32 없이 알고리즘 검증)
//   빌드:  g++ -std=c++17 -O2 -I../src/core sim_test.cpp ../src/core/*.cpp -o sim && ./sim
//   자세한 로그:  ./sim -v
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "controller.h"
#include "geo.h"
#include "pdr_selftest.h"

using namespace chh;

static bool verbose = false;
static int fails = 0;
static uint8_t motors = 0;

static void motorWrite(uint8_t i, bool on) { if (on) motors |= 1 << i; else motors &= ~(1 << i); }
static void logFn(const char* s) {
  if (verbose) printf("    | %s\n", s);
}
#define CHECK(cond, msg)                                         \
  do {                                                           \
    if (cond) printf("  [PASS] %s\n", msg);                      \
    else { printf("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); fails++; } \
  } while (0)

static uint32_t T = 0;
static float noise(float amp) { return amp * ((rand() / (float)RAND_MAX) * 2 - 1); }

// 진입: heading 방향으로 n걸음 (헤딩 노이즈 포함)
static void walk(Controller& c, float heading, int n, float noiseDeg = 4) {
  for (int i = 0; i < n; i++) { T += 600; c.onStep(wrap360(heading + noise(noiseDeg)), T); c.tick(T); }
}

// 피험자가 진동만 보고 따라간다고 가정: 울리는 모터 방향으로 몸을 돌리고 한 걸음
static int followGuidance(Controller& c, float& heading, int maxSteps, int blockAtStep = -1) {
  int steps = 0;
  for (; steps < maxSteps; steps++) {
    T += 150; c.tick(T);
    if (c.phase() != Phase::RETURNING) break;
    if (steps == blockAtStep) c.onCommand("BLOCK", T);
    uint8_t d = c.guideDir();
    if (d < 8) heading = wrap360(heading + d * 45.0f + noise(10));
    T += 450;
    c.onStep(heading, T);
  }
  return steps;
}

static void testSector() {
  printf("\n[1] 상대각 → 모터 번호\n");
  CHECK(sectorOf(0, 255, 7) == 0 && sectorOf(90, 255, 7) == 2 && sectorOf(180, 255, 7) == 4 &&
            sectorOf(270, 255, 7) == 6 && sectorOf(-45, 255, 7) == 7,
        "0/90/180/270/-45도 → 0/2/4/6/7");
  CHECK(sectorOf(25, 0, 7) == 0 && sectorOf(31, 0, 7) == 1, "히스테리시스: 25도는 0 유지, 31도에서 1로 변경");
}

static void testHaptic() {
  printf("\n[2] 햅틱 패턴 타이밍\n");
  HapticBelt b;
  b.begin(motorWrite);
  b.setBackground(Signal::DIRECTION, 2);
  b.tick(1000);
  bool on1 = motors == (1 << 2);
  b.tick(1300);
  bool off = motors == 0;
  b.tick(2200);
  bool on2 = motors == (1 << 2);
  CHECK(on1 && off && on2, "방향 2: 0ms ON → 300ms OFF → 1200ms 다시 ON");
  b.playOnce(Signal::BLOCKED);
  b.tick(2300);
  bool blk = motors == ((1 << 2) | (1 << 6));
  b.tick(5000);
  b.tick(5001);
  CHECK(blk && b.background() == Signal::DIRECTION && motors == (1 << 2),
        "1회성 BLOCKED(양옆) 재생 후 방향 신호 자동 재개");
  b.allOff();
}

// 버튼만으로 기록 → 복귀 → 도착 (Controller 전체 흐름)
static void testButtonFlow() {
  printf("\n[3] 버튼 흐름: 시작 → 기록 → 복귀 → 도착\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onButtonShort(T);
  CHECK(c.phase() == Phase::RECORDING, "버튼 1회 → 기록 시작");
  c.onButtonLong(T);
  bool out = c.envMode() == EnvMode::OUTDOOR;
  c.onButtonLong(T);
  CHECK(out && c.envMode() == EnvMode::INDOOR && c.phase() == Phase::RECORDING,
        "길게 누르기 → 실내↔실외 전환 (기록은 그대로 유지)");
  walk(c, 0, 14); walk(c, 90, 10);  // ㄱ자
  c.onButtonShort(T);
  CHECK(c.phase() == Phase::RETURNING && c.guideDir() < 8, "버튼 2회 → 복귀 안내 시작 (방향 모터 지정)");
  float h = c.heading();
  int steps = followGuidance(c, h, 300);
  printf("    복귀 %d걸음, 종점 오차(추정좌표) %.2fm\n", steps, hypotf(c.x(), c.y()));
  CHECK(c.phase() == Phase::ARRIVED, "출발점 도착");
}

// 막다른 길 왕복 구간도 지나온 그대로 되짚는지 (최단경로로 건너뛰지 않음)
static void testDeadEndRetrace() {
  printf("\n[4] 막다른 길 왕복도 그대로 되짚기\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("START", T);
  walk(c, 0, 14);    // 북 ~10m
  walk(c, 90, 11);   // 동 ~8m  (교차점)
  walk(c, 0, 8);     // 북쪽 막다른 길 ~5.6m
  walk(c, 180, 8);   // 되돌아옴 → 교차점 노드 재사용
  walk(c, 90, 8);    // 동 ~5.6m
  int recorded = 14 + 11 + 8 + 8 + 8;
  c.onCommand("RETURN", T);
  printf("    되짚을 노드 순서:");
  for (int i = 0; i < c.guide().routeLen(); i++) printf(" %d", c.guide().routeAt(i));
  printf("\n");
  float h = c.heading();
  int steps = followGuidance(c, h, 400);
  printf("    진입 %d걸음, 복귀 %d걸음\n", recorded, steps);
  CHECK(c.phase() == Phase::ARRIVED, "출발점 도착");
  CHECK(steps >= recorded - 8, "막다른 길 왕복 구간까지 되짚음 (지름길 없음)");
}

// 외길에서 BLOCK 명령 → NO_ROUTE 상태 + 전체 진동 신호 (SELFTEST는 진동 출력을 보지 않으므로 여기서 확인)
static void testNoRouteSignal() {
  printf("\n[5] 외길 차단 → NO_ROUTE 진동 출력\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("START", T);
  walk(c, 0, 14); walk(c, 90, 14);
  c.onCommand("RETURN", T);
  float h = c.heading();
  followGuidance(c, h, 400, 5);
  CHECK(c.phase() == Phase::NO_ROUTE, "NO_ROUTE 상태");
  CHECK(c.belt().background() == Signal::NO_ROUTE, "NO_ROUTE 진동(전체 길게 반복) 출력");
}

// 모터 배선 시험 명령: DIR k → k번 모터만 진동, OFF → 전체 끄기 (복귀 안내가 덮어쓰지 않음)
static void testMotorCommands() {
  printf("\n[6] 모터 시험 명령 DIR / OFF\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  bool ok = true;
  for (int k = 0; k < 8; k++) {
    char cmd[8]; snprintf(cmd, sizeof(cmd), "DIR %d", k);
    c.onCommand(cmd, T);
    for (int i = 0; i < 4; i++) { T += 20; c.tick(T); }  // 진동 ON 구간
    for (int m = 0; m < 8; m++) if (((motors >> m) & 1) != (m == k)) ok = false;
  }
  CHECK(ok, "DIR 0~7: 지정한 모터 1개만 진동");
  c.onCommand("OFF", T); T += 20; c.tick(T);
  bool off = true; for (int m = 0; m < 8; m++) if ((motors >> m) & 1) off = false;
  CHECK(off && c.belt().background() == Signal::NONE, "OFF: 전체 모터 꺼짐");
  // 복귀 중에도 DIR 시험이 안내 진동에 덮어써지지 않는지
  c.onCommand("START", T); walk(c, 0, 14); c.onCommand("RETURN", T);
  c.onCommand("DIR 3", T);
  for (int i = 0; i < 10; i++) { T += 100; c.tick(T); }
  CHECK(c.belt().background() == Signal::DIRECTION && c.belt().backgroundDir() == 3, "복귀 중 DIR 3 유지 (안내가 덮어쓰지 않음)");
  c.onCommand("OFF", T);
  for (int i = 0; i < 3; i++) { T += 100; c.tick(T); }
  CHECK(c.guideDir() < 8 && c.belt().background() == Signal::DIRECTION, "OFF 뒤 복귀 안내 진동 다시 나옴");
}

static void testSelfTest() {
  printf("\n[7] SELFTEST 명령 (내장 PDR 백트래킹 시나리오 4개)\n");
  CHECK(runPdrSelfTest(logFn, 0) == 0, "4개 시나리오 모두 PASS");
}

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "-v")) verbose = true;
  srand(42);
  testSector();
  testHaptic();
  testButtonFlow();
  testDeadEndRetrace();
  testNoRouteSignal();
  testMotorCommands();
  testSelfTest();
  printf("\n==== %s (%d failures) ====\n", fails ? "FAILED" : "ALL PASSED", fails);
  return fails ? 1 : 0;
}
