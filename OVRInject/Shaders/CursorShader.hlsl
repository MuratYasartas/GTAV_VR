// A simple shader that renders a colored quad
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
};

VS_OUTPUT main(float4 pos : POSITION)
{
    VS_OUTPUT output;
    output.pos = pos;
    return output;
}

float4 main() : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0); // White
}
