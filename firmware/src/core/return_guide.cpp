#include "return_guide.h"
#include "geo.h"

namespace chh {

bool ReturnGuide::replan(int from) {
  len_ = g_ ? g_->shortestPath(from, goal_, route_, cfg::MAX_NODES) : 0;
  idx_ = 0;  // route_[0](출발 노드)부터 목표로 삼음. 이미 근처면 update에서 바로 통과 처리
  return len_ > 0;
}

ReturnGuide::Status ReturnGuide::begin(PathGraph* g, int fromNode, int goalNode) {
  g_ = g;
  goal_ = goalNode;
  lastPassed_ = fromNode;
  sector_ = 255;
  status_ = replan(fromNode) ? Status::GUIDING : Status::NO_ROUTE;
  return status_;
}

ReturnGuide::Status ReturnGuide::update(float x, float y, float heading, uint8_t& dirOut) {
  if (status_ != Status::GUIDING) return status_;

  // 앞쪽 웨이포인트 중 이미 반경 안에 들어온 게 있으면 거기까지 건너뜀 (지름길로 와도 OK)
  for (int j = len_ - 1; j >= idx_; j--) {
    const GNode& n = g_->node(route_[j]);
    if (dist2d(x, y, n.x, n.y) <= cfg::ARRIVE_RADIUS_M) {
      lastPassed_ = route_[j];
      idx_ = j + 1;
      break;
    }
  }
  if (idx_ >= len_) { status_ = Status::ARRIVED; return status_; }

  const GNode& t = g_->node(route_[idx_]);
  float rel = planarBearing(t.x - x, t.y - y) - heading;
  sector_ = sectorOf(rel, sector_, cfg::SECTOR_HYST_DEG);
  dirOut = sector_;
  return status_;
}

// 점 (px,py)와 선분 (ax,ay)-(bx,by) 사이 거리
static float pointSegDist(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
  float t = L2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / L2 : 0;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return dist2d(px, py, ax + t * dx, ay + t * dy);
}

ReturnGuide::BlockResult ReturnGuide::onBlocked() {
  if (status_ != Status::GUIDING) return BlockResult::IGNORED;
  int a = lastPassed_;
  int b = (idx_ < len_) ? route_[idx_] : -1;
  if (b < 0 || a == b) return BlockResult::IGNORED;  // 아직 첫 노드 도달 전: 어느 간선인지 모름

  g_->blockEdge(a, b);
  // PDR 드리프트로 같은 복도에 노드가 겹쳐 생기면 '가짜 평행 간선'이 생김.
  // 막힌 간선과 겹치는(양 끝점이 모두 병합 반경 안) 간선도 같은 복도로 보고 함께 막음
  const GNode &A = g_->node(a), &B = g_->node(b);
  for (int k = 0; k < g_->edgeCount(); k++) {
    const GEdge& e = g_->edge(k);
    if (e.blocked) continue;
    const GNode &U = g_->node(e.a), &V = g_->node(e.b);
    if (pointSegDist(U.x, U.y, A.x, A.y, B.x, B.y) <= cfg::MERGE_RADIUS_M &&
        pointSegDist(V.x, V.y, A.x, A.y, B.x, B.y) <= cfg::MERGE_RADIUS_M)
      g_->blockEdge(e.a, e.b);
  }
  sector_ = 255;
  if (!replan(a)) { status_ = Status::NO_ROUTE; return BlockResult::NO_ROUTE; }
  return BlockResult::REROUTED;
}

float ReturnGuide::remainingM(float x, float y) const {
  if (idx_ >= len_) return 0;
  const GNode& n0 = g_->node(route_[idx_]);
  float d = dist2d(x, y, n0.x, n0.y);
  for (int i = idx_; i + 1 < len_; i++) {
    const GNode& p = g_->node(route_[i]);
    const GNode& q = g_->node(route_[i + 1]);
    d += dist2d(p.x, p.y, q.x, q.y);
  }
  return d;
}

}  // namespace chh
