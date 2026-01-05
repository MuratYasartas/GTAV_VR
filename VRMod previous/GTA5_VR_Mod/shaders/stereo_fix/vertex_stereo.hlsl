// Stereo correction for vertex shaders
// Inject eye-specific offset into world-view-projection matrix

cbuffer StereoParams : register(b13) {
    float4 stereoParams;  // x=separation, y=convergence, z=eye(-1/+1), w=enabled
};

cbuffer ViewProjection : register(b0) {
    float4x4 viewProj;
    float4x4 viewProjLeft;
    float4x4 viewProjRight;
};

// Apply stereo separation based on depth
float4 ApplyStereoCorrection(float4 position) {
    if (stereoParams.w > 0.5) {
        float depth = position.w;
        float separation = stereoParams.x * stereoParams.z;
        float convergence = stereoParams.y;

        position.x += separation * (depth - convergence);
    }
    return position;
}

// Get eye-specific view-projection matrix
float4x4 GetEyeViewProjection() {
    if (stereoParams.z < 0) {
        return viewProjLeft;
    }
    return viewProjRight;
}

// Main stereo vertex transformation
float4 TransformStereo(float4 worldPos) {
    float4x4 eyeVP = GetEyeViewProjection();
    float4 clipPos = mul(worldPos, eyeVP);
    return ApplyStereoCorrection(clipPos);
}
