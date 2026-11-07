// 경로 그래프 + 경로 기록기
//  - 진입하며 방향전환 지점을 노드로 저장 (분기 노드 기반 그래프)
//  - 이미 지나간 지점 근처로 다시 오면 같은 노드로 병합 → 막다른 길 탐색 등 우회분을 복귀 때 건너뜀
//  - 복귀 경로 = 현재 노드 → 출발 노드 Dijkstra 최단경로 (막힌 간선 제외)
//    ※ 병합이 한 번도 없으면 결과는 '역순(LIFO) 되짚기'와 동일
//  - 사전 공간정보(평면도)가 있으면 같은 그래프 구조로 미리 로드해서 사용
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
  // from → to 최단경로. out[0]=from ... out[len-1]=to. 경로 없으면 0 반환
  int  shortestPath(int from, int to, uint8_t* out, int maxLen) const;

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
  // 사전 지도 모드: 그래프는 건드리지 않고 기록만 끔
  void attachPrior(PathGraph* g, int startNode);
  // 걸음마다 호출 (이전 위치, 새 위치, 헤딩)
  void onStep(float xPrev, float yPrev, float x, float y, float headingDeg);
  // 현재 위치에 노드 강제 생성 (수동 MARK, 복귀 시작 시점)
  // atTurn=true: 방향전환 지점. 직전 노드가 직선 구간 자동 노드이고 아주 가까우면 그 노드를 모퉁이로 옮김
  int  commitNode(float x, float y, bool atTurn = false);
  int  current() const { return cur_; }
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
};

}  // namespace chh
