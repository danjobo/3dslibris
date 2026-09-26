#include "shared/app_lifecycle_utils.h"

namespace app_lifecycle_utils {

LifecycleFlags ApplyAptEvent(LifecycleFlags flags, AptEvent event) {
  switch (event) {
  case AptEvent::Suspend:
    flags.home_suspended = true;
    flags.resume_pending = false;
    flags.suspend_handled = false;
    break;
  case AptEvent::Sleep:
    flags.sleeping = true;
    flags.resume_pending = false;
    flags.sleep_catchup_pending = true;
    break;
  case AptEvent::Restore:
    flags.home_suspended = false;
    // Sleeping while in HOME is possible; stay suspended until wake.
    if (!flags.sleeping)
      flags.resume_pending = true;
    break;
  case AptEvent::Wakeup:
    flags.sleeping = false;
    // Waking into HOME must not resume the app; ONRESTORE will.
    if (!flags.home_suspended)
      flags.resume_pending = true;
    break;
  case AptEvent::Exit:
    flags.exit_requested = true;
    break;
  case AptEvent::Other:
    break;
  }
  return flags;
}

bool ShouldQuiesceWorkersInHook(AptEvent event) {
  return event == AptEvent::Sleep;
}

} // namespace app_lifecycle_utils
