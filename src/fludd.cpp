#include "fludd.hxx"

#include <raw_fn.hxx>
#include <sdk.h>
#include <string.h>
#include "libs/global_vector.hxx"

#include "logging.hxx"

#include <J3D/J3DModel.hxx>
#include <J3D/J3DModelLoaderDataBase.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/ModelWaterManager.hxx>
#include <SMS/Manager/RumbleManager.hxx>
#include <SMS/MarioUtil/TexUtil.hxx>
#include <SMS/MoveBG/NozzleBox.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/System/MarDirector.hxx>

using namespace BetterSMS;

#define MAX_NOZZLE_COUNT 15
#define MAX_EMITTERS 2

template <typename _I, typename _C> struct NozzleCallbackMeta {
    _I mID;
    _C mCallback;
    u32 mNumStreams;
    u32 mJointEmitter[MAX_EMITTERS];
};


static TGlobalVector<NozzleCallbackMeta<const char *, Fludd::NozzleInitializer>>
    sCustomNozzleList;
static TNozzleBase *gNozzles[MAX_NOZZLE_COUNT];
static u32 gNozzleNumberOfStreams[MAX_NOZZLE_COUNT] = {1, 1, 2, 1, 2, 1};
// TODO: This must be somewhat dynamic
// Which joint in the model should the stream come out of
static u32 gJointEmitters[MAX_NOZZLE_COUNT][MAX_EMITTERS] = {
    {7},
    {7},
    {2, 4},
    {2},
    {2, 4},
    {5},
    {7}
};

bool BetterSMS::Fludd::registerNozzle(const char *name, NozzleInitializer initFn, u32 numStreams, u32 jointEmitters[4]) {
    for (auto &item : sCustomNozzleList) {
        if (strcmp(item.mID, name) == 0) {
            Console::log("Object '%s' is already registered!\n", name);
            return false;
        }
    }

    NozzleCallbackMeta<const char*, Fludd::NozzleInitializer> metadata = {name, initFn, numStreams};
    for(int i = 0; i < MAX_EMITTERS; ++i) {
        metadata.mJointEmitter[i] = jointEmitters[i];
    }
    sCustomNozzleList.push_back(metadata);

    return true;
}

// Initialization of watergun + custom initialization logic
void TWaterGun_init_override(TWaterGun *watergun) {
    watergun->init();

    for (int i = 0; i < MAX_NOZZLE_COUNT; ++i) {
        gNozzles[i] = nullptr;
    }

    for (int i = 0; i < 6; ++i) {
        gNozzles[i] = watergun->mNozzleList[i];
    }

    // TODO: Initialization logic
    int idx = 6;
    for (auto &item : sCustomNozzleList) {
        gNozzles[idx] = item.mCallback(watergun);
        gNozzleNumberOfStreams[idx] = item.mNumStreams;
        for(int i = 0; i < MAX_EMITTERS; ++i) {
            gJointEmitters[idx][i] = item.mJointEmitter[i];
        }
        idx++;
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8027681c, 0, 0, 0), TWaterGun_init_override);

TNozzleBase *TWaterGun_getCurrentNozzle_override(TWaterGun *watergun) {
    // Due to this being called in init, we need to do this to avoid crashing
    if(watergun->mCurrentNozzle < 6) {
        return watergun->mNozzleList[watergun->mCurrentNozzle];
    }
    return gNozzles[watergun->mCurrentNozzle];
}
SMS_PATCH_B(SMS_PORT_REGION(0x80269610, 0, 0, 0), TWaterGun_getCurrentNozzle_override);

// We might want to override this for cases where we want custom emitting logic. E.g snow nozzle.
// void TWaterGun_emit_override(TWaterGun *watergun) {
//    if (watergun->mCurrentNozzle < 6) {
//        watergun->emit();
//    } else {
//        watergun->emit();
//        // TODO Emit;
//    }
//}
// SMS_PATCH_BL(SMS_PORT_REGION(0x8024e51c, 0, 0, 0), TWaterGun_emit_override);
// SMS_PATCH_BL(SMS_PORT_REGION(0x8024e548, 0, 0, 0), TWaterGun_emit_override);
// SMS_PATCH_BL(SMS_PORT_REGION(0x8024e58c, 0, 0, 0), TWaterGun_emit_override);

