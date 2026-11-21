#include "controller.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "geo.h"
#include "pdr_selftest.h"

namespace chh {

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::IDLE:      return "IDLE";
    case Phase::RECORDING: return "RECORDING";
    case Phase::RETURNING: return "RETURNING";
    case Phase::ARRIVED:   return "ARRIVED";
    case Phase::NO_ROUTE:  return "NO_ROUTE";
  }
  return "?";
}

void Controller::logf(const char* fmt, ...) {
  if (!log_) return;
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log_(buf);
}

void Controller::begin(MotorWriteFn motor, LogFn log) {
  log_ = log;
  belt_.begin(motor);
  logf("COMEBACK HOME ready. 'HELP' 입력 시 명령어 목록");
}

// =============================== 루프 ===============================
void Controller::tick(uint32_t now) {
  if (phase_ == Phase::RETURNING && now - lastGuideMs_ >= cfg::GUIDE_UPDATE_MS) updateGuidance(now);
  belt_.tick(now);
}

// =============================== 센서 ===============================
void Controller::onHeading(float h) { heading_ = wrap360(h); }

void Controller::onStep(float h, uint32_t now) {
  heading_ = wrap360(h);
  if (phase_ == Phase::IDLE) return;
  float px = x_, py = y_;
  x_ += cfg::STEP_LENGTH_M * sinf(heading_ * DEG2RAD);
  y_ += cfg::STEP_LENGTH_M * cosf(heading_ * DEG2RAD);
  // 복귀 중에도 기록 유지: 실제로 걸은 길이므로 그래프에 추가해도 안전 (취소 후 재진입 대비)
  if (phase_ == Phase::RECORDING || phase_ == Phase::RETURNING) {
    rec_.onStep(px, py, x_, y_, heading_);
    if (rec_.full()) logf("WARN,graph full - 노드/간선 한도 초과");
  }
  if (track_) logf("STEP,%lu,%.2f,%.2f,%.1f", (unsigned long)now, x_, y_, heading_);
  if (phase_ == Phase::RETURNING) updateGuidance(now);
}

// =============================== 미션 ===============================
void Controller::startMission() {
  motorTest_ = false;
  belt_.allOff();
  guideDir_ = 255;
  x_ = 0; y_ = 0;
  rec_.begin(&graph_, x_, y_, heading_);  // 0번 노드 = 출발점(탈출구)
  phase_ = Phase::RECORDING;
  belt_.playOnce(Signal::CONFIRM);
  logf("PHASE,RECORDING");
}

void Controller::startReturn(uint32_t now) {
  if (phase_ != Phase::RECORDING) { logf("ERR,RECORDING 상태에서만 복귀 시작 가능"); return; }
  rec_.commitNode(x_, y_, true);  // 현재 위치를 마지막 노드로 확정
  phase_ = Phase::RETURNING;
  ReturnGuide::Status st = guide_.begin(&graph_, rec_);  // 지나온 노드 순서를 역순으로
  logf("PHASE,RETURNING,retrace=%d nodes,dist=%.1fm", guide_.routeLen(), guide_.remainingM(x_, y_));
  if (st == ReturnGuide::Status::NO_ROUTE) { finish(Phase::NO_ROUTE); return; }
  updateGuidance(now);
}

void Controller::cancelReturn() {
  if (phase_ != Phase::RETURNING && phase_ != Phase::NO_ROUTE && phase_ != Phase::ARRIVED) return;
  phase_ = rec_.recording() ? Phase::RECORDING : Phase::IDLE;
  belt_.setBackground(Signal::NONE);
  guideDir_ = 255;
  belt_.playOnce(Signal::CONFIRM);
  logf("PHASE,%s (복귀 취소)", phaseName(phase_));
}

void Controller::finish(Phase p) {
  phase_ = p;
  guideDir_ = 255;
  if (p == Phase::ARRIVED) {
    belt_.setBackground(Signal::NONE);
    belt_.playOnce(Signal::ARRIVED);
    logf("PHASE,ARRIVED,pos=(%.2f,%.2f)", x_, y_);
  } else if (p == Phase::NO_ROUTE) {
    belt_.setBackground(Signal::NO_ROUTE);
    logf("PHASE,NO_ROUTE - 복귀 가능한 경로 없음, 구조 요청 필요");
  }
}

void Controller::setGuideDir(uint8_t d) {
  if (motorTest_) return;  // 모터 시험 중에는 안내 진동을 내보내지 않음
  if (d != guideDir_) logf("GUIDE,dir=%u,wp=%d,remain=%.1fm", d, guide_.nextWaypoint(), guide_.remainingM(x_, y_));
  guideDir_ = d;
  belt_.setBackground(Signal::DIRECTION, d);
}

void Controller::updateGuidance(uint32_t now) {
  lastGuideMs_ = now;
  if (phase_ != Phase::RETURNING) return;
  uint8_t dir = 0;
  ReturnGuide::Status st = guide_.update(x_, y_, heading_, dir);
  if (st == ReturnGuide::Status::ARRIVED)       finish(Phase::ARRIVED);
  else if (st == ReturnGuide::Status::NO_ROUTE) finish(Phase::NO_ROUTE);
  else                                          setGuideDir(dir);
}

void Controller::handleBlocked(const char* why) {
  if (phase_ != Phase::RETURNING) return;
  int a = guide_.lastPassed(), b = guide_.nextWaypoint();
  if (guide_.onBlocked() == ReturnGuide::BlockResult::IGNORED) return;
  // 되짚을 길이 막힘 → 다른 길은 찾지 않고 경로 없음
  belt_.playOnce(Signal::BLOCKED);
  logf("BLOCK,%s,edge=%d-%d,retrace blocked", why, a, b);
  finish(Phase::NO_ROUTE);
}

