// Project game HUD onto a curved surface in VR space

cbuffer HUDParams : register(b0) {
    float4x4 viewProj;
    float distance;
    float scale;
    float curvature;
    float opacity;
};

Texture2D hudTexture : register(t0);
SamplerState linearSampler : register(s0);

struct VS_INPUT {
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

PS_INPUT VSMain(VS_INPUT input) {
    PS_INPUT output;

    // Apply scale
    float3 pos = input.position * scale;

    // Apply curvature for cylindrical projection
    if (curvature > 0) {
        float angle = pos.x * curvature;
        pos.z = distance + (1.0 - cos(angle)) * distance * 0.5;
        pos.x = sin(angle) * distance;
    } else {
        pos.z = distance;
    }

    output.position = mul(float4(pos, 1.0), viewProj);
    output.texcoord = input.texcoord;
    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET {
    float4 color = hudTexture.Sample(linearSampler, input.texcoord);
    color.a *= opacity;

    // Discard fully transparent pixels
    clip(color.a - 0.01);

    return color;
}
