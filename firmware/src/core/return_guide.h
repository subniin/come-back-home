// 복귀 안내(백트래킹): 지나온 노드 순서를 역순으로 되짚으며 "지금 어느 방향 모터를 울릴지" 계산
//  - 복귀 경로 = 기록기(PathRecorder)의 trail을 거꾸로 뒤집은 것 (최단경로가 아님)
//  - 막다른 길 왕복 구간도 지나온 그대로 다시 지나감
//  - 되짚을 길이 막히면 다른 길을 찾지 않고 NO_ROUTE
#pragma once
#include <stdint.h>
#include "config.h"
#include "path_graph.h"

namespace chh {

class ReturnGuide {
 public:
  enum class Status : uint8_t { GUIDING, ARRIVED, NO_ROUTE };
  enum class BlockResult : uint8_t { NO_ROUTE, IGNORED };

  // rec의 trail(출발→현재)을 뒤집어 복귀 경로(현재→출발)로 만든다
  Status begin(PathGraph* g, const PathRecorder& rec);
  // 현재 위치·헤딩 → 상태와 모터 번호(dirOut, 0~7)
  Status update(float x, float y, float headingDeg, uint8_t& dirOut);
  // 되짚던 길이 막힘: 그 간선을 막힘으로 표시하고 NO_ROUTE
  BlockResult onBlocked();

  int  nextWaypoint() const { return (idx_ < len_) ? route_[idx_] : -1; }
  int  lastPassed() const { return lastPassed_; }
  int  routeLen() const { return len_; }
  int  routeAt(int i) const { return route_[i]; }
  int  routeIndex() const { return idx_; }
  float remainingM(float x, float y) const;

 private:
  PathGraph* g_ = nullptr;
  uint8_t route_[cfg::MAX_TRAIL];
  int len_ = 0, idx_ = 0, lastPassed_ = -1;
  uint8_t sector_ = 255;
  Status status_ = Status::NO_ROUTE;
};

}  // namespace chh
