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
static char lastLog[256];
static int summaryCount = 0;

static void motorWrite(uint8_t i, bool on) { if (on) motors |= 1 << i; else motors &= ~(1 << i); }
static void logFn(const char* s) {
  strncpy(lastLog, s, sizeof(lastLog) - 1);
  if (!strncmp(s, "SUMMARY", 7)) summaryCount++;
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

static void testDetourSkip() {
  printf("\n[3] 실내: 막다른 길 탐색 후 복귀 → 우회분 건너뛰기\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("START", T);
  walk(c, 0, 14);    // 북 ~10m
  walk(c, 90, 11);   // 동 ~8m  (교차점)
  walk(c, 0, 8);     // 북쪽 막다른 길 ~5.6m
  walk(c, 180, 8);   // 되돌아옴 → 교차점 노드와 병합
  walk(c, 90, 8);    // 동 ~5.6m
  int recorded = 14 + 11 + 8 + 8 + 8;
  c.onCommand("RETURN", T);
  float h = c.heading();
  int steps = followGuidance(c, h, 300);
  float err = hypotf(c.x(), c.y());
  printf("    진입 %d걸음, 복귀 %d걸음, 노드 %d개, 종점 오차(추정좌표) %.2fm\n", recorded, steps,
         c.graph().nodeCount(), err);
  CHECK(c.phase() == Phase::ARRIVED, "출발점 도착");
  CHECK(err <= cfg::ARRIVE_RADIUS_M + 0.1f, "도착 반경 이내");
  CHECK(steps < recorded - 10, "막다른 길 왕복 구간을 건너뛰어 더 짧게 복귀");
}

static void testBlockReroute() {
  printf("\n[4] 실내: 복귀 중 경로 차단 → 다른 길로 재탐색\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("START", T);
  walk(c, 0, 14); walk(c, 90, 14); walk(c, 180, 14); walk(c, 270, 14);  // 사각형 한 바퀴
  walk(c, 0, 14); walk(c, 90, 7);                                       // 다시 북 → 동쪽 절반
  c.onCommand("RETURN", T);
  float h = c.heading();
  // 북서쪽 코너 노드를 지나 '출발점으로 가는 마지막 간선'에 들어서면 차단
  int s1 = 0;
  for (; s1 < 100 && c.phase() == Phase::RETURNING; s1++) {
    T += 150; c.tick(T);
    if (c.guide().nextWaypoint() == 0) break;
    uint8_t d = c.guideDir();
    if (d < 8) h = wrap360(h + d * 45.0f);
    T += 450; c.onStep(h, T);
  }
  int before = c.graph().nodeCount();
  c.onCommand("BLOCK", T);
  bool rerouted = c.phase() == Phase::RETURNING;
  int s2 = followGuidance(c, h, 400);
  printf("    차단 전 %d걸음, 차단 후 %d걸음, 노드 %d개\n", s1, s2, before);
  CHECK(rerouted, "대체 경로 발견 (NO_ROUTE 아님)");
  CHECK(c.phase() == Phase::ARRIVED, "돌아가는 길로 출발점 도착");
}

static void testDeadEnd() {
  printf("\n[5] 실내: 유일한 길이 막힘 → NO_ROUTE 신호\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("START", T);
  walk(c, 0, 14); walk(c, 90, 14);
  c.onCommand("RETURN", T);
  float h = c.heading();
  followGuidance(c, h, 400, 5);
  CHECK(c.phase() == Phase::NO_ROUTE, "NO_ROUTE 상태");
  CHECK(c.belt().background() == Signal::NO_ROUTE, "NO_ROUTE 진동(전체 길게 반복) 출력");
}

static void testLinkLoss() {
  printf("\n[6] 원격 조종 중 연결 끊김 → 자율 모드 + 자동 복귀\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onLink(true, T);
  c.onCommand("START", T);
  walk(c, 0, 10);
  c.onCommand("DIR 2", T);
  CHECK(c.ctrlMode() == CtrlMode::REMOTE && c.belt().backgroundDir() == 2, "DIR 2 → 오른쪽 모터 원격 진동");
  walk(c, 90, 10);
  c.onLink(false, T);
  T += 1000; c.tick(T);
  bool stillRemote = c.ctrlMode() == CtrlMode::REMOTE;
  T += 2500; c.tick(T);
  CHECK(stillRemote && c.ctrlMode() == CtrlMode::AUTO, "3초 유예 후 AUTO 전환 (순간 끊김은 무시)");
  CHECK(c.phase() == Phase::RETURNING, "자동으로 복귀 안내 시작");
  float h = c.heading();
  followGuidance(c, h, 200);
  CHECK(c.phase() == Phase::ARRIVED, "출발점 도착");
}

static void testOutdoor() {
  printf("\n[7] 실외: GPS 방위각 안내 + GPS 끊김 시 PDR 대체\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  const double bLat = 37.5582, bLon = 127.0000;
  c.onCommand("ENV OUT", T);
  c.onGps(bLat, bLon, true, T);
  c.onHeading(0);
  c.onCommand("START", T);
  walk(c, 0, 20, 0);
  double lat = bLat + 100.0 / 111320.0;  // 북쪽 100m
  c.onGps(lat, bLon, true, T);
  c.onHeading(0);
  c.onCommand("RETURN", T);
  T += 200; c.tick(T);
  CHECK(c.guideDir() == 4, "북쪽을 보고 있을 때 남쪽 안전지점 → 뒤(4번) 모터");
  c.onHeading(180 - cfg::MAG_DECLINATION_DEG);
  T += 200; c.tick(T);
  CHECK(c.guideDir() == 0, "남쪽으로 돌아서면 정면(0번) 모터 (편각 보정 포함)");
  c.onGps(bLat + 3.0 / 111320.0, bLon, true, T);
  T += 200; c.tick(T);
  CHECK(c.phase() == Phase::ARRIVED, "안전지점 5m 이내 → 도착");

  Controller d; d.begin(motorWrite, logFn); T = 1000;
  d.onCommand("ENV OUT", T);
  d.onGps(bLat, bLon, true, T);
  d.onCommand("START", T);
  walk(d, 0, 14); walk(d, 90, 10);
  d.onGps(0, 0, false, T);  // 터널 진입 등으로 GPS 상실
  T += 4000;
  d.onCommand("RETURN", T);
  float h = d.heading();
  followGuidance(d, h, 200);
  CHECK(d.phase() == Phase::ARRIVED, "GPS 없이 PDR 경로로 복귀");
}

static void testPriorMap() {
  printf("\n[8] 사전 공간정보(평면도 그래프)로 복귀\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  const char* cmds[] = {"MAP CLEAR", "MAP NODE 0 0", "MAP NODE 0 10", "MAP NODE 10 10", "MAP NODE 10 0",
                        "MAP NODE 20 10", "MAP EDGE 0 1", "MAP EDGE 1 2", "MAP EDGE 2 3", "MAP EDGE 3 0",
                        "MAP EDGE 2 4", "MAP START 0", "MAP EXIT 0", "START"};
  for (const char* s : cmds) c.onCommand(s, T);
  walk(c, 0, 14); walk(c, 90, 28);  // 0 → 1 → 2 → 4
  c.onCommand("RETURN", T);
  float h = c.heading();
  int steps = followGuidance(c, h, 300);
  CHECK(c.phase() == Phase::ARRIVED, "지도상 출구 도착");
  printf("    복귀 %d걸음\n", steps);
}

static void testHapticExperiment() {
  printf("\n[9] 햅틱 인지 실험 모드 (혼동 행렬)\n");
  Controller c; c.begin(motorWrite, logFn); T = 1000;
  c.onCommand("TEST DIR8 3", T);
  int answered = 0;
  while (c.testActive() && T < 1000000) {
    T += 50; c.tick(T);
    if (c.belt().background() == Signal::DIRECTION && c.belt().outputMask()) {
      int d = c.belt().backgroundDir();
      int resp = (answered % 7 == 3) ? (d + 1) % 8 : d;  // 일부러 가끔 틀림
      T += 400 + rand() % 300;
      char buf[16]; snprintf(buf, sizeof(buf), "R %d", resp);
      c.onCommand(buf, T);
      answered++;
    }
  }
  CHECK(answered == 24 && summaryCount > 0, "24회 제시·응답 후 요약 출력");
  printf("    %s\n", lastLog);
}

static void testSelfTest() {
  printf("\n[10] SELFTEST 명령 (내장 PDR 백트래킹 시나리오 4개)\n");
  CHECK(runPdrSelfTest(logFn, 0) == 0, "4개 시나리오 모두 PASS");
}

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "-v")) verbose = true;
  srand(42);
  testSector();
  testHaptic();
  testDetourSkip();
  testBlockReroute();
  testDeadEnd();
  testLinkLoss();
  testOutdoor();
  testPriorMap();
  testHapticExperiment();
  testSelfTest();
  printf("\n==== %s (%d failures) ====\n", fails ? "FAILED" : "ALL PASSED", fails);
  return fails ? 1 : 0;
}