// Gets mtx of where water should be emitted from.
Mtx *TWaterGun_getEmitMtx_override(TWaterGun *that, int numberOfStream) {
    Mtx *result = nullptr;
    if (!that->mMario->onYoshi()) {
        if (that->mCurrentNozzle == 3) {
            TYoshi *yoshi = that->mMario->mYoshi;
            result        = yoshi->mActor->mModel->mJointArray;
        } else if (numberOfStream < MAX_EMITTERS) {
            TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
            if (gNozzleNumberOfStreams[that->mCurrentNozzle] <= numberOfStream) {
                OSPanic(__FILE__, __LINE__,
                        "Tried to get emit mtx for joint outside max number of streams. max %d idx "
                        "%d\n",
                        gNozzleNumberOfStreams[that->mCurrentNozzle], numberOfStream);
            }
            int idx = gJointEmitters[that->mCurrentNozzle][numberOfStream];
            result = currentNozzle->mModel->mModel->mJointArray + idx;
        }

    } else {
        TYoshi *yoshi = that->mMario->mYoshi;
        result        = yoshi->mActor->mModel->mJointArray;
    }

    return result;
}
SMS_PATCH_B(SMS_PORT_REGION(0x8026a2c4, 0, 0, 0), TWaterGun_getEmitMtx_override);

// TODO: Might want custom suck logic for nozzles. E.g snow nozzle
// Triggered when in water and fludd should refill
bool TWaterGun_suck_override(TWaterGun *that) {
    if (that->mCurrentNozzle == 3) {
        return false;
    }

    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);

    s32 suckPower = (s32)((f32)that->mCurrentPressure * currentNozzle->mEmitParams.mSuckRate.get());
    if (suckPower == 0) {
        return false;
    }

    that->mCurrentWater += suckPower;
    s32 maxWater = currentNozzle->mEmitParams.mAmountMax.get();
    if (that->mCurrentWater > maxWater) {
        that->mCurrentWater = maxWater;
    }

    if (that->mCurrentWater < maxWater) {
        if (gpMSound->gateCheck(0xf)) {
            MSoundSE::startSoundActor(0xf, that->mEmitPos[0], 0, nullptr, 0, 4);
        }
    }
    return true;
}
SMS_PATCH_B(SMS_PORT_REGION(0x80268e84, 0, 0, 0), TWaterGun_suck_override);

