// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <types.h>

namespace motherbase {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct WaterSample {
    bool  hasWater;
    float surfaceY;
    float bottomY;
    float depth;
    s32   nativeCode;
};

using WaterQueryFn = bool (*)(void* context, float x, float z, WaterSample* out);

enum class Phase : u8 {
    Idle = 0,
    ScanAhead,
    ValidateFootprint,
    ValidateIntent,
    ValidateClearance,
    Ready,
    Rejected,
};

enum class RejectReason : u8 {
    None = 0,
    NoWaterAhead,
    WaterTooShallow,
    FootprintNotWater,
    SurfaceUneven,
    Obstructed,
    ClearanceTimedOut,
    WaterNotVisible,
    WaterOutsideHeightLimit,
    VisibilityTimedOut,
    QueryUnavailable,
    InvalidFacing,
};

struct Config {
    float scanStartM;
    float scanStepM;
    float scanMaxM;
    float castOutM;
    float minAnchorDistanceM;
    float candidateAdvanceM;
    float maxAnchorDistanceM;
    float minDepthM;
    float surfaceToleranceM;
    float deckClearanceM;
    float footprintHalfWidthM;
    float footprintBackM;
    float footprintFrontM;
    float clearanceTopM;
    float clearanceBottomInsetM;
    float intentEyeHeightM;
    float intentOriginForwardM;
    float intentTargetAboveSurfaceM;
    float maxSurfaceBelowLinkM;
    float maxSurfaceAboveLinkM;
    u8    gridColumns;
    u8    gridRows;
};

extern const Config kMvpConfig;

// Ledger v1 compatibility: old records only carry bit 0 and therefore remain cliff sites.
constexpr u32 ledgerSiteFlags(bool waterMode) { return 1u | (waterMode ? 2u : 0u); }
constexpr bool ledgerSiteIsWater(u32 flags) { return (flags & 2u) != 0; }
constexpr bool shouldRefundAdditionalTrigger(bool isTrigger, bool placementPending) {
    return isTrigger && placementPending;
}
constexpr bool shouldRefundPlacementFailure(bool visibleWaterIntent, bool fallbackFromCliff) {
    return visibleWaterIntent || fallbackFromCliff;
}

struct IntentRay {
    Vec3 from;
    Vec3 to;
};

struct ClearanceRay {
    Vec3 from;
    Vec3 to;
    u16  sampleIndex;
};

struct ReadySite {
    Vec3 anchor;
    float nx;
    float nz;
    float surfaceY;
    float minDepthM;
    float maxDepthM;
    float anchorDistanceM;
};

class Placement {
public:
    void reset();
    void begin(const Vec3& linkPosition, float faceX, float faceZ);

    // Advances the synchronous terrain-water portion within the caller's per-tick budget.
    void advanceWater(WaterQueryFn query, void* queryContext, u32 budget);

    bool wantsIntentRay() const;
    IntentRay intentRay() const;
    void acceptIntentResult(bool timedOut, bool hit, float hitY, u32 bodyId);

    bool wantsClearanceRay() const;
    ClearanceRay clearanceRay() const;
    void acceptClearanceResult(bool timedOut, bool hit, float hitY, u32 bodyId);

    Phase phase() const { return mPhase; }
    RejectReason rejectReason() const { return mRejectReason; }
    bool shouldRefund() const { return mVisibleWaterIntent; }
    bool sawWater() const { return mSawWater; }
    bool hasIntentTarget() const { return mHasIntentTarget; }
    bool visibleWaterIntent() const { return mVisibleWaterIntent; }
    const Vec3& intentTarget() const { return mIntentTarget; }
    float intentSurfaceDeltaM() const { return mIntentSurfaceDeltaM; }
    u32 intentBodyId() const { return mIntentBodyId; }
    float intentHitY() const { return mIntentHitY; }
    const ReadySite& readySite() const { return mReady; }

    u16 waterSamplesTested() const { return mWaterSamplesTested; }
    u16 clearanceSamplesTested() const { return mClearanceIndex; }
    u8 candidateAttempts() const { return mCandidateAttempts; }
    s32 lastNativeCode() const { return mLastNativeCode; }
    float failedSampleDepthM() const { return mFailedSampleDepthM; }
    float failedSampleSurfaceDeltaM() const { return mFailedSampleSurfaceDeltaM; }
    u32 obstructionBodyId() const { return mObstructionBodyId; }
    float obstructionHitY() const { return mObstructionHitY; }

private:
    void reject(RejectReason reason);
    void rememberIntentTarget(float x, float surfaceY, float z);
    void beginIntentValidation(RejectReason rejectAfterVisibleIntent, Phase phaseAfterVisibleIntent);
    void finishWaterFailure(RejectReason reason);
    void startCandidate(float anchorDistanceM);
    void advanceCandidateOrReject(RejectReason reason);
    void gridPoint(u16 index, float* outX, float* outZ) const;
    u16 gridCount() const;

    Phase mPhase = Phase::Idle;
    RejectReason mRejectReason = RejectReason::None;
    Vec3 mLink = {};
    float mNx = 0.0f;
    float mNz = 0.0f;
    float mTx = 0.0f;
    float mTz = 0.0f;
    float mScanDistanceM = 0.0f;
    float mCandidateDistanceM = 0.0f;
    float mCandidateX = 0.0f;
    float mCandidateZ = 0.0f;
    bool mSawWater = false;
    bool mHasIntentTarget = false;
    bool mVisibleWaterIntent = false;
    Vec3 mIntentTarget = {};
    float mIntentSurfaceDeltaM = 0.0f;
    RejectReason mRejectAfterVisibleIntent = RejectReason::None;
    Phase mPhaseAfterVisibleIntent = Phase::ValidateClearance;
    bool mSurfaceReferenceSet = false;
    float mSurfaceReferenceY = 0.0f;
    float mSurfaceSumY = 0.0f;
    float mMinDepthM = 0.0f;
    float mMaxDepthM = 0.0f;
    u16 mWaterGridIndex = 0;
    u16 mClearanceIndex = 0;
    u16 mWaterSamplesTested = 0;
    u8 mCandidateAttempts = 0;
    s32 mLastNativeCode = 0;
    float mFailedSampleDepthM = 0.0f;
    float mFailedSampleSurfaceDeltaM = 0.0f;
    u32 mIntentBodyId = 0xFFFFFFFFu;
    float mIntentHitY = 0.0f;
    u32 mObstructionBodyId = 0xFFFFFFFFu;
    float mObstructionHitY = 0.0f;
    ReadySite mReady = {};
};

const char* rejectReasonName(RejectReason reason);

} // namespace motherbase
