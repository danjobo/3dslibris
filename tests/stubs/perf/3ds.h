#pragma once
#include <cstdint>
extern uint64_t fake_tick;
inline uint64_t svcGetSystemTick() {
  return fake_tick;
}
#define SYSCLOCK_ARM11 1000000ULL
#define MEMREGION_ALL 0
inline unsigned osGetMemRegionFree(int) {
  return 1234;
}
inline unsigned linearSpaceFree() {
  return 5678;
}

#include <mutex>
typedef std::mutex LightLock;
inline void LightLock_Init(LightLock *) {}
inline void LightLock_Lock(LightLock *l) {
  l->lock();
}
inline void LightLock_Unlock(LightLock *l) {
  l->unlock();
}
