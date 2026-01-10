cbuffer cbLighting : register(b1)
{
    float4 lightDirection;  // Directional light direction
    float4 lightColor;      // Light color
};

struct PS_INPUT
{
    float4 pos     : SV_POSITION;
    float4 color   : COLOR;
    float3 normal  : NORMAL; // Received normal from vertex shader
};

float4 main(PS_INPUT input) : SV_Target
{
    // Basic Lambertian diffuse lighting
    float3 normal = normalize(input.normal);
    float3 lightDir = normalize(-lightDirection.xyz); // Assuming lightDirection is the direction light is coming from
    float diffuse = saturate(dot(normal, lightDir));

    // Fake shadow: darker color if diffuse is low
    float shadowFactor = diffuse < 0.3f ? 0.5f : 1.0f;

    // Combine vertex color with light color and diffuse factor
    float4 finalColor = input.color * lightColor * diffuse * shadowFactor;

    // Add ambient term
    float ambient = 0.2f;
    finalColor += input.color * ambient;

    return finalColor;
}
