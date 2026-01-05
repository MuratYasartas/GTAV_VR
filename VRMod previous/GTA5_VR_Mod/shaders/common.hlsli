// Common shader utilities for GTA5 VR Mod
// Include this in other shaders for shared functionality

#ifndef GTA5VR_COMMON_HLSLI
#define GTA5VR_COMMON_HLSLI

// Math constants
#define PI 3.14159265359
#define TWO_PI 6.28318530718
#define HALF_PI 1.57079632679
#define EPSILON 0.0001

// Eye indices
#define EYE_LEFT 0
#define EYE_RIGHT 1

// Render modes
#define RENDER_MODE_NATIVE_STEREO 0
#define RENDER_MODE_SEQUENTIAL 1
#define RENDER_MODE_ALTERNATING 2

//-----------------------------------------------------------------------------
// Matrix utilities
//-----------------------------------------------------------------------------

// Extract camera position from view matrix
float3 GetCameraPosition(float4x4 viewMatrix) {
    return float3(
        -dot(viewMatrix[0].xyz, viewMatrix[3].xyz),
        -dot(viewMatrix[1].xyz, viewMatrix[3].xyz),
        -dot(viewMatrix[2].xyz, viewMatrix[3].xyz)
    );
}

// Create rotation matrix around Y axis
float4x4 RotationY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return float4x4(
        c, 0, s, 0,
        0, 1, 0, 0,
        -s, 0, c, 0,
        0, 0, 0, 1
    );
}

//-----------------------------------------------------------------------------
// Color utilities
//-----------------------------------------------------------------------------

// Convert linear to sRGB
float3 LinearToSRGB(float3 color) {
    float3 srgb;
    srgb.x = color.x <= 0.0031308 ? 12.92 * color.x : 1.055 * pow(color.x, 1.0 / 2.4) - 0.055;
    srgb.y = color.y <= 0.0031308 ? 12.92 * color.y : 1.055 * pow(color.y, 1.0 / 2.4) - 0.055;
    srgb.z = color.z <= 0.0031308 ? 12.92 * color.z : 1.055 * pow(color.z, 1.0 / 2.4) - 0.055;
    return srgb;
}

// Convert sRGB to linear
float3 SRGBToLinear(float3 color) {
    float3 linear;
    linear.x = color.x <= 0.04045 ? color.x / 12.92 : pow((color.x + 0.055) / 1.055, 2.4);
    linear.y = color.y <= 0.04045 ? color.y / 12.92 : pow((color.y + 0.055) / 1.055, 2.4);
    linear.z = color.z <= 0.04045 ? color.z / 12.92 : pow((color.z + 0.055) / 1.055, 2.4);
    return linear;
}

//-----------------------------------------------------------------------------
// Depth utilities
//-----------------------------------------------------------------------------

// Linearize depth (reverse-Z)
float LinearizeDepthReverseZ(float depth, float nearPlane, float farPlane) {
    return nearPlane * farPlane / (farPlane + depth * (nearPlane - farPlane));
}

// Linearize depth (standard)
float LinearizeDepthStandard(float depth, float nearPlane, float farPlane) {
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - (2.0 * depth - 1.0) * (farPlane - nearPlane));
}

//-----------------------------------------------------------------------------
// VR comfort utilities
//-----------------------------------------------------------------------------

// Calculate vignette for comfort
float CalculateComfortVignette(float2 uv, float intensity) {
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center) * 2.0;
    return 1.0 - saturate(pow(dist, 2.0) * intensity);
}

// Smooth step for comfort transitions
float SmoothVignetteEdge(float x, float edge0, float edge1) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

//-----------------------------------------------------------------------------
// Full-screen quad generation
//-----------------------------------------------------------------------------

// Generate full-screen triangle from vertex ID
void GenerateFullScreenTriangle(uint vertexId, out float4 position, out float2 texcoord) {
    texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

//-----------------------------------------------------------------------------
// Stereo utilities
//-----------------------------------------------------------------------------

// Apply IPD offset to position
float3 ApplyIPDOffset(float3 position, float ipd, int eyeIndex) {
    float offset = ipd * 0.5 * (eyeIndex == EYE_LEFT ? -1.0 : 1.0);
    return position + float3(offset, 0, 0);
}

// Get eye-specific UV for texture array
float3 GetStereoUV(float2 uv, uint eyeIndex) {
    return float3(uv, float(eyeIndex));
}

#endif // GTA5VR_COMMON_HLSLI
