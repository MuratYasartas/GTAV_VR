cbuffer StereoParams : register(b0)
{
    float u_ipd_offset;
    float3 u_padding;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD) : SV_Target
{
    // Apply a non-linear shift based on the x-coordinate
    float shift = u_ipd_offset * (1.0 - (tex.x - 0.5) * (tex.x - 0.5) * 4.0);
    
    // Sample the texture with the shifted coordinate
    return g_texture.Sample(g_sampler, float2(tex.x - shift, tex.y));
}