void TWaterGun_movement_override(TWaterGun *that) {

    bool canSpray              = false;  // Not sure if this is correct variable name
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    if (that->mCurrentWater == 0) {
        canSpray = false;
    } else {
        u16 gameState = gpMarDirector->mGameState;
        if (gameState != 3 && gameState != 4) {
            canSpray = true;
        }
        if (gameState != 1 && gameState != 2) {
            canSpray = false;
        }
        if (!canSpray) {
            u8 kind = currentNozzle->getNozzleKind();
            if (kind == 1) {
                TNozzleTrigger *triggerNozzle = (TNozzleTrigger *)currentNozzle;
                canSpray = triggerNozzle->mSprayState == TNozzleTrigger::ACTIVE;
            } else if (currentNozzle->_378 <= 0.0) {
                canSpray = false;
            } else {
                canSpray = true;
            }
        } else {
            canSpray = false;
        }
    }

    if (!canSpray) {
        that->_1cc2 = 0;
        that->_1cc4 = 0;
    }

    f32 prevY = (f32)that->_1cc2;
    f32 prevZ = (f32)that->_1cc4;
    that->mNozzleSpeedY += (prevY - that->mNozzleSpeedY) * that->mWatergunParams.mChangeSpeed.get();
    that->mNozzleSpeedZ += (prevZ - that->mNozzleSpeedZ) * that->mWatergunParams.mChangeSpeed.get();

    // Turbo, i think this is forward speed?
    if (that->mCurrentNozzle == 5) {
        that->_1cd2 += currentNozzle->_378 * that->mWatergunParams.mNozzleAngleYSpeed.get();
        that->_1cd2 *= that->mWatergunParams.mNozzleAngleYBrake.get();
        if (that->_1cd2 > that->mWatergunParams.mNozzleAngleYSpeedMax.get()) {
            that->_1cd2 = that->mWatergunParams.mNozzleAngleYSpeedMax.get();
        }
        that->_1cd0 = that->_1cd0 + that->_1cd2;
    } else {
        that->_1cd0 = 0;
        that->_1cd2 = 0;
    }

    // It does the same again???
    if (that->mCurrentNozzle == 5) {
        that->_1cd2 += currentNozzle->_378 * that->mWatergunParams.mNozzleAngleYSpeed.get();
        that->_1cd2 *= that->mWatergunParams.mNozzleAngleYBrake.get();
        if (that->_1cd2 > that->mWatergunParams.mNozzleAngleYSpeedMax.get()) {
            that->_1cd2 = that->mWatergunParams.mNozzleAngleYSpeedMax.get();
        }
        that->_1cd0 = that->_1cd0 + that->_1cd2;
    } else {
        that->_1cd0 = 0;
        that->_1cd2 = 0;
    }

    // Yoshi nozzle
    if (that->mCurrentNozzle == 3) {
        that->mCurrentWater = that->mNozzleList[3]->mEmitParams.mAmountMax.get();
    }

    if (!SMS_isDivingMap__Fv()) {
        that->_26 = 0.0;
    }

    // Nozzle swapping
    if (that->_1d00 != 0.0) {
        f32 unk     = that->_1cfc;
        f32 sum     = unk + that->_1d00;
        that->_1cfc = sum;
        if ((unk < 0.5) && (0.5 <= sum)) {
            u8 secondNozzle     = that->mSecondNozzle;
            f32 currentWater    = (f32)that->mCurrentWater;
            f32 maxWater        = currentNozzle->mEmitParams.mAmountMax.get();
            f32 waterPercentage = currentWater / maxWater;

            if (secondNozzle != 0) {
                that->mSecondNozzle = secondNozzle;
            }
            that->mCurrentNozzle = secondNozzle;

            currentNozzle = TWaterGun_getCurrentNozzle_override(that);
            currentNozzle->init();  // TODO: 2 vtable entry

            if (secondNozzle == 3) {
                that->mCurrentWater = that->mMario->mYoshi->_11[0];  // TODO: Proper yoshi stuff
            } else {
                that->mCurrentWater = waterPercentage * currentNozzle->mEmitParams.mAmountMax.get();
            }
        }
        if ((sum < 0.5) && (0.5 <= unk)) {
            f32 currentWater    = (f32)that->mCurrentWater;
            f32 maxWater        = currentNozzle->mEmitParams.mAmountMax.get();
            f32 waterPercentage = currentWater / maxWater;

            that->mCurrentNozzle = 0;

            currentNozzle = TWaterGun_getCurrentNozzle_override(that);
            currentNozzle->init();  // TODO: 2 vtable entry

            that->mCurrentWater = waterPercentage * currentNozzle->mEmitParams.mAmountMax.get();
        }

        if (that->_1cfc < 0.0) {
            that->_1cfc = 0.0f;
            that->_1d00 = 0.0f;
        }
        if (1.0f < that->_1cfc) {
            that->_1cfc = 1.0f;
            that->_1d00 = 0.0f;
        }
    }
    currentNozzle->animation(0);
}
SMS_PATCH_B(SMS_PORT_REGION(0x80269be4, 0, 0, 0), TWaterGun_movement_override);

// Update function of fludd
void TWaterGun_perform_override(TWaterGun *that, u32 flags, JDrama::TGraphics *graphics) {

    if ((flags & 0x1) != 0) {
        if (that->mFlags & 0x10) {
            that->mCurrentWater = 0;
        }
        that->movement();
    }

    if ((flags & 0x2) != 0) {
        that->calcAnimation(graphics);
    }

    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    MActor *nozzleMactor       = currentNozzle->mModel;
    that->mFluddModel->perform(flags, graphics);
    if ((flags & 0x2) != 0) {
        if (nozzleMactor != nullptr) {
            PSMTXCopy(that->mFluddModel->mModel->mJointArray[that->mCurFluddTransformIdx],
                      nozzleMactor->mModel->mBaseMtx);
        }

        // Set emit positions for each stream
        // Note: I am unsure if it supports more than 2 natively, might have to expand this if more
        // than 2 emitters
        for (int i = 0; i < gNozzleNumberOfStreams[that->mCurrentNozzle]; ++i) {
            Mtx *mtx = that->getEmitMtx(i);
            if (mtx != nullptr) {
                that->mEmitPos[i].x = (*mtx)[0][3];
                that->mEmitPos[i].y = (*mtx)[1][3];
                that->mEmitPos[i].z = (*mtx)[2][3];
            }
        }
    }

    if (nozzleMactor != nullptr) {
        nozzleMactor->perform(flags, graphics);
    }
}
SMS_WRITE_32(SMS_PORT_REGION(0x803dd4a8, 0, 0, 0), &TWaterGun_perform_override);

