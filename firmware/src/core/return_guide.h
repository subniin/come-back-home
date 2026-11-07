// 복귀 안내: 그래프 위 최단경로를 웨이포인트 단위로 따라가며 "지금 어느 방향 모터를 울릴지" 계산
#pragma once
#include <stdint.h>
#include "config.h"
#include "path_graph.h"

namespace chh {

class ReturnGuide {
 public:
  enum class Status : uint8_t { GUIDING, ARRIVED, NO_ROUTE };
  enum class BlockResult : uint8_t { REROUTED, NO_ROUTE, IGNORED };

  Status begin(PathGraph* g, int fromNode, int goalNode);
  // 현재 위치·헤딩 → 상태와 모터 번호(dirOut, 0~7)
  Status update(float x, float y, float headingDeg, uint8_t& dirOut);
  // 전방 차단: 지금 가던 간선을 막고, 마지막으로 지난 노드로 되돌아가 다른 길 탐색(백트래킹)
  BlockResult onBlocked();

  int  nextWaypoint() const { return (idx_ < len_) ? route_[idx_] : -1; }
  int  lastPassed() const { return lastPassed_; }
  int  routeLen() const { return len_; }
  int  routeAt(int i) const { return route_[i]; }
  int  routeIndex() const { return idx_; }
  float remainingM(float x, float y) const;

 private:
  bool replan(int from);
  PathGraph* g_ = nullptr;
  uint8_t route_[cfg::MAX_NODES];
  int len_ = 0, idx_ = 0, goal_ = 0, lastPassed_ = -1;
  uint8_t sector_ = 255;
  Status status_ = Status::NO_ROUTE;
};

}  // namespace chh
