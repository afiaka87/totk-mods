// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "MotherBase.hpp"

namespace motherbase {

// Covers the 5x5 basecamp plus margin.
const Config kMvpConfig = {
    .scanStartM = 4.0f,
    .scanStepM = 2.0f,
    .scanMaxM = 48.0f,
    .castOutM = 7.0f,
    .minAnchorDistanceM = 16.0f,
    .candidateAdvanceM = 4.0f,
    .maxAnchorDistanceM = 64.0f,
    .minDepthM = 3.0f,
    .surfaceToleranceM = 0.35f,
    .deckClearanceM = 0.30f,
    .footprintHalfWidthM = 22.0f,
    .footprintBackM = 6.0f,
    .footprintFrontM = 30.0f,
    .clearanceTopM = 8.0f,
    .clearanceBottomInsetM = 0.25f,
    // Root-position eye proxy and visibility guard.
    .intentEyeHeightM = 1.55f,
    .intentOriginForwardM = 0.75f,
    .intentTargetAboveSurfaceM = 0.30f,
    .maxSurfaceBelowLinkM = 40.0f,
    .maxSurfaceAboveLinkM = 12.0f,
    .gridColumns = 11,
    .gridRows = 9,
};

namespace {

inline float absf(float value) { return value < 0.0f ? -value : value; }

inline float sqrtfPositive(float value) {
    return value > 0.0f ? __builtin_sqrtf(value) : 0.0f;
}

} // namespace

void Placement::reset() {
    *this = Placement{};
}

void Placement::begin(const Vec3& linkPosition, float faceX, float faceZ) {
    reset();
    mLink = linkPosition;
    const float length = sqrtfPositive(faceX * faceX + faceZ * faceZ);
    if (length < 0.0001f) {
        reject(RejectReason::InvalidFacing);
        return;
    }
    mNx = faceX / length;
    mNz = faceZ / length;
    mTx = mNz;
    mTz = -mNx;
    mScanDistanceM = kMvpConfig.scanStartM;
    mPhase = Phase::ScanAhead;
}

u16 Placement::gridCount() const {
    return static_cast<u16>(static_cast<u16>(kMvpConfig.gridColumns)
                          * static_cast<u16>(kMvpConfig.gridRows));
}

void Placement::reject(RejectReason reason) {
    mRejectReason = reason;
    mPhase = Phase::Rejected;
}

void Placement::rememberIntentTarget(float x, float surfaceY, float z) {
    mHasIntentTarget = true;
    mIntentTarget = {x, surfaceY, z};
    mIntentSurfaceDeltaM = absf(surfaceY - mLink.y);
}

void Placement::beginIntentValidation(RejectReason rejectAfterVisibleIntent, Phase phaseAfterVisibleIntent) {
    mRejectAfterVisibleIntent = rejectAfterVisibleIntent;
    mPhaseAfterVisibleIntent = phaseAfterVisibleIntent;
    if (!mHasIntentTarget) {
        reject(rejectAfterVisibleIntent);
        return;
    }
    const float surfaceBelowLinkM = mLink.y - mIntentTarget.y;
    mIntentSurfaceDeltaM = absf(surfaceBelowLinkM);
    if (surfaceBelowLinkM > kMvpConfig.maxSurfaceBelowLinkM
        || surfaceBelowLinkM < -kMvpConfig.maxSurfaceAboveLinkM) {
        reject(RejectReason::WaterOutsideHeightLimit);
        return;
    }
    mPhase = Phase::ValidateIntent;
}

void Placement::finishWaterFailure(RejectReason reason) {
    if (mVisibleWaterIntent) {
        reject(reason);
        return;
    }
    if (mHasIntentTarget) {
        beginIntentValidation(reason, Phase::Rejected);
        return;
    }
    reject(reason);
}

void Placement::startCandidate(float anchorDistanceM) {
    mCandidateDistanceM = anchorDistanceM;
    mCandidateX = mLink.x + mNx * anchorDistanceM;
    mCandidateZ = mLink.z + mNz * anchorDistanceM;
    mSurfaceReferenceSet = false;
    mSurfaceReferenceY = 0.0f;
    mSurfaceSumY = 0.0f;
    mMinDepthM = 0.0f;
    mMaxDepthM = 0.0f;
    mWaterGridIndex = 0;
    mPhase = Phase::ValidateFootprint;
    mCandidateAttempts++;
}

void Placement::advanceCandidateOrReject(RejectReason reason) {
    const float nextDistance = mCandidateDistanceM + kMvpConfig.candidateAdvanceM;
    if (nextDistance <= kMvpConfig.maxAnchorDistanceM) {
        startCandidate(nextDistance);
        return;
    }
    finishWaterFailure(reason);
}

void Placement::gridPoint(u16 index, float* outX, float* outZ) const {
    const u16 columns = kMvpConfig.gridColumns;
    const u16 rows = kMvpConfig.gridRows;
    const u16 column = static_cast<u16>(index % columns);
    const u16 row = static_cast<u16>(index / columns);
    const float columnT = columns > 1 ? static_cast<float>(column) / static_cast<float>(columns - 1) : 0.5f;
    const float rowT = rows > 1 ? static_cast<float>(row) / static_cast<float>(rows - 1) : 0.5f;
    const float t = -kMvpConfig.footprintHalfWidthM
                  + (2.0f * kMvpConfig.footprintHalfWidthM) * columnT;
    const float n = -kMvpConfig.footprintBackM
                  + (kMvpConfig.footprintBackM + kMvpConfig.footprintFrontM) * rowT;
    *outX = mCandidateX + mTx * t + mNx * n;
    *outZ = mCandidateZ + mTz * t + mNz * n;
}

void Placement::advanceWater(WaterQueryFn query, void* queryContext, u32 budget) {
    if (query == nullptr || budget == 0) { return; }

    while (budget-- > 0) {
        if (mPhase == Phase::ScanAhead) {
            WaterSample sample = {};
            const float x = mLink.x + mNx * mScanDistanceM;
            const float z = mLink.z + mNz * mScanDistanceM;
            if (!query(queryContext, x, z, &sample)) {
                finishWaterFailure(RejectReason::QueryUnavailable);
                return;
            }
            mWaterSamplesTested++;
            mLastNativeCode = sample.nativeCode;
            if (sample.hasWater) {
                mSawWater = true;
                // Keep the farthest centerline water sample; a valid footprint replaces it.
                rememberIntentTarget(x, sample.surfaceY, z);
                if (sample.depth >= kMvpConfig.minDepthM) {
                    float anchorDistance = mScanDistanceM + kMvpConfig.castOutM;
                    if (anchorDistance < kMvpConfig.minAnchorDistanceM) {
                        anchorDistance = kMvpConfig.minAnchorDistanceM;
                    }
                    startCandidate(anchorDistance);
                    // Prove the cast-out anchor visible before the 99-point grid.
                    rememberIntentTarget(mCandidateX, sample.surfaceY, mCandidateZ);
                    beginIntentValidation(RejectReason::None, Phase::ValidateFootprint);
                    return;
                }
                mFailedSampleDepthM = sample.depth;
            }
            mScanDistanceM += kMvpConfig.scanStepM;
            if (mScanDistanceM > kMvpConfig.scanMaxM) {
                if (mSawWater) { finishWaterFailure(RejectReason::WaterTooShallow); }
                else { reject(RejectReason::NoWaterAhead); }
                return;
            }
            continue;
        }

        if (mPhase == Phase::ValidateFootprint) {
            float x = 0.0f;
            float z = 0.0f;
            gridPoint(mWaterGridIndex, &x, &z);
            WaterSample sample = {};
            if (!query(queryContext, x, z, &sample)) {
                finishWaterFailure(RejectReason::QueryUnavailable);
                return;
            }
            mWaterSamplesTested++;
            mLastNativeCode = sample.nativeCode;
            if (!sample.hasWater) {
                advanceCandidateOrReject(RejectReason::FootprintNotWater);
                continue;
            }
            mSawWater = true;
            if (sample.depth < kMvpConfig.minDepthM) {
                mFailedSampleDepthM = sample.depth;
                advanceCandidateOrReject(RejectReason::WaterTooShallow);
                continue;
            }
            if (!mSurfaceReferenceSet) {
                mSurfaceReferenceSet = true;
                mSurfaceReferenceY = sample.surfaceY;
                mMinDepthM = sample.depth;
                mMaxDepthM = sample.depth;
            } else {
                const float delta = absf(sample.surfaceY - mSurfaceReferenceY);
                if (delta > kMvpConfig.surfaceToleranceM) {
                    mFailedSampleSurfaceDeltaM = delta;
                    advanceCandidateOrReject(RejectReason::SurfaceUneven);
                    continue;
                }
                if (sample.depth < mMinDepthM) { mMinDepthM = sample.depth; }
                if (sample.depth > mMaxDepthM) { mMaxDepthM = sample.depth; }
            }
            mSurfaceSumY += sample.surfaceY;
            mWaterGridIndex++;
            if (mWaterGridIndex < gridCount()) { continue; }

            const float averageSurfaceY = mSurfaceSumY / static_cast<float>(gridCount());
            mReady.anchor = {mCandidateX, averageSurfaceY + kMvpConfig.deckClearanceM, mCandidateZ};
            mReady.nx = mNx;
            mReady.nz = mNz;
            mReady.surfaceY = averageSurfaceY;
            mReady.minDepthM = mMinDepthM;
            mReady.maxDepthM = mMaxDepthM;
            mReady.anchorDistanceM = mCandidateDistanceM;
            rememberIntentTarget(mCandidateX, averageSurfaceY, mCandidateZ);
            mClearanceIndex = 0;
            if (mVisibleWaterIntent) { mPhase = Phase::ValidateClearance; }
            else { beginIntentValidation(RejectReason::None, Phase::ValidateClearance); }
            return;
        }

        return;
    }
}

bool Placement::wantsIntentRay() const {
    return mPhase == Phase::ValidateIntent && mHasIntentTarget;
}

IntentRay Placement::intentRay() const {
    IntentRay ray = {};
    ray.from = {
        mLink.x + mNx * kMvpConfig.intentOriginForwardM,
        mLink.y + kMvpConfig.intentEyeHeightM,
        mLink.z + mNz * kMvpConfig.intentOriginForwardM,
    };
    ray.to = {
        mIntentTarget.x,
        mIntentTarget.y + kMvpConfig.intentTargetAboveSurfaceM,
        mIntentTarget.z,
    };
    return ray;
}

void Placement::acceptIntentResult(bool timedOut, bool hit, float hitY, u32 bodyId) {
    if (mPhase != Phase::ValidateIntent) { return; }
    if (timedOut) {
        reject(RejectReason::VisibilityTimedOut);
        return;
    }
    if (hit) {
        mIntentBodyId = bodyId;
        mIntentHitY = hitY;
        reject(RejectReason::WaterNotVisible);
        return;
    }
    mVisibleWaterIntent = true;
    if (mRejectAfterVisibleIntent != RejectReason::None) {
        reject(mRejectAfterVisibleIntent);
        return;
    }
    mPhase = mPhaseAfterVisibleIntent;
}

bool Placement::wantsClearanceRay() const {
    return mPhase == Phase::ValidateClearance && mClearanceIndex < gridCount();
}

ClearanceRay Placement::clearanceRay() const {
    ClearanceRay ray = {};
    ray.sampleIndex = mClearanceIndex;
    float x = 0.0f;
    float z = 0.0f;
    gridPoint(mClearanceIndex, &x, &z);
    ray.from = {x, mReady.surfaceY + kMvpConfig.clearanceTopM, z};
    ray.to = {x, mReady.surfaceY - kMvpConfig.minDepthM + kMvpConfig.clearanceBottomInsetM, z};
    return ray;
}

void Placement::acceptClearanceResult(bool timedOut, bool hit, float hitY, u32 bodyId) {
    if (mPhase != Phase::ValidateClearance) { return; }
    if (timedOut) {
        reject(RejectReason::ClearanceTimedOut);
        return;
    }
    if (hit) {
        mObstructionBodyId = bodyId;
        mObstructionHitY = hitY;
        reject(RejectReason::Obstructed);
        return;
    }
    mClearanceIndex++;
    if (mClearanceIndex >= gridCount()) { mPhase = Phase::Ready; }
}

const char* rejectReasonName(RejectReason reason) {
    switch (reason) {
    case RejectReason::None: return "none";
    case RejectReason::NoWaterAhead: return "no water ahead";
    case RejectReason::WaterTooShallow: return "water too shallow for the full Mother Base";
    case RejectReason::FootprintNotWater: return "Mother Base footprint reaches land or dry terrain";
    case RejectReason::SurfaceUneven: return "water surface is too uneven";
    case RejectReason::Obstructed: return "Mother Base clearance contains an obstruction";
    case RejectReason::ClearanceTimedOut: return "Mother Base obstruction check timed out";
    case RejectReason::WaterNotVisible: return "queried water is hidden behind solid terrain or an obstruction";
    case RejectReason::WaterOutsideHeightLimit: return "queried water is outside the Mother Base height limit";
    case RejectReason::VisibilityTimedOut: return "Mother Base water-visibility check timed out";
    case RejectReason::QueryUnavailable: return "MainField water query unavailable";
    case RejectReason::InvalidFacing: return "Link facing vector is invalid";
    }
    return "unknown Mother Base rejection";
}

} // namespace motherbase
