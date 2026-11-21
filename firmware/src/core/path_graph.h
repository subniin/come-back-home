// 경로 그래프 + 경로 기록기
//  - 진입하며 방향전환 지점을 노드로 저장 (분기 노드 기반 그래프)
//  - 이미 지나간 지점 근처로 다시 오면 새 노드를 만들지 않고 기존 노드를 재사용(병합)
//  - 지나온 노드를 순서대로 trail에 쌓음 (재방문 포함) → 복귀 = trail을 역순으로 되짚기(LIFO)
//    ※ 막다른 길에 들어갔다 나온 구간도 지나온 그대로 다시 지나감 (지름길로 건너뛰지 않음)
#pragma once
#include <stdint.h>
#include "config.h"

namespace chh {

struct GNode { float x, y; };
struct GEdge { uint8_t a, b; float len; bool blocked; };

class PathGraph {
 public:
  void clear() { nN_ = 0; nE_ = 0; }
  int  addNode(float x, float y);                       // 꽉 차면 -1
  int  findNear(float x, float y, float r) const;       // r 이내 가장 가까운 노드, 없으면 -1
  int  nearest(float x, float y) const;
  bool addEdge(int a, int b);                           // 중복이면 무시
  int  findEdge(int a, int b) const;
  bool blockEdge(int a, int b);
  void moveNode(int i, float x, float y);              // 위치 수정 + 연결 간선 길이 재계산
  void unblockAll();

  int nodeCount() const { return nN_; }
  int edgeCount() const { return nE_; }
  const GNode& node(int i) const { return nodes_[i]; }
  const GEdge& edge(int i) const { return edges_[i]; }

 private:
  GNode nodes_[cfg::MAX_NODES];
  GEdge edges_[cfg::MAX_EDGES];
  int nN_ = 0, nE_ = 0;
};

class PathRecorder {
 public:
  // 새 임무 시작: 그래프 초기화, 현재 위치를 출발 노드(0번)로
  void begin(PathGraph* g, float x, float y, float headingDeg);
  // 걸음마다 호출 (이전 위치, 새 위치, 헤딩)
  void onStep(float xPrev, float yPrev, float x, float y, float headingDeg);
  // 현재 위치에 노드 강제 생성 (수동 MARK, 복귀 시작 시점)
  // atTurn=true: 방향전환 지점. 직전 노드가 직선 구간 자동 노드이고 아주 가까우면 그 노드를 모퉁이로 옮김
  int  commitNode(float x, float y, bool atTurn = false);
  int  current() const { return cur_; }
  // 지나온 노드 순서: trailAt(0)=출발 노드 ... trailAt(trailLen()-1)=현재 노드
  int  trailLen() const { return trailLen_; }
  int  trailAt(int i) const { return trail_[i]; }
  bool recording() const { return enabled_; }
  bool full() const { return full_; }

 private:
  PathGraph* g_ = nullptr;
  int   cur_ = -1;
  float segHead_ = 0;
  int   pending_ = 0;
  float turnX_ = 0, turnY_ = 0;
  bool  enabled_ = false;
  bool  full_ = false;
  bool  curIsTurn_ = true;  // 현재 노드가 모퉁이(또는 출발점/병합점)인지, 직선 구간 자동 노드인지
  uint8_t trail_[cfg::MAX_TRAIL];
  int   trailLen_ = 0;
  void  pushTrail(int n);
};

}  // namespace chh
