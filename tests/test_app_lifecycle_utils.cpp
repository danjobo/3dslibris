#include "shared/app_lifecycle_utils.h"

#include "test_assert.h"

using app_lifecycle_utils::AptEvent;
using app_lifecycle_utils::ApplyAptEvent;
using app_lifecycle_utils::LifecycleFlags;

namespace {

LifecycleFlags Run(LifecycleFlags flags, const AptEvent *events, int count) {
  for (int i = 0; i < count; i++)
    flags = ApplyAptEvent(flags, events[i]);
  return flags;
}

void TestSleepThenWake() {
  LifecycleFlags flags = ApplyAptEvent(LifecycleFlags(), AptEvent::Sleep);
  test::ExpectTrue("sleep suspends", flags.IsSuspended());
  test::ExpectTrue("sleep marks catch-up", flags.sleep_catchup_pending);
  test::ExpectFalse("sleep is not a resume", flags.resume_pending);

  flags = ApplyAptEvent(flags, AptEvent::Wakeup);
  test::ExpectFalse("wake unsuspends", flags.IsSuspended());
  test::ExpectTrue("wake requests resume", flags.resume_pending);
  test::ExpectTrue("catch-up survives wake", flags.sleep_catchup_pending);
}

void TestHomeSuspendThenRestore() {
  const AptEvent events[] = {AptEvent::Suspend, AptEvent::Restore};
  LifecycleFlags flags = Run(LifecycleFlags(), events, 2);
  test::ExpectFalse("restore unsuspends", flags.IsSuspended());
  test::ExpectTrue("restore requests resume", flags.resume_pending);
  test::ExpectFalse("HOME needs no sleep catch-up",
                    flags.sleep_catchup_pending);
}

void TestSuspendClearsHandledFlag() {
  LifecycleFlags flags;
  flags.suspend_handled = true;
  flags = ApplyAptEvent(flags, AptEvent::Suspend);
  test::ExpectFalse("new suspend must be handled again",
                    flags.suspend_handled);
}

void TestWakeWithoutSleep() {
  // Sleep-cancel style wake: must not leave the app suspended.
  LifecycleFlags flags = ApplyAptEvent(LifecycleFlags(), AptEvent::Wakeup);
  test::ExpectFalse("stray wake does not suspend", flags.IsSuspended());
  test::ExpectTrue("stray wake requests harmless resume",
                   flags.resume_pending);
  test::ExpectFalse("stray wake needs no catch-up",
                    flags.sleep_catchup_pending);
}

void TestSleepWhileInHome() {
  const AptEvent events[] = {AptEvent::Suspend, AptEvent::Sleep,
                             AptEvent::Wakeup};
  LifecycleFlags flags = Run(LifecycleFlags(), events, 3);
  test::ExpectTrue("waking into HOME stays suspended", flags.IsSuspended());
  test::ExpectFalse("waking into HOME does not resume", flags.resume_pending);

  flags = ApplyAptEvent(flags, AptEvent::Restore);
  test::ExpectFalse("restore from HOME unsuspends", flags.IsSuspended());
  test::ExpectTrue("restore from HOME resumes", flags.resume_pending);
}

void TestRestoreWhileStillAsleep() {
  const AptEvent events[] = {AptEvent::Suspend, AptEvent::Sleep,
                             AptEvent::Restore};
  LifecycleFlags flags = Run(LifecycleFlags(), events, 3);
  test::ExpectTrue("still asleep stays suspended", flags.IsSuspended());
  test::ExpectFalse("no resume until wake", flags.resume_pending);

  flags = ApplyAptEvent(flags, AptEvent::Wakeup);
  test::ExpectTrue("wake after restore resumes", flags.resume_pending);
}

void TestExitDuringSleep() {
  const AptEvent events[] = {AptEvent::Sleep, AptEvent::Exit};
  LifecycleFlags flags = Run(LifecycleFlags(), events, 2);
  test::ExpectTrue("exit is recorded while asleep", flags.exit_requested);
  test::ExpectTrue("exit keeps sleep state", flags.IsSuspended());
}

void TestOtherIsNoOp() {
  LifecycleFlags flags;
  flags.resume_pending = true;
  flags = ApplyAptEvent(flags, AptEvent::Other);
  test::ExpectTrue("unknown hook leaves state", flags.resume_pending);
  test::ExpectFalse("unknown hook does not suspend", flags.IsSuspended());
}

void TestOnlySleepQuiescesInHook() {
  test::ExpectTrue("sleep quiesces in hook",
                   app_lifecycle_utils::ShouldQuiesceWorkersInHook(
                       AptEvent::Sleep));
  test::ExpectFalse("HOME suspend defers to main loop",
                    app_lifecycle_utils::ShouldQuiesceWorkersInHook(
                        AptEvent::Suspend));
  test::ExpectFalse("wake does not quiesce",
                    app_lifecycle_utils::ShouldQuiesceWorkersInHook(
                        AptEvent::Wakeup));
}

} // namespace

int main() {
  TestSleepThenWake();
  TestHomeSuspendThenRestore();
  TestSuspendClearsHandledFlag();
  TestWakeWithoutSleep();
  TestSleepWhileInHome();
  TestRestoreWhileStillAsleep();
  TestExitDuringSleep();
  TestOtherIsNoOp();
  TestOnlySleepQuiescesInHook();
  return 0;
}
