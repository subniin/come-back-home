#include "pdr_selftest.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "config.h"
#include "geo.h"
#include "path_graph.h"
#include "return_guide.h"

namespace chh {

// ============================================================================
//  ★ 여기서 고치는 변수들 ★
// ============================================================================
// 진입 경로 = { 헤딩(도), 걸음 수 } 의 목록. 헤딩: 0=앞(북), 90=오른쪽, 180=뒤, 270=왼쪽
// 한 걸음 = cfg::STEP_LENGTH_M (기본 0.7m)
struct Leg { float heading; int steps; };

// 헤딩 노이즈(±도): 센서 흔들림 흉내. 0이면 완벽한 센서. SELFTEST NOISE 10 처럼 바꿀 수 있음
static float NOISE_DEG = 4.0f;
// 난수 시드: 같은 값이면 매번 같은 결과 (재현 가능)
static const uint32_t SEED = 12345;

// 차단 시점: -1 = 차단 없음, 0 이상 = 복귀 n걸음째에 차단,
//            BLOCK_LAST_EDGE = 출발점으로 가는 마지막 간선에 들어서는 순간 차단
static const int NO_BLOCK = -1;
static const int BLOCK_LAST_EDGE = -2;

// 시나리오 1: ㄱ자 경로 (앞 9.8m → 오른쪽 7m)
static const Leg PATH_L[] = {{0, 14}, {90, 10}};
// 시나리오 2: 막다른 길 탐색 후 계속 진행 (복귀 때 막다른 길 왕복도 그대로 되짚어야 함)
static const Leg PATH_DEAD_END[] = {{0, 14}, {90, 11}, {0, 8}, {180, 8}, {90, 8}};
// 시나리오 3: 사각형 순환로 한 바퀴 + 다시 진입 (되짚던 길이 막히면 다른 길로 가지 않고 NO_ROUTE)
static const Leg PATH_LOOP[] = {{0, 14}, {90, 14}, {180, 14}, {270, 14}, {0, 14}, {90, 7}};
// 시나리오 4: 외길 (차단되면 갈 길이 없어야 함)
static const Leg PATH_ONE_WAY[] = {{0, 14}, {90, 14}};

struct Scenario {
  const char* name;
  const Leg*  legs;
  int         nLegs;
  int         blockAt;     // 위 차단 시점 규칙
  bool        expectArrive;  // true = 도착해야 정상, false = NO_ROUTE여야 정상
};

#define LEGS(a) a, (int)(sizeof(a) / sizeof(a[0]))
static const Scenario SCENARIOS[] = {
    {"L-shape",            LEGS(PATH_L),        NO_BLOCK,        true},
    {"dead-end retrace",   LEGS(PATH_DEAD_END), NO_BLOCK,        true},
    {"loop + block",       LEGS(PATH_LOOP),     BLOCK_LAST_EDGE, false},
    {"one-way + block",    LEGS(PATH_ONE_WAY),  5,               false},
};
static const int N_SCENARIOS = (int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0]));
// ============================================================================

int pdrSelfTestCount() { return N_SCENARIOS; }
void pdrSelfTestSetNoise(float d) { NOISE_DEG = d < 0 ? 0 : d; }
float pdrSelfTestNoise() { return NOISE_DEG; }

// 그래프는 크기가 커서(약 3KB) 스택 대신 정적 메모리에
static PathGraph    s_graph;
static PathRecorder s_rec;
static ReturnGuide  s_guide;
static uint32_t     s_rng = SEED;

static float noise() {
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  return NOISE_DEG * ((s_rng % 20001) / 10000.0f - 1.0f);
}

static void logf(LogFn log, const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log(buf);
}

