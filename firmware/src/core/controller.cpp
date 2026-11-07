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

static LogFn g_log = nullptr;
static void testLog(const char* s) { if (g_log) g_log(s); }

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
  g_log = log;
  belt_.begin(motor);
  logf("COMEBACK HOME ready. 'HELP' 입력 시 명령어 목록");
}

// =============================== 루프 ===============================
void Controller::tick(uint32_t now) {
  // 연결 끊김 확정 → 자율 모드
  if (linkEverUp_ && !linkUp_ && !linkLossHandled_ && now - linkLostMs_ >= cfg::LINK_LOST_MS) {
    linkLossHandled_ = true;
    enterAuto("LINK_LOST", now);
    if (cfg::AUTO_RETURN_ON_LINK_LOSS && phase_ == Phase::RECORDING) startReturn(now);
  }

  if (test_.active()) {
    test_.tick(now, belt_, testLog);
  } else if (phase_ == Phase::RETURNING && now - lastGuideMs_ >= cfg::GUIDE_UPDATE_MS) {
    updateGuidance(now);
  }
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

void Controller::onGps(double lat, double lon, bool fix, uint32_t now) {
  gpsFix_ = fix;
  if (!fix) return;
  lat_ = lat; lon_ = lon; gpsMs_ = now;
}

void Controller::onObstacle(float d, uint32_t now) {
  bool near = d > 0 && d < cfg::BLOCK_DIST_M;
  if (!near) { obsNear_ = false; return; }
  if (!obsNear_) { obsNear_ = true; obsSince_ = now; return; }
  if (phase_ == Phase::RETURNING && !usingGps_ && ctrl_ == CtrlMode::AUTO &&
      now - obsSince_ >= cfg::BLOCK_CONFIRM_MS && now - lastBlockMs_ >= cfg::BLOCK_COOLDOWN_MS) {
    lastBlockMs_ = now;
    obsNear_ = false;
    handleBlocked("obstacle");
  }
}

// =============================== 링크 ===============================
void Controller::onLink(bool up, uint32_t now) {
  if (up == linkUp_) return;
  linkUp_ = up;
  if (up) {
    linkEverUp_ = true;
    linkLossHandled_ = true;
    logf("LINK,UP");
  } else {
    linkLostMs_ = now;
    linkLossHandled_ = false;
    logf("LINK,DOWN (%.1fs 후 자율모드)", cfg::LINK_LOST_MS / 1000.0f);
  }
}

void Controller::enterAuto(const char* why, uint32_t) {
  bool wasRemote = (ctrl_ == CtrlMode::REMOTE);
  ctrl_ = CtrlMode::AUTO;
  if (wasRemote) belt_.setBackground(Signal::NONE);
  belt_.playOnce(Signal::MODE_AUTO);
  guideDir_ = 255;
  logf("MODE,AUTO,%s", why);
}

// =============================== 미션 ===============================
void Controller::startMission(uint32_t now) {
  (void)now;
  belt_.allOff();
  guideDir_ = 255;
  if (priorMap_) {
    if (graph_.nodeCount() == 0) { logf("ERR,사전 지도가 비어 있음"); return; }
    graph_.unblockAll();
    x_ = graph_.node(mapStart_).x;
    y_ = graph_.node(mapStart_).y;
    rec_.attachPrior(&graph_, mapStart_);
  } else {
    x_ = 0; y_ = 0;
    rec_.begin(&graph_, x_, y_, heading_);
  }
  if (gpsFix_ && !baseSet_) {
    baseLat_ = lat_; baseLon_ = lon_; baseSet_ = true;
    logf("BASE,%.6f,%.6f (현재 GPS로 설정)", baseLat_, baseLon_);
  }
  phase_ = Phase::RECORDING;
  belt_.playOnce(Signal::CONFIRM);
  logf("PHASE,RECORDING,env=%s,map=%s", env_ == EnvMode::INDOOR ? "IN" : "OUT",
       priorMap_ ? "PRIOR" : "PDR");
}

void Controller::startReturn(uint32_t now) {
  if (phase_ != Phase::RECORDING) { logf("ERR,RECORDING 상태에서만 복귀 시작 가능"); return; }
  int from, goal;
  if (priorMap_) {
    from = graph_.nearest(x_, y_);
    goal = mapExit_;
  } else {
    from = rec_.commitNode(x_, y_, true);  // 현재 위치를 노드로 확정 (고정점)
    goal = 0;
  }
  phase_ = Phase::RETURNING;
  outSector_ = 255;
  ReturnGuide::Status st = guide_.begin(&graph_, from, goal);
  logf("PHASE,RETURNING,from=%d,goal=%d,route=%d nodes,dist=%.1fm", from, goal,
       guide_.routeLen(), guide_.remainingM(x_, y_));
  if (st == ReturnGuide::Status::NO_ROUTE && !(env_ == EnvMode::OUTDOOR && baseSet_)) {
    finish(Phase::NO_ROUTE);
    return;
  }
  updateGuidance(now);
}

void Controller::cancelReturn() {
  if (phase_ != Phase::RETURNING && phase_ != Phase::NO_ROUTE && phase_ != Phase::ARRIVED) return;
  phase_ = priorMap_ || rec_.recording() ? Phase::RECORDING : Phase::IDLE;
  if (ctrl_ == CtrlMode::AUTO) belt_.setBackground(Signal::NONE);
  guideDir_ = 255;
  belt_.playOnce(Signal::CONFIRM);
  logf("PHASE,%s (복귀 취소)", phaseName(phase_));
}

void Controller::finish(Phase p) {
  phase_ = p;
  guideDir_ = 255;
  if (ctrl_ == CtrlMode::REMOTE) { logf("PHASE,%s (원격 조종 중이라 진동 생략)", phaseName(p)); return; }
  if (p == Phase::ARRIVED) {
    belt_.setBackground(Signal::NONE);
    belt_.playOnce(Signal::ARRIVED);
    logf("PHASE,ARRIVED,pos=(%.2f,%.2f)", x_, y_);
  } else if (p == Phase::NO_ROUTE) {
    belt_.setBackground(Signal::NO_ROUTE);
    logf("PHASE,NO_ROUTE - 복귀 가능한 경로 없음, 구조 요청/원격 안내 필요");
  }
}

void Controller::setGuideDir(uint8_t d) {
  if (d != guideDir_) logf("GUIDE,dir=%u,wp=%d,remain=%.1fm%s", d, guide_.nextWaypoint(),
                           guide_.remainingM(x_, y_), usingGps_ ? ",GPS" : "");
  guideDir_ = d;
  if (ctrl_ == CtrlMode::AUTO) belt_.setBackground(Signal::DIRECTION, d);
}

void Controller::updateGuidance(uint32_t now) {
  lastGuideMs_ = now;
  if (phase_ != Phase::RETURNING) return;

  // --- 실외: GPS가 살아 있으면 안전지점 방위각으로 직접 안내 ---
  bool gpsFresh = env_ == EnvMode::OUTDOOR && baseSet_ && gpsFix_ && now - gpsMs_ < cfg::GPS_STALE_MS;
  if (gpsFresh != usingGps_) {
    usingGps_ = gpsFresh;
    logf("NAV,%s", gpsFresh ? "GPS" : "PDR_GRAPH");
  }
  if (gpsFresh) {
    double d = haversineM(lat_, lon_, baseLat_, baseLon_);
    if (d <= cfg::OUTDOOR_ARRIVE_M) { finish(Phase::ARRIVED); return; }
    float brg = gpsBearingDeg(lat_, lon_, baseLat_, baseLon_);
    float trueHeading = wrap360(heading_ + cfg::MAG_DECLINATION_DEG);
    outSector_ = sectorOf(brg - trueHeading, outSector_, cfg::SECTOR_HYST_DEG);
    setGuideDir(outSector_);
    return;
  }

  // --- 실내 (또는 GPS 끊긴 실외): 경로 그래프 ---
  uint8_t dir = 0;
  ReturnGuide::Status st = guide_.update(x_, y_, heading_, dir);
  if (st == ReturnGuide::Status::ARRIVED)       finish(Phase::ARRIVED);
  else if (st == ReturnGuide::Status::NO_ROUTE) finish(Phase::NO_ROUTE);
  else                                          setGuideDir(dir);
}

void Controller::handleBlocked(const char* why) {
  if (phase_ != Phase::RETURNING) return;
  int a = guide_.lastPassed(), b = guide_.nextWaypoint();
  ReturnGuide::BlockResult r = guide_.onBlocked();
  if (r == ReturnGuide::BlockResult::IGNORED) {
    logf("BLOCK,ignored,%s (차단 간선 판단 불가)", why);
    return;
  }
  belt_.playOnce(Signal::BLOCKED);
  if (r == ReturnGuide::BlockResult::REROUTED) {
    logf("BLOCK,%s,edge=%d-%d,rerouted,route=%d nodes,dist=%.1fm", why, a, b, guide_.routeLen(),
         guide_.remainingM(x_, y_));
    guideDir_ = 255;
  } else {
    logf("BLOCK,%s,edge=%d-%d,no alternative", why, a, b);
    finish(Phase::NO_ROUTE);
  }
}

// =============================== 버튼 ===============================
void Controller::onButtonShort(uint32_t now) {
  if (test_.active()) return;
  switch (phase_) {
    case Phase::IDLE:      startMission(now); break;
    case Phase::RECORDING: startReturn(now); break;
    default:               cancelReturn(); break;
  }
}

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
    logf("START | RETURN | CANCEL | ENV IN/OUT | BASE lat lon | MARK | BLOCK");
    logf("DIR 0-7 | SIG STOP/ARRIVED/BLOCKED/NOROUTE/CONFIRM/MODE | OFF | AUTO");
    logf("MAP CLEAR | MAP NODE x y | MAP EDGE a b | MAP START id | MAP EXIT id | MAP OFF");
    logf("TEST DIR8/DIR4/SIGNAL reps | R code | TEST STOP");
    logf("SIM STEP heading [n] | SIM HEAD h | SIM GPS lat lon | SIM GPSOFF | SIM OBS m | SIM LINK 0/1");
    logf("STATUS | DUMP | TRACK ON/OFF");
    logf("SELFTEST [1-%d] | SELFTEST NOISE deg   (PDR 백트래킹 알고리즘 자가 테스트)", pdrSelfTestCount());
  }
  // ---- 미션 ----
  else if (eq(c, "START"))  startMission(now);
  else if (eq(c, "RETURN")) startReturn(now);
  else if (eq(c, "CANCEL")) cancelReturn();
  else if (eq(c, "MARK")) {
    if (phase_ == Phase::RECORDING && rec_.recording()) logf("MARK,node=%d", rec_.commitNode(x_, y_, true));
  }
  else if (eq(c, "BLOCK")) handleBlocked("manual");
  else if (eq(c, "ENV") && n >= 2) {
    env_ = (toupper((unsigned char)tok[1][0]) == 'O') ? EnvMode::OUTDOOR : EnvMode::INDOOR;
    logf("ENV,%s", env_ == EnvMode::INDOOR ? "INDOOR" : "OUTDOOR");
  }
  else if (eq(c, "BASE") && n >= 3) {
    baseLat_ = num(1, 0); baseLon_ = num(2, 0); baseSet_ = true;
    logf("BASE,%.6f,%.6f", baseLat_, baseLon_);
  }
  // ---- 원격 수동 조종 ----
  else if (eq(c, "DIR") && n >= 2) {
    int d = (int)num(1, 0);
    if (d < 0 || d > 7) { logf("ERR,DIR 0~7"); return; }
    ctrl_ = CtrlMode::REMOTE;
    belt_.setBackground(Signal::DIRECTION, (uint8_t)d);
    logf("REMOTE,DIR,%d", d);
  }
  else if (eq(c, "SIG") && n >= 2) {
    const char* s = tok[1];
    if (eq(s, "STOP"))         { ctrl_ = CtrlMode::REMOTE; belt_.setBackground(Signal::STOP); }
    else if (eq(s, "NOROUTE")) { ctrl_ = CtrlMode::REMOTE; belt_.setBackground(Signal::NO_ROUTE); }
    else if (eq(s, "ARRIVED")) belt_.playOnce(Signal::ARRIVED);
    else if (eq(s, "BLOCKED")) belt_.playOnce(Signal::BLOCKED);
    else if (eq(s, "CONFIRM")) belt_.playOnce(Signal::CONFIRM);
    else if (eq(s, "MODE"))    belt_.playOnce(Signal::MODE_AUTO);
    else { logf("ERR,unknown signal"); return; }
    logf("REMOTE,SIG,%s", s);
  }
  else if (eq(c, "OFF"))  { belt_.allOff(); logf("HAPTIC,OFF"); }
  else if (eq(c, "AUTO")) enterAuto("command", now);
  // ---- 사전 공간정보 ----
  else if (eq(c, "MAP") && n >= 2) {
    const char* s = tok[1];
    if (eq(s, "CLEAR")) { graph_.clear(); priorMap_ = true; mapStart_ = mapExit_ = 0; phase_ = Phase::IDLE; logf("MAP,CLEARED (사전 지도 모드)"); }
    else if (eq(s, "OFF")) { priorMap_ = false; graph_.clear(); phase_ = Phase::IDLE; logf("MAP,OFF (PDR 기록 모드)"); }
    else if (eq(s, "NODE") && n >= 4) logf("MAP,NODE,%d", graph_.addNode((float)num(2, 0), (float)num(3, 0)));
    else if (eq(s, "EDGE") && n >= 4) logf("MAP,EDGE,%s", graph_.addEdge((int)num(2, 0), (int)num(3, 0)) ? "ok" : "fail");
    else if (eq(s, "START") && n >= 3) { mapStart_ = (int)num(2, 0); logf("MAP,START,%d", mapStart_); }
    else if (eq(s, "EXIT") && n >= 3)  { mapExit_ = (int)num(2, 0);  logf("MAP,EXIT,%d", mapExit_); }
  }
  // ---- 햅틱 인지 실험 ----
  else if (eq(c, "TEST") && n >= 2) {
    if (eq(tok[1], "STOP")) { test_.stop(testLog); belt_.allOff(); return; }
    HapticTest::Kind k = eq(tok[1], "DIR4") ? HapticTest::Kind::DIR4
                       : eq(tok[1], "SIGNAL") ? HapticTest::Kind::SIGNAL : HapticTest::Kind::DIR8;
    belt_.allOff();
    test_.start(k, (int)num(2, 10), now * 2654435761u + 1, now, testLog);
  }
  else if (eq(c, "R") && n >= 2) test_.respond((int)num(1, -1), now, belt_, testLog);
  // ---- 책상 위 시뮬레이션 (센서 없이 알고리즘 확인용) ----
  else if (eq(c, "SIM") && n >= 2) {
    const char* s = tok[1];
    if (eq(s, "STEP")) { int k = (int)num(3, 1); for (int i = 0; i < k; i++) onStep((float)num(2, heading_), now); }
    else if (eq(s, "HEAD"))   onHeading((float)num(2, 0));
    else if (eq(s, "GPS"))    onGps(num(2, 0), num(3, 0), true, now);
    else if (eq(s, "GPSOFF")) onGps(0, 0, false, now);
    else if (eq(s, "OBS"))    onObstacle((float)num(2, 9), now);
    else if (eq(s, "LINK"))   onLink(num(2, 1) != 0, now);
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
  logf("STATUS,phase=%s,ctrl=%s,env=%s,map=%s,link=%d", phaseName(phase_),
       ctrl_ == CtrlMode::AUTO ? "AUTO" : "REMOTE", env_ == EnvMode::INDOOR ? "IN" : "OUT",
       priorMap_ ? "PRIOR" : "PDR", linkUp_);
  logf("STATUS,pos=(%.2f,%.2f),heading=%.1f,nodes=%d,edges=%d,guide=%d,haptic=%s", x_, y_, heading_,
       graph_.nodeCount(), graph_.edgeCount(), guideDir_, signalName(belt_.background()));
  if (baseSet_) logf("STATUS,base=%.6f,%.6f,gps=%d", baseLat_, baseLon_, gpsFix_);
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
