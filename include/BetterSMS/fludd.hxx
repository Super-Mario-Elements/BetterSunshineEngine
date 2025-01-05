#pragma once
#include <SMS/Player/NozzleBase.hxx>

namespace BetterSMS {
    namespace Fludd {
        typedef TNozzleBase *(*NozzleInitializer)(TWaterGun *watergun);
        bool registerNozzle(const char *name, NozzleInitializer initFn, u32 numStreams, u32 jointEmitters[4]);
    }  // namespace Fludd
};     // namespace BetterSMS