// Depth buffer reconstruction for VR reprojection
// Reconstructs world-space positions from depth for accurate reprojection

cbuffer ReprojectionParams : register(b0) {
    float4x4 currentViewProj;       // Current frame view-projection
    float4x4 previousViewProj;      // Previous frame view-projection
    float4x4 currentInverseViewProj;
    float4x4 previousInverseViewProj;
    float4 eyePosition;             // Current eye position
    float4 previousEyePosition;     // Previous eye position
    float2 jitterOffset;            // TAA jitter offset
    float2 motionScale;             // Motion vector scaling
};

Texture2D<float> depthTexture : register(t0);
Texture2D<float4> colorTexture : register(t1);
Texture2D<float2> motionTexture : register(t2);  // Optional motion vectors

SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Full-screen triangle
VS_OUTPUT VSMain(uint vertexId : SV_VertexID) {
    VS_OUTPUT output;
    output.texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.texcoord * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

// Reconstruct world position from depth and UV
float3 ReconstructWorldPosition(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;

    float4 worldPos = mul(clipPos, currentInverseViewProj);
    return worldPos.xyz / worldPos.w;
}

// Calculate reprojected UV for previous frame
float2 CalculateReprojectedUV(float3 worldPos) {
    float4 previousClip = mul(float4(worldPos, 1.0), previousViewProj);
    float2 previousNDC = previousClip.xy / previousClip.w;
    previousNDC.y = -previousNDC.y;
    return previousNDC * 0.5 + 0.5;
}

// Main reprojection shader - used for ASW/Motion Smoothing
float4 PSReproject(VS_OUTPUT input) : SV_TARGET {
    float depth = depthTexture.Sample(pointSampler, input.texcoord);

    // Skip sky/far plane
    if (depth >= 1.0) {
        return colorTexture.Sample(linearSampler, input.texcoord);
    }

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPosition(input.texcoord, depth);

    // Calculate where this pixel was in the previous frame
    float2 reprojectedUV = CalculateReprojectedUV(worldPos);

    // Check if reprojected UV is valid
    if (reprojectedUV.x < 0 || reprojectedUV.x > 1 ||
        reprojectedUV.y < 0 || reprojectedUV.y > 1) {
        // Out of bounds - use current frame
        return colorTexture.Sample(linearSampler, input.texcoord);
    }

    // Sample from previous position
    return colorTexture.Sample(linearSampler, reprojectedUV);
}

// Generate motion vectors from depth
float2 PSMotionVectors(VS_OUTPUT input) : SV_TARGET {
    float depth = depthTexture.Sample(pointSampler, input.texcoord);

    if (depth >= 1.0) {
        return float2(0, 0);  // No motion for sky
    }

    float3 worldPos = ReconstructWorldPosition(input.texcoord, depth);
    float2 reprojectedUV = CalculateReprojectedUV(worldPos);

    // Motion vector = current - previous
    float2 motion = input.texcoord - reprojectedUV;

    return motion * motionScale;
}

// Depth-aware upscale for reprojection artifacts
float4 PSDepthAwareUpscale(VS_OUTPUT input) : SV_TARGET {
    float centerDepth = depthTexture.Sample(pointSampler, input.texcoord);
    float4 centerColor = colorTexture.Sample(linearSampler, input.texcoord);

    // Sample neighbors
    float2 texelSize = float2(1.0 / 1920.0, 1.0 / 1080.0);  // TODO: Pass as parameter

    float4 colors[4];
    float depths[4];
    float2 offsets[4] = {
        float2(-texelSize.x, 0),
        float2(texelSize.x, 0),
        float2(0, -texelSize.y),
        float2(0, texelSize.y)
    };

    float totalWeight = 1.0;
    float4 result = centerColor;

    [unroll]
    for (int i = 0; i < 4; i++) {
        colors[i] = colorTexture.Sample(linearSampler, input.texcoord + offsets[i]);
        depths[i] = depthTexture.Sample(pointSampler, input.texcoord + offsets[i]);

        // Weight by depth similarity
        float depthDiff = abs(depths[i] - centerDepth);
        float weight = exp(-depthDiff * 100.0);

        result += colors[i] * weight;
        totalWeight += weight;
    }

    return result / totalWeight;
}
