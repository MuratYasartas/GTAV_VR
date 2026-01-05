// Stereo instancing shader for native stereo rendering mode
// Renders both eyes in a single draw call using instancing

cbuffer StereoViewData : register(b0) {
    float4x4 viewProjLeft;
    float4x4 viewProjRight;
    float4x4 viewLeft;
    float4x4 viewRight;
    float4 eyePositionLeft;
    float4 eyePositionRight;
    float4 stereoConfig;    // x=IPD, y=worldScale, z=convergence, w=renderMode
};

cbuffer ModelData : register(b1) {
    float4x4 world;
    float4x4 worldInverseTranspose;
};

struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    uint eyeIndex : SV_RenderTargetArrayIndex;
};

// Select view-projection matrix based on instance ID (eye index)
float4x4 GetViewProjection(uint eyeIndex) {
    return eyeIndex == 0 ? viewProjLeft : viewProjRight;
}

float4x4 GetView(uint eyeIndex) {
    return eyeIndex == 0 ? viewLeft : viewRight;
}

float4 GetEyePosition(uint eyeIndex) {
    return eyeIndex == 0 ? eyePositionLeft : eyePositionRight;
}

// Vertex shader with stereo instancing
VS_OUTPUT VSMain(VS_INPUT input, uint instanceId : SV_InstanceID) {
    VS_OUTPUT output;

    // Instance 0 = left eye, Instance 1 = right eye
    uint eyeIndex = instanceId & 1;

    // Transform to world space
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.worldPos = worldPos.xyz;

    // Transform to clip space using eye-specific view-projection
    float4x4 viewProj = GetViewProjection(eyeIndex);
    output.position = mul(worldPos, viewProj);

    // Transform normal
    output.normal = normalize(mul(float4(input.normal, 0.0), worldInverseTranspose).xyz);

    // Pass through other attributes
    output.texcoord = input.texcoord;
    output.color = input.color;

    // Set render target array index for stereo output
    output.eyeIndex = eyeIndex;

    return output;
}

// Geometry shader for single-pass stereo (alternative approach)
[maxvertexcount(6)]
void GSMain(triangle VS_OUTPUT input[3], inout TriangleStream<VS_OUTPUT> triStream) {
    // Emit for left eye
    [unroll]
    for (int i = 0; i < 3; i++) {
        VS_OUTPUT output = input[i];
        output.eyeIndex = 0;
        output.position = mul(float4(input[i].worldPos, 1.0), viewProjLeft);
        triStream.Append(output);
    }
    triStream.RestartStrip();

    // Emit for right eye
    [unroll]
    for (int j = 0; j < 3; j++) {
        VS_OUTPUT output = input[j];
        output.eyeIndex = 1;
        output.position = mul(float4(input[j].worldPos, 1.0), viewProjRight);
        triStream.Append(output);
    }
    triStream.RestartStrip();
}

// Standard pixel shader - works for both eyes
Texture2D diffuseTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    float4 diffuse = diffuseTexture.Sample(linearSampler, input.texcoord);
    return diffuse * input.color;
}

// Debug shader - visualize which eye is rendering
float4 PSDebugEye(VS_OUTPUT input) : SV_TARGET {
    // Left eye = cyan, Right eye = red
    float3 eyeColor = input.eyeIndex == 0 ?
        float3(0, 1, 1) :  // Cyan for left
        float3(1, 0, 0);   // Red for right

    float4 diffuse = diffuseTexture.Sample(linearSampler, input.texcoord);

    // Tint with eye color
    return float4(diffuse.rgb * eyeColor, diffuse.a);
}