void TWaterGun_triggerPressureMovement_override(TWaterGun *that,
                                                const TMarioControllerWork &controllerWork) {
    that->mCurrentPressure = (char)(int)(controllerWork.mAnalogL * 150.0);

    auto *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    currentNozzle->movement(controllerWork);

    if (that->mPreviousPressure < that->mCurrentPressure) {
        that->mPreviousPressure = that->mCurrentPressure;
    } else if (that->mPreviousPressure == 0) {
        that->mPreviousPressure = 0;
    } else {
        that->mPreviousPressure -= 1;
    }
}
SMS_PATCH_B(SMS_PORT_REGION(0x80269240, 0, 0, 0), TWaterGun_triggerPressureMovement_override);

f32 TWaterGun_getPressureMax_override(TWaterGun *that) {
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    u8 kind                    = currentNozzle->getNozzleKind();
    f32 pressureMax            = 0.0f;
    if (kind == 1) {
        pressureMax = currentNozzle->mEmitParams.mInsidePressureMax.get();
    }
    return pressureMax;
}
SMS_PATCH_B(SMS_PORT_REGION(0x8026944c, 0, 0, 0), TWaterGun_getPressureMax_override);

f32 TWaterGun_getPressure_override(TWaterGun *that) {
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    u8 kind                    = currentNozzle->getNozzleKind();
    f32 pressureMax            = 0.0f;
    if (kind == 1) {
        TNozzleTrigger *triggerNozzle = (TNozzleTrigger *)currentNozzle;
        pressureMax                   = triggerNozzle->mTriggerFill;
    }
    return pressureMax;
}
SMS_PATCH_B(SMS_PORT_REGION(0x802694b8, 0, 0, 0), TWaterGun_getPressure_override);

bool TWaterGun_isPressureOn_override(TWaterGun *that) {
    TNozzleBase *currentNozzle    = TWaterGun_getCurrentNozzle_override(that);
    TNozzleTrigger *triggerNozzle = (TNozzleTrigger *)currentNozzle;
    u8 kind                       = currentNozzle->getNozzleKind();
    bool isPressureOn             = false;
    if (kind == 1) {
        isPressureOn = triggerNozzle->mTriggerFill >= 0.0f;
    }
    return isPressureOn;
}
SMS_PATCH_B(SMS_PORT_REGION(0x80269524, 0, 0, 0), TWaterGun_isPressureOn_override);

void TWaterGun_setAmountToRate_override(TWaterGun *that, f32 rate) {
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    u8 kind                    = currentNozzle->getNozzleKind();

    if (that->mCurrentNozzle == 3) {
        that->mCurrentWater = currentNozzle->mEmitParams.mAmountMax.get();
    } else {
        that->mCurrentWater = rate * currentNozzle->mEmitParams.mAmountMax.get();  // CONCAT44?
    }
    return;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80276848, 0, 0, 0), TWaterGun_setAmountToRate_override);

bool TWaterGun_isEmitting_override(TWaterGun *that) {
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);

    int gameState = gpMarDirector->mGameState;
    if (that->mCurrentWater != 0 && gameState != 3 && gameState != 4) {
        bool isPaused = true;
        if (gameState != 1 && gameState != 2) {
            isPaused = false;
        }

        if (!isPaused) {
            u8 kind = currentNozzle->getNozzleKind();
            if (kind != 1) {
                if (currentNozzle->_378 <= 0.0) {
                    return 0;
                }
                return 1;
            }
            TNozzleTrigger *triggerNozzle = (TNozzleTrigger *)currentNozzle;
            if (triggerNozzle->mSprayState != 1) {
                return 0;
            }
            return 1;
        }
    }
    return 0;
}
SMS_PATCH_B(SMS_PORT_REGION(0x80269624, 0, 0, 0), TWaterGun_isEmitting_override);

