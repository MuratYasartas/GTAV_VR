#include "TestFramework.hpp"

#include "../OVRInject/VR/SharedSettings.hpp"

using namespace OVRInject::VR;

TEST(WorldScale_IsPhysicalAndInverse) {
    CHECK_NEAR(ComputeTrackingMetersToGameScale(1.0f), 1.0f, 1e-6f);
    CHECK_NEAR(ComputeTrackingMetersToGameScale(2.0f), 0.5f, 1e-6f);
    CHECK_NEAR(ComputeTrackingMetersToGameScale(0.5f), 2.0f, 1e-6f);
}

TEST(WorldScale_ClampsInvalidValues) {
    CHECK_NEAR(ComputeTrackingMetersToGameScale(0.0f), 2.0f, 1e-6f);
    CHECK_NEAR(ComputeTrackingMetersToGameScale(100.0f), 1.0f / 3.0f, 1e-6f);
    CHECK_NEAR(ComputeTrackingMetersToGameScale(NAN), 1.0f, 1e-6f);
}

TEST(RenderScale_RequestedThreeIsNotSilentlyClampedToTwo) {
    CHECK_NEAR(ClampRequestedRenderScale(3.0f), 3.0f, 1e-6f);
    CHECK_NEAR(ClampRequestedRenderScale(4.0f), 3.0f, 1e-6f);
}

TEST(RenderScale_PimaxSizedTargetHonorsSafetyBudget) {
    float effective = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    ComputeSafeRenderSize(5424, 5356, 3.0f, effective, width, height);
    CHECK(effective > 1.0f);
    CHECK(effective < 2.0f);
    CHECK(width <= kMaxEyeTextureDimension);
    CHECK(height <= kMaxEyeTextureDimension);
    CHECK(static_cast<uint64_t>(width) * height <=
          kMaxEyeTexturePixels + static_cast<uint64_t>(width + height));
}
