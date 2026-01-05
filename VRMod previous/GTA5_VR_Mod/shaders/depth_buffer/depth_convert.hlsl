// Depth buffer conversion and linearization shaders
// Used for VR reprojection (ASW 2.0, Motion Smoothing)

cbuffer DepthParams : register(b0) {
    float nearPlane;        // Camera near plane
    float farPlane;         // Camera far plane
    float2 depthRange;      // min/max depth values
    float4x4 inverseProj;   // Inverse projection matrix
    float4x4 eyeToWorld;    // Eye-space to world-space transform
};

Texture2D<float> depthTexture : register(t0);
SamplerState pointSampler : register(s0);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Full-screen quad vertex shader
VS_OUTPUT VSMain(uint vertexId : SV_VertexID) {
    VS_OUTPUT output;

    // Generate full-screen triangle
    output.texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.texcoord * float2(2, -2) + float2(-1, 1), 0, 1);

    return output;
}

// Convert hardware depth to linear depth (0 to 1)
float LinearizeDepth(float depth) {
    // Reverse-Z projection: depth = nearPlane / z
    // Standard projection: depth = (farPlane * (z - nearPlane)) / (z * (farPlane - nearPlane))

#ifdef REVERSE_Z
    return nearPlane / depth;
#else
    float z = depth * 2.0 - 1.0;  // NDC
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
#endif
}

// Convert linear depth to normalized (0-1) range for VR compositor
float NormalizeDepth(float linearDepth) {
    return saturate((linearDepth - nearPlane) / (farPlane - nearPlane));
}

// Reconstruct view-space position from depth
float3 ReconstructViewPosition(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;  // Flip Y for DirectX

    float4 viewPos = mul(clipPos, inverseProj);
    return viewPos.xyz / viewPos.w;
}

// Main depth copy shader - outputs linear depth for VR compositor
float4 PSCopyDepth(VS_OUTPUT input) : SV_TARGET {
    float depth = depthTexture.Sample(pointSampler, input.texcoord);
    float linearDepth = LinearizeDepth(depth);
    float normalizedDepth = NormalizeDepth(linearDepth);

    return float4(normalizedDepth, normalizedDepth, normalizedDepth, 1.0);
}

// Output raw depth (for debugging/visualization)
float4 PSRawDepth(VS_OUTPUT input) : SV_TARGET {
    float depth = depthTexture.Sample(pointSampler, input.texcoord);
    return float4(depth, depth, depth, 1.0);
}

// Output linear depth visualization
float4 PSVisualizeDepth(VS_OUTPUT input) : SV_TARGET {
    float depth = depthTexture.Sample(pointSampler, input.texcoord);
    float linearDepth = LinearizeDepth(depth);

    // Map to visible range
    float visualDepth = saturate(linearDepth / farPlane);

    // Color gradient: near=green, far=red
    float3 color = lerp(float3(0, 1, 0), float3(1, 0, 0), visualDepth);

    return float4(color, 1.0);
}
