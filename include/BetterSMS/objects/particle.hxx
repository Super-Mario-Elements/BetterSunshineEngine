#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JAudio/JAISound.hxx>
#include <JSystem/JDrama/JDRActor.hxx>
#include <JSystem/JUtility/JUTColor.hxx>

#include "libs/boundbox.hxx"
#include "module.hxx"

class TParticleBox : public JDrama::TActor {
public:
    BETTER_SMS_FOR_CALLBACK static JDrama::TNameRef *instantiate() {
        return new TParticleBox("TParticleBox");
    }

    TParticleBox(const char *name)
        : TActor(name), mID(-1), mSpawnRate(10), mSpawnScale(1.0f), mShape(BoundingType::Box), mIsStrict(false), mSpawnTimer() {}
    virtual ~TParticleBox() override {}

    void load(JSUMemoryInputStream &in) override;
    void perform(u32 flags, JDrama::TGraphics *) override;

private:
    TVec3f getRandomParticlePosition(const BoundingBox &bb) const;

    // Bin parameters
    const char *mParticleName;
    s32 mID;
    s32 mSpawnRate;
    f32 mSpawnScale;
    BoundingType mShape;
    bool mIsStrict;

    // Game state
    s32 mSpawnTimer;
};
