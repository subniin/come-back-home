#include "haptic.h"
#include "config.h"

namespace chh {

const char* signalName(Signal s) {
  switch (s) {
    case Signal::NONE:      return "NONE";
    case Signal::DIRECTION: return "DIRECTION";
    case Signal::CONFIRM:   return "CONFIRM";
    case Signal::BLOCKED:   return "BLOCKED";
    case Signal::MODE_AUTO: return "MODE_AUTO";
    case Signal::ARRIVED:   return "ARRIVED";
    case Signal::STOP:      return "STOP";
    case Signal::NO_ROUTE:  return "NO_ROUTE";
  }
  return "?";
}

static inline uint8_t bit(int i) { return (uint8_t)(1u << (i & 7)); }

HapticPattern makePattern(Signal s, uint8_t dir) {
  HapticPattern p;
  const uint8_t ALL = 0xFF;
  switch (s) {
    case Signal::DIRECTION:
      p.add(bit(dir), cfg::DIR_PULSE_MS);
      p.add(0, cfg::DIR_PERIOD_MS - cfg::DIR_PULSE_MS);
      p.repeat = true;
      break;
    case Signal::CONFIRM:
      p.add(bit(0), 80);
      p.add(0, 150);
      break;
    case Signal::BLOCKED:
      for (int k = 0; k < 2; k++) { p.add(bit(2) | bit(6), 150); p.add(0, 150); }
      p.add(0, 400);
      break;
    case Signal::MODE_AUTO:
      for (int k = 0; k < 2; k++) { p.add(bit(0) | bit(4), 400); p.add(0, 200); }
      p.add(0, 400);
      break;
    case Signal::ARRIVED:
      for (int r = 0; r < 2; r++)
        for (int i = 0; i < 8; i++) p.add(bit(i), 90);
      p.add(0, 300);
      break;
    case Signal::STOP:
      for (int k = 0; k < 3; k++) { p.add(ALL, 100); p.add(0, 100); }
      p.add(0, 900);
      p.repeat = true;
      break;
    case Signal::NO_ROUTE:
      p.add(ALL, 800);
      p.add(0, 2200);
      p.repeat = true;
      break;
    case Signal::NONE:
      break;
  }
  return p;
}

void HapticBelt::begin(MotorWriteFn fn) {
  write_ = fn;
  out_ = 0xFF;  // 강제로 전부 OFF 쓰기
  apply(0);
}

void HapticBelt::setBackground(Signal s, uint8_t dir) {
  dir &= 7;
  if (s == bgSig_ && (s != Signal::DIRECTION || dir == bgDir_) && (bg_.active || s == Signal::NONE))
    return;  // 같은 신호 → 리듬 유지
  bgSig_ = s;
  bgDir_ = dir;
  if (s == Signal::NONE) {
    bg_.active = false;
  } else {
    bg_.p = makePattern(s, dir);
    bg_.active = true;
    bg_.started = false;  // 방향 바뀌면 즉시 새 방향으로 진동
  }
}

void HapticBelt::playOnce(Signal s, uint8_t dir) {
  if (s == Signal::NONE) return;
  fg_.p = makePattern(s, dir);
  fg_.p.repeat = false;
  fg_.active = true;
  fg_.started = false;
  bg_.started = false;
}

void HapticBelt::allOff() {
  fg_.active = false;
  bg_.active = false;
  bgSig_ = Signal::NONE;
  apply(0);
}

bool HapticBelt::step(Player& pl, uint32_t now) {
  if (!pl.started) { pl.started = true; pl.i = 0; pl.t0 = now; }
  if (pl.p.n == 0) return false;
  if (now - pl.t0 > 10000) pl.t0 = now;  // 오래 멈춰 있었으면 따라잡지 않고 리셋
  while (now - pl.t0 >= pl.p.ms[pl.i]) {
    pl.t0 += pl.p.ms[pl.i];
    if (++pl.i >= pl.p.n) {
      if (!pl.p.repeat) return false;
      pl.i = 0;
    }
  }
  apply(pl.p.mask[pl.i]);
  return true;
}

void HapticBelt::tick(uint32_t now) {
  if (fg_.active) {
    if (step(fg_, now)) return;
    fg_.active = false;
    bg_.started = false;
  }
  if (bg_.active) {
    if (!step(bg_, now)) { bg_.active = false; apply(0); }
  } else {
    apply(0);
  }
}

void HapticBelt::apply(uint8_t mask) {
  if (mask == out_ || !write_) { out_ = mask; return; }
  for (int i = 0; i < cfg::MOTOR_COUNT; i++) {
    bool was = out_ & bit(i), now = mask & bit(i);
    if (was != now) write_((uint8_t)i, now);
  }
  out_ = mask;
}

}  // namespace chh
