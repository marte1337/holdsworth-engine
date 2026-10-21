// Reuse the accepted native APP/AU/VST3 hosts and their actual production APIs.
// Include their acceptance header first so its definitions are not renamed.
#include "../tuner-v1/host-acceptance.h"
#include "integration-acceptance.h"
#define runHostAcceptance runPostAmpEQAcceptance
#if defined(EQ_HOST_APP)
#include "../tuner-v1/app-host.mm"
#elif defined(EQ_HOST_AU)
#include "../tuner-v1/au-host.mm"
#elif defined(EQ_HOST_VST3)
#include "../tuner-v1/vst3-host.mm"
#else
#error Select an EQ host format
#endif
