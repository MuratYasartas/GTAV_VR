cbuffer CursorParams : register(b0)
{
    float4x4 u_world;
    float4x4 u_view;
    float4x4 u_proj;
};

struct VS_INPUT
{
    float4 pos : POSITION;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(input.pos, u_world);
    output.pos = mul(output.pos, u_view);
    output.pos = mul(output.pos, u_proj);
    return output;
}
