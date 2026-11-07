// 허리 8채널 햅틱 신호 체계
//  - 세기(PWM)는 쓰지 않고 "어느 모터 / 몇 번 / 어떤 리듬"으로만 구분 (멘토링 확정사항)
//  - 하드웨어와 분리: 모터 ON/OFF 함수만 넘겨주면 어떤 드라이버든 사용 가능
//  - 신호 정의는 docs/HAPTIC_SPEC.md 참고 (다른 팀 재사용용)
#pragma once
#include <stdint.h>

namespace chh {

enum class Signal : uint8_t {
  NONE = 0,
  DIRECTION,  // 해당 방향 모터 1회, 1.2초마다 반복         (이동 방향)
  CONFIRM,    // 정면 아주 짧게 1회                         (명령/버튼 확인)
  BLOCKED,    // 양옆(2,6) 짧게 2회                          (경로 차단 → 재탐색)
  MODE_AUTO,  // 앞뒤(0,4) 길게 2회                          (연결 끊김 → 자율 모드)
  ARRIVED,    // 0→7 한 바퀴 회전 2회                         (복귀 완료)
  STOP,       // 전체 짧게 3회, 반복                          (즉시 정지/위험)
  NO_ROUTE,   // 전체 길게 1회, 3초마다 반복                   (복귀 경로 없음 → 대기)
};
const char* signalName(Signal s);

typedef void (*MotorWriteFn)(uint8_t motorIndex, bool on);

struct HapticPattern {
  static constexpr int MAX_STEPS = 20;
  uint8_t  mask[MAX_STEPS];  // bit i = 모터 i ON
  uint16_t ms[MAX_STEPS];
  uint8_t  n = 0;
  bool     repeat = false;
  void add(uint8_t m, uint16_t t) {
    if (n < MAX_STEPS) { mask[n] = m; ms[n] = t; n++; }
  }
};
HapticPattern makePattern(Signal s, uint8_t dir = 0);

class HapticBelt {
 public:
  void begin(MotorWriteFn fn);
  // 배경(반복) 신호: 방향 안내, STOP, NO_ROUTE. NONE이면 끔. 같은 신호면 리듬 유지.
  void setBackground(Signal s, uint8_t dir = 0);
  // 1회성 신호: 재생 중엔 배경을 잠시 멈추고, 끝나면 배경을 처음부터 재개
  void playOnce(Signal s, uint8_t dir = 0);
  void allOff();
  void tick(uint32_t nowMs);  // loop()에서 계속 호출 (논블로킹)

  Signal  background() const { return bgSig_; }
  uint8_t backgroundDir() const { return bgDir_; }
  bool    busy() const { return fg_.active; }
  uint8_t outputMask() const { return out_; }

 private:
  struct Player {
    HapticPattern p;
    uint8_t  i = 0;
    uint32_t t0 = 0;
    bool active = false, started = false;
  };
  bool step(Player& pl, uint32_t now);  // false면 재생 끝
  void apply(uint8_t mask);

  MotorWriteFn write_ = nullptr;
  Player  fg_, bg_;
  Signal  bgSig_ = Signal::NONE;
  uint8_t bgDir_ = 0;
  uint8_t out_ = 0;
};

}  // namespace chh
