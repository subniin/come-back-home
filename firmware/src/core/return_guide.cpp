#include "return_guide.h"
#include "geo.h"

namespace chh {

ReturnGuide::Status ReturnGuide::begin(PathGraph* g, const PathRecorder& rec) {
  g_ = g;
  sector_ = 255;
  idx_ = 0;
  len_ = 0;
  // trail: 출발(0) → ... → 현재.  route: 현재 → ... → 출발(0)
  // 단, 되짚는 도중 출발 노드를 만나면 거기서 끝 (순환로를 돌아 출발점을 지나쳤던 경우, 출발점을 지나쳐 계속 가지 않도록)
  const int start = rec.trailLen() > 0 ? rec.trailAt(0) : -1;
  for (int i = rec.trailLen() - 1; i >= 0 && len_ < cfg::MAX_TRAIL; i--) {
    route_[len_++] = (uint8_t)rec.trailAt(i);
    if (rec.trailAt(i) == start) break;
  }
  lastPassed_ = len_ > 0 ? route_[0] : -1;
  status_ = len_ > 0 ? Status::GUIDING : Status::NO_ROUTE;
  return status_;
}

ReturnGuide::Status ReturnGuide::update(float x, float y, float heading, uint8_t& dirOut) {
  if (status_ != Status::GUIDING) return status_;

  // 지나온 순서대로만 진행: 지금 목표 지점에 도착해야 다음 지점으로 넘어감
  // (같은 노드가 경로에 두 번 나와도 앞쪽으로 건너뛰지 않음 → 막다른 길 왕복도 그대로 되짚음)
  while (idx_ < len_) {
    const GNode& n = g_->node(route_[idx_]);
    if (dist2d(x, y, n.x, n.y) > cfg::ARRIVE_RADIUS_M) break;
    lastPassed_ = route_[idx_];
    idx_++;
  }
  if (idx_ >= len_) { status_ = Status::ARRIVED; return status_; }

  const GNode& t = g_->node(route_[idx_]);
  float rel = planarBearing(t.x - x, t.y - y) - heading;
  sector_ = sectorOf(rel, sector_, cfg::SECTOR_HYST_DEG);
  dirOut = sector_;
  return status_;
}

ReturnGuide::BlockResult ReturnGuide::onBlocked() {
  if (status_ != Status::GUIDING) return BlockResult::IGNORED;
  int a = lastPassed_;
  int b = (idx_ < len_) ? route_[idx_] : -1;
  if (b >= 0 && a >= 0 && a != b) g_->blockEdge(a, b);  // DUMP에서 어디가 막혔는지 보이도록 표시
  sector_ = 255;
  status_ = Status::NO_ROUTE;  // 되짚을 길이 막히면 다른 길은 찾지 않음
  return BlockResult::NO_ROUTE;
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
