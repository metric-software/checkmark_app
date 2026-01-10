cbuffer cbMatrices : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
};

cbuffer cbLighting : register(b1)
{
    float4 lightDirection; // Directional light direction
    float4 lightColor;     // Light color
};

struct VS_INPUT
{
    float3 pos   : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL; // New normal attribute
};

struct VS_OUTPUT
{
    float4 pos     : SV_POSITION;
    float4 color   : COLOR;
    float3 normal  : NORMAL; // Pass normal to pixel shader
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0f), world);
    float4 viewPos  = mul(worldPos, view);
    output.pos      = mul(viewPos, proj);
    output.color    = input.color;
    // Transform normal to world space
    float3 worldNormal = normalize(mul(input.normal, (float3x3)world));
    output.normal    = worldNormal;
    return output;
}