static bool runOne(LogFn log, int idx) {
  const Scenario& sc = SCENARIOS[idx];
  logf(log, "=== SELFTEST %d: %s (noise=+-%.0f deg) ===", idx + 1, sc.name, NOISE_DEG);

  // ---------- 1) 진입: 걸음 데이터를 넣으며 경로 기록 ----------
  float x = 0, y = 0, h = sc.legs[0].heading;
  int inSteps = 0;
  s_rec.begin(&s_graph, x, y, h);
  for (int l = 0; l < sc.nLegs; l++) {
    for (int k = 0; k < sc.legs[l].steps; k++) {
      float px = x, py = y;
      h = wrap360(sc.legs[l].heading + noise());
      x += cfg::STEP_LENGTH_M * sinf(h * DEG2RAD);
      y += cfg::STEP_LENGTH_M * cosf(h * DEG2RAD);
      s_rec.onStep(px, py, x, y, h);
      inSteps++;
    }
  }
  logf(log, "ENTER,steps=%d,pos=(%.2f,%.2f),nodes=%d,edges=%d", inSteps, x, y,
       s_graph.nodeCount(), s_graph.edgeCount());
  for (int i = 0; i < s_graph.nodeCount(); i++)
    logf(log, "  NODE %d (%.2f, %.2f)%s", i, s_graph.node(i).x, s_graph.node(i).y, i == 0 ? "  <- start" : "");

  // ---------- 2) 복귀 시작: 지나온 노드 순서를 역순으로 ----------
  s_rec.commitNode(x, y, true);
  ReturnGuide::Status st = s_guide.begin(&s_graph, s_rec);
  {
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "RETURN,retrace=");
    for (int i = 0; i < s_guide.routeLen() && n < (int)sizeof(buf) - 8; i++)
      n += snprintf(buf + n, sizeof(buf) - n, i ? "->%d" : "%d", s_guide.routeAt(i));
    snprintf(buf + n, sizeof(buf) - n, ",dist=%.1fm", s_guide.remainingM(x, y));
    log(buf);
  }

  // ---------- 3) 진동이 가리키는 방향대로 걸어간다고 가정 ----------
  int retSteps = 0;
  bool blocked = false;
  uint8_t lastDir = 255;
  while (st == ReturnGuide::Status::GUIDING && retSteps < 600) {
    uint8_t dir = 0;
    st = s_guide.update(x, y, h, dir);
    if (st != ReturnGuide::Status::GUIDING) break;

    bool doBlock = !blocked && ((sc.blockAt >= 0 && retSteps == sc.blockAt) ||
                                (sc.blockAt == BLOCK_LAST_EDGE && s_guide.nextWaypoint() == 0 &&
                                 s_guide.routeIndex() > 0));
    if (doBlock) {
      blocked = true;
      int a = s_guide.lastPassed(), b = s_guide.nextWaypoint();
      ReturnGuide::BlockResult r = s_guide.onBlocked();
      const char* rs = r == ReturnGuide::BlockResult::NO_ROUTE ? "NO_ROUTE" : "IGNORED";
      logf(log, "BLOCK at return step %d, edge %d-%d -> %s, new dist=%.1fm", retSteps, a, b, rs,
           s_guide.remainingM(x, y));
      lastDir = 255;
      if (r == ReturnGuide::BlockResult::NO_ROUTE) { st = ReturnGuide::Status::NO_ROUTE; break; }
      continue;
    }

    if (dir != lastDir) {
      logf(log, "  step %3d: motor %u -> waypoint %d, remain %.1fm", retSteps, dir, s_guide.nextWaypoint(),
           s_guide.remainingM(x, y));
      lastDir = dir;
    }
    float px = x, py = y;
    h = wrap360(h + dir * 45.0f + noise());  // 울린 모터 쪽으로 몸을 돌리고
    x += cfg::STEP_LENGTH_M * sinf(h * DEG2RAD);  // 한 걸음
    y += cfg::STEP_LENGTH_M * cosf(h * DEG2RAD);
    s_rec.onStep(px, py, x, y, h);
    retSteps++;
  }

  // ---------- 4) 판정 ----------
  float err = dist2d(x, y, 0, 0);
  bool arrived = (st == ReturnGuide::Status::ARRIVED);
  // 되짚기 확인: 지나온 노드를 하나도 건너뛰지 않고 전부 지났는지 (도착한 경우)
  bool retraced = !arrived || s_guide.routeIndex() == s_guide.routeLen();
  bool pass = arrived == sc.expectArrive && retraced && (!arrived || err <= cfg::ARRIVE_RADIUS_M + 0.1f);
  logf(log, "RESULT,%s,return_steps=%d (enter %d),end_error=%.2fm,expected=%s -> %s",
       arrived ? "ARRIVED" : (st == ReturnGuide::Status::NO_ROUTE ? "NO_ROUTE" : "TIMEOUT"), retSteps, inSteps,
       err, sc.expectArrive ? "ARRIVED" : "NO_ROUTE", pass ? "PASS" : "FAIL");
  return pass;
}

int runPdrSelfTest(LogFn log, int which) {
  s_rng = SEED;
  int fails = 0, ran = 0;
  for (int i = 0; i < N_SCENARIOS; i++) {
    if (which != 0 && which != i + 1) continue;
    if (!runOne(log, i)) fails++;
    ran++;
  }
  if (ran == 0) { logf(log, "ERR,SELFTEST 1~%d", N_SCENARIOS); return 1; }
  logf(log, "=== SELFTEST DONE: %d/%d PASS ===", ran - fails, ran);
  return fails;
}

}  // namespace chh