// =============================== 버튼 ===============================
void Controller::onButtonShort(uint32_t now) {
  switch (phase_) {
    case Phase::IDLE:      startMission(); break;
    case Phase::RECORDING: startReturn(now); break;
    default:               cancelReturn(); break;
  }
}

// 길게 누르기: 실내 ↔ 실외 전환
//  ※ 실외 GPS 안내는 범위에서 제외했으므로, 지금은 모드 값만 바뀌고 안내 방식은 둘 다 PDR 되짚기
void Controller::onButtonLong(uint32_t) {
  env_ = (env_ == EnvMode::INDOOR) ? EnvMode::OUTDOOR : EnvMode::INDOOR;
  belt_.playOnce(Signal::CONFIRM);
  logf("ENV,%s", env_ == EnvMode::INDOOR ? "INDOOR" : "OUTDOOR");
}

// =============================== 명령어 ===============================
static bool eq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) { if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}

void Controller::onCommand(const char* line, uint32_t now) {
  char buf[128];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char* tok[8];
  int n = 0;
  for (char* p = strtok(buf, " ,\t\r\n"); p && n < 8; p = strtok(nullptr, " ,\t\r\n")) tok[n++] = p;
  if (n == 0) return;
  const char* c = tok[0];
  auto num = [&](int i, double def) { return i < n ? strtod(tok[i], nullptr) : def; };

  if (eq(c, "HELP")) {
    logf("START | RETURN | CANCEL | MARK | BLOCK");
    logf("SIM STEP heading [n] | SIM HEAD h");
    logf("STATUS | DUMP | TRACK ON/OFF");
    logf("SELFTEST [1-%d] | SELFTEST NOISE deg   (PDR 백트래킹 알고리즘 자가 테스트)", pdrSelfTestCount());
    logf("DIR 0-7 | OFF   (모터 배선 시험: 지정한 방향 모터 진동 / 전체 끄기)");
  }
  // ---- 미션 ----
  else if (eq(c, "START"))  startMission();
  else if (eq(c, "RETURN")) startReturn(now);
  else if (eq(c, "CANCEL")) cancelReturn();
  else if (eq(c, "MARK")) {
    if (phase_ == Phase::RECORDING && rec_.recording()) logf("MARK,node=%d", rec_.commitNode(x_, y_, true));
  }
  else if (eq(c, "BLOCK")) handleBlocked("manual");
  // ---- 모터 배선 시험 (대시보드 앱 개발자 메뉴에서도 사용) ----
  else if (eq(c, "DIR") && n >= 2) {
    int d = (int)num(1, -1);
    if (d < 0 || d > 7) { logf("ERR,DIR 0~7"); return; }
    motorTest_ = true;
    belt_.setBackground(Signal::DIRECTION, (uint8_t)d);
    logf("MOTOR,DIR,%d", d);
  }
  else if (eq(c, "OFF")) {
    motorTest_ = false;
    belt_.allOff();
    guideDir_ = 255;  // 복귀 중이었다면 다음 갱신 때 안내 진동이 다시 나옴
    logf("MOTOR,OFF");
  }
  // ---- 책상 위 시뮬레이션 (센서 없이 알고리즘 확인용) ----
  else if (eq(c, "SIM") && n >= 2) {
    const char* s = tok[1];
    if (eq(s, "STEP")) { int k = (int)num(3, 1); for (int i = 0; i < k; i++) onStep((float)num(2, heading_), now); }
    else if (eq(s, "HEAD")) onHeading((float)num(2, 0));
  }
  // ---- PDR 백트래킹 자가 테스트 (센서/모터 불필요, 현재 미션에 영향 없음) ----
  else if (eq(c, "SELFTEST")) {
    if (n >= 3 && eq(tok[1], "NOISE")) {
      pdrSelfTestSetNoise((float)num(2, 4));
      logf("SELFTEST,noise=+-%.1f deg", pdrSelfTestNoise());
    } else {
      runPdrSelfTest(log_, n >= 2 ? (int)num(1, 0) : 0);
    }
  }
  // ---- 로그 ----
  else if (eq(c, "STATUS")) status();
  else if (eq(c, "DUMP"))   dump();
  else if (eq(c, "TRACK") && n >= 2) { track_ = eq(tok[1], "ON"); logf("TRACK,%s", track_ ? "ON" : "OFF"); }
  else logf("ERR,unknown command: %s", c);
}

void Controller::status() {
  logf("STATUS,phase=%s,env=%s,pos=(%.2f,%.2f),heading=%.1f,nodes=%d,edges=%d,guide=%d,haptic=%s",
       phaseName(phase_), env_ == EnvMode::INDOOR ? "IN" : "OUT", x_, y_, heading_, graph_.nodeCount(), graph_.edgeCount(), guideDir_,
       signalName(belt_.background()));
}

// 실험 후 경로 시각화용 CSV (실제 경로와 비교)
void Controller::dump() {
  logf("POS,%.2f,%.2f", x_, y_);
  for (int i = 0; i < graph_.nodeCount(); i++) logf("NODE,%d,%.2f,%.2f", i, graph_.node(i).x, graph_.node(i).y);
  for (int i = 0; i < graph_.edgeCount(); i++) {
    const GEdge& e = graph_.edge(i);
    logf("EDGE,%d,%d,%.2f,%d", e.a, e.b, e.len, e.blocked);
  }
  if (phase_ == Phase::RETURNING)
    for (int i = 0; i < guide_.routeLen(); i++) logf("ROUTE,%d,%d%s", i, guide_.routeAt(i), i == guide_.routeIndex() ? ",next" : "");
}

}  // namespace chh