void TWaterGun_changeNozzle_override(TWaterGun *that, u32 nozzleType, bool replenishWater) {
    TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
    f32 waterPercent           = that->mCurrentWater / currentNozzle->mEmitParams.mAmountMax.get();

    if (nozzleType == 0) {
        if (replenishWater) {
            that->_1cfc = 0.0;
        }
    } else {
        that->mSecondNozzle = nozzleType;
        if (replenishWater) {
            that->_1cfc = 1.0;
        }
    }

    that->mCurrentNozzle = nozzleType;
    currentNozzle        = TWaterGun_getCurrentNozzle_override(that);
    currentNozzle->init();
    if (nozzleType == 3) {
        that->mCurrentWater = that->mMario->mYoshi->_11[0];
    } else {
        that->mCurrentWater = currentNozzle->mEmitParams.mAmountMax.get() * waterPercent;
    }
}
SMS_PATCH_B(SMS_PORT_REGION(0x8026a168, 0, 0, 0), TWaterGun_changeNozzle_override);

void TWatergun_emit_override(TWaterGun *that) {
    bool isOnYoshi       = that->mMario->onYoshi();
    bool hasHelmetCamera = that->mMario->mAttributes.mGainHelmetFlwCamera;
    bool isInWater       = that->mMario->mAttributes.mIsShallowWater ||
                     that->mMario->mAttributes.mIsWater;
    bool someTreshold = that->mGeometry3.z <= 0.0f;  // mGeometry3 is obviously wrong...

    Mtx *emitMtx     = that->getEmitMtx(0);
    bool heightCheck = false;
    if (emitMtx != nullptr) {
        f32 emitHeight = *(f32 *)((u32)emitMtx + 0x1c);
        heightCheck    = that->mMario->mWaterHeight + 20.0f <= emitHeight;
    }

    if ((hasHelmetCamera || !isInWater || heightCheck) && (isOnYoshi || someTreshold)) {
        if ((that->mFlags & 4) == 0) {
            u8 currentNozzleId         = that->mCurrentNozzle;
            TNozzleBase *currentNozzle = TWaterGun_getCurrentNozzle_override(that);
            for (int i = 0; i < gNozzleNumberOfStreams[currentNozzleId]; ++i) {
                currentNozzle->emit(i);
            }
            if (that->mCurrentWater > 0) {
                if (currentNozzleId == 1) {
                    return;
                } else if (currentNozzleId == 2) {
                    if (!gpMSound->gateCheck(0x18))
                        return;

                    MSoundSE::startSoundActor(0x18, that->mEmitPos[0], 0, nullptr, 0, 4);
                    return;
                } else if (currentNozzleId == 0) {
                    if (gpMSound->gateCheck(0x24)) {
                        MSoundSE::startSoundActorWithInfo(0x24, that->mEmitPos[0], nullptr, 0.0, 0,
                                                          0, nullptr, 0, 4);
                    }
                } else if (currentNozzleId == 3) {
                    return;
                } else if (currentNozzleId == 4) {
                    if (!that->mIsEmitWater || !gpMSound->gateCheck(0x18))
                        return;

                    MSoundSE::startSoundActor(0x18, that->mEmitPos[0], 0, nullptr, 0, 4);
                } else if (currentNozzleId == 5) {
                    if (gpMSound->gateCheck(0x0)) {
                        u32 nozzleValue = (u32)currentNozzle->_378;
                        MSoundSE::startSoundActorWithInfo(0x0, that->mEmitPos[0], nullptr, 0,
                                                          nozzleValue, 0, nullptr, 0, 4);
                    }
                }
            }
        } else {
            that->mFlags &= 0xfffb;
        }
    }
}
SMS_PATCH_B(SMS_PORT_REGION(0x80268f9c, 0, 0, 0), TWatergun_emit_override);
