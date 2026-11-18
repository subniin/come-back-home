#include "path_graph.h"
#include <math.h>
#include "geo.h"

namespace chh {

// ============================ PathGraph ============================
int PathGraph::addNode(float x, float y) {
  if (nN_ >= cfg::MAX_NODES) return -1;
  nodes_[nN_] = {x, y};
  return nN_++;
}

int PathGraph::findNear(float x, float y, float r) const {
  int best = -1;
  float bd = r;
  for (int i = 0; i < nN_; i++) {
    float d = dist2d(x, y, nodes_[i].x, nodes_[i].y);
    if (d <= bd) { bd = d; best = i; }
  }
  return best;
}

int PathGraph::nearest(float x, float y) const {
  return findNear(x, y, 1e9f);
}

int PathGraph::findEdge(int a, int b) const {
  for (int i = 0; i < nE_; i++) {
    const GEdge& e = edges_[i];
    if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) return i;
  }
  return -1;
}

bool PathGraph::addEdge(int a, int b) {
  if (a == b || a < 0 || b < 0 || a >= nN_ || b >= nN_) return false;
  if (findEdge(a, b) >= 0) return true;
  if (nE_ >= cfg::MAX_EDGES) return false;
  float len = dist2d(nodes_[a].x, nodes_[a].y, nodes_[b].x, nodes_[b].y);
  edges_[nE_++] = {(uint8_t)a, (uint8_t)b, len, false};
  return true;
}

bool PathGraph::blockEdge(int a, int b) {
  int e = findEdge(a, b);
  if (e < 0) return false;
  edges_[e].blocked = true;
  return true;
}

void PathGraph::moveNode(int i, float x, float y) {
  if (i < 0 || i >= nN_) return;
  nodes_[i] = {x, y};
  for (int k = 0; k < nE_; k++) {
    GEdge& e = edges_[k];
    if (e.a == i || e.b == i)
      e.len = dist2d(nodes_[e.a].x, nodes_[e.a].y, nodes_[e.b].x, nodes_[e.b].y);
  }
}

void PathGraph::unblockAll() {
  for (int i = 0; i < nE_; i++) edges_[i].blocked = false;
}

// ============================ PathRecorder ============================
void PathRecorder::begin(PathGraph* g, float x, float y, float headingDeg) {
  g_ = g;
  g_->clear();
  cur_ = g_->addNode(x, y);  // 0번 = 출발점(탈출구)
  segHead_ = headingDeg;
  pending_ = 0;
  enabled_ = true;
  full_ = false;
  curIsTurn_ = true;
  trailLen_ = 0;
  pushTrail(cur_);
}

void PathRecorder::pushTrail(int n) {
  if (trailLen_ >= cfg::MAX_TRAIL) { full_ = true; return; }
  trail_[trailLen_++] = (uint8_t)n;
}

void PathRecorder::onStep(float xPrev, float yPrev, float x, float y, float heading) {
  if (!g_ || !enabled_ || cur_ < 0) return;

  float d = wrap180(heading - segHead_);
  if (fabsf(d) > cfg::TURN_DEG) {
    // 꺾이기 시작한 지점을 기억해 두었다가, N걸음 연속이면 그 지점을 노드로 확정
    if (pending_ == 0) { turnX_ = xPrev; turnY_ = yPrev; }
    if (++pending_ >= cfg::TURN_CONFIRM_STEPS) {
      commitNode(turnX_, turnY_, true);
      segHead_ = heading;
      pending_ = 0;
    }
  } else {
    pending_ = 0;
    segHead_ = wrap360(segHead_ + cfg::SEG_HEADING_ALPHA * d);
  }

  const GNode& c = g_->node(cur_);
  if (pending_ == 0 && dist2d(c.x, c.y, x, y) > cfg::MAX_SEGMENT_M) commitNode(x, y);
}

int PathRecorder::commitNode(float x, float y, bool atTurn) {
  if (!g_ || cur_ < 0) return cur_;
  int near = g_->findNear(x, y, cfg::MERGE_RADIUS_M);
  if (near == cur_) {  // 직전 노드와 너무 가까움
    // 직선 구간 자동 노드 바로 뒤에서 꺾였으면, 그 노드를 실제 모퉁이 위치로 옮김
    // (안 옮기면 모퉁이가 최대 MERGE_RADIUS만큼 앞당겨져 저장됨)
    if (atTurn && !curIsTurn_) { g_->moveNode(cur_, x, y); curIsTurn_ = true; }
    return cur_;
  }
  if (near >= 0) {  // 예전에 지나간 지점 → 기존 노드 재사용 (루프/되돌아옴)
    g_->addEdge(cur_, near);
    cur_ = near;
    curIsTurn_ = true;
    pushTrail(cur_);  // 재방문도 지나온 순서에 그대로 기록
    return cur_;
  }
  int n = g_->addNode(x, y);
  if (n < 0) { full_ = true; return cur_; }
  if (!g_->addEdge(cur_, n)) full_ = true;
  cur_ = n;
  curIsTurn_ = atTurn;
  pushTrail(cur_);
  return n;
}

}  // namespace chh
