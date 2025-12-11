// gpu_test.cpp

#include "gpu_test.h"
#include "../logging/Logger.h"

#include <chrono>
#include <iostream>
#include <random>

#include <d3dcompiler.h>

#include "diagnostic/DiagnosticDataStore.h"

using namespace DirectX;

// --------------------------------------------------------------------------------------
// Simple vertex/pixel shaders with lighting
// --------------------------------------------------------------------------------------
static const char* vertexShaderCode = R"(
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
)";

static const char* pixelShaderCode = R"(
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
)";

// --------------------------------------------------------------------------------------

GPUTest::GPUTest()
    : device(nullptr), context(nullptr), swapChain(nullptr),
      renderTarget(nullptr), depthStencilBuffer(nullptr),
      depthStencilView(nullptr), envVertexBuffer(nullptr),
      envIndexBuffer(nullptr), gridVertexBuffer(nullptr),
      sphereVertexBuffer(nullptr), sphereIndexBuffer(nullptr),
      matricesCB(nullptr), lightingCB(nullptr), vertexShader(nullptr),
      pixelShader(nullptr), inputLayout(nullptr), wireframeRS(nullptr),
      sphereIndexCount(0), gridLineCount(0) {}

GPUTest::~GPUTest() {
  if (wireframeRS) wireframeRS->Release();
  if (matricesCB) matricesCB->Release();
  if (lightingCB) lightingCB->Release();  // Released

  if (sphereIndexBuffer) sphereIndexBuffer->Release();
  if (sphereVertexBuffer) sphereVertexBuffer->Release();
  if (gridVertexBuffer) gridVertexBuffer->Release();
  if (envIndexBuffer) envIndexBuffer->Release();
  if (envVertexBuffer) envVertexBuffer->Release();

  // if (shadowSRV)            shadowSRV->Release();    // Uncomment if shadow
  // mapping is added
  if (depthStencilView) depthStencilView->Release();
  if (depthStencilBuffer) depthStencilBuffer->Release();
  if (renderTarget) renderTarget->Release();
  if (context) context->Release();
  if (swapChain) swapChain->Release();
  if (device) device->Release();
}

// --------------------------------------------------------------------------------------
bool GPUTest::Initialize(HWND hwnd) {
  // Swap chain description - unchanged
  DXGI_SWAP_CHAIN_DESC scd = {};
  scd.BufferCount = 1;
  scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.OutputWindow = hwnd;
  scd.SampleDesc.Count = 1;
  scd.Windowed = TRUE;

  // Try different driver types to handle various system configurations
  D3D_DRIVER_TYPE driverTypes[] = {
    D3D_DRIVER_TYPE_HARDWARE,
    D3D_DRIVER_TYPE_WARP,      // Software fallback
    D3D_DRIVER_TYPE_REFERENCE  // Last resort
  };
  UINT numDriverTypes = ARRAYSIZE(driverTypes);

  // Specify feature levels from highest to lowest for compatibility
  D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1};
  D3D_FEATURE_LEVEL createdFeatureLevel;

  HRESULT hr = E_FAIL;

  UINT createDeviceFlags = 0;
#ifdef _DEBUG
  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  for (UINT i = 0; i < numDriverTypes; i++) {
    hr = D3D11CreateDeviceAndSwapChain(
      nullptr,                   // adapter
      driverTypes[i],            // driver type
      nullptr,                   // software
      createDeviceFlags,         // flags (using debug in debug builds)
      featureLevels,             // feature levels - ADDED
      ARRAYSIZE(featureLevels),  // number of feature levels - ADDED
      D3D11_SDK_VERSION, &scd, &swapChain, &device,
      &createdFeatureLevel,  // will receive created feature level - ADDED
      &context);

    if (SUCCEEDED(hr)) {
      LOG_INFO << "Created DirectX device with driver type: "
                << (driverTypes[i] == D3D_DRIVER_TYPE_HARDWARE
                      ? "Hardware"
                      : (driverTypes[i] == D3D_DRIVER_TYPE_WARP
                           ? "WARP (Software)"
                           : "Reference (Software)"))
                << " and feature level: ";

      // Print the feature level that was created
      switch (createdFeatureLevel) {
        case D3D_FEATURE_LEVEL_11_0:
          LOG_INFO << "11.0";
          break;
        case D3D_FEATURE_LEVEL_10_1:
          LOG_INFO << "10.1";
          break;
        case D3D_FEATURE_LEVEL_10_0:
          LOG_INFO << "10.0";
          break;
        case D3D_FEATURE_LEVEL_9_3:
          LOG_INFO << "9.3";
          break;
        case D3D_FEATURE_LEVEL_9_2:
          LOG_INFO << "9.2";
          break;
        case D3D_FEATURE_LEVEL_9_1:
          LOG_INFO << "9.1";
          break;
        default:
          LOG_WARN << "Unknown";
      }
      // Removed std::endl - Logger handles line endings
      break;
    }
  }

  if (FAILED(hr)) {
    // Convert HRESULT to readable error
    char errorMsg[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg,
                   sizeof(errorMsg), nullptr);
    LOG_ERROR << "Failed to create device & swap chain: " << errorMsg;
    return false;
  }

  // Create render target
  ID3D11Texture2D* backBuffer = nullptr;
  hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to get back buffer";
    return false;
  }
  hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTarget);
  backBuffer->Release();
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create render target view";
    return false;
  }

  // Create depth buffer + view
  if (!CreateDepthBuffer(800, 600)) {
    LOG_ERROR << "Failed to create depth buffer.";
    return false;
  }

  // Bind both
  context->OMSetRenderTargets(1, &renderTarget, depthStencilView);

  // Viewport
  D3D11_VIEWPORT vp = {};
  vp.Width = 800.0f;
  vp.Height = 600.0f;
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  context->RSSetViewports(1, &vp);

  // Create geometry + shaders
  if (!Create3DEnvironment()) return false;
  if (!CreateSphereGeometry()) return false;
  if (!CreateShaders()) return false;

  // Initialize camera
  XMVECTOR eye = XMVectorSet(0, 2, -8, 0);
  XMVECTOR at = XMVectorSet(0, 0, 0, 0);
  XMVECTOR up = XMVectorSet(0, 1, 0, 0);
  viewMatrix = XMMatrixLookAtLH(eye, at, up);

  float aspect = 800.0f / 600.0f;
  projMatrix =
    XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), aspect, 0.1f, 50.0f);

  // Room bounds
  minX = -4;
  maxX = 4;
  minY = 0;
  maxY = 4;
  minZ = -4;
  maxZ = 4;

  // Create some balls with more varied colors
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> posDist(-2.0f, 2.0f);
  std::uniform_real_distribution<float> velDist(-1.0f, 1.0f);

  // More vibrant color ranges
  std::uniform_real_distribution<float> redDist(0.5f, 1.0f);  // Brighter reds
  std::uniform_real_distribution<float> greenDist(
    0.3f, 0.9f);  // Mid to bright greens
  std::uniform_real_distribution<float> blueDist(0.4f,
                                                 1.0f);  // Mid to bright blues

  balls.reserve(NUM_BALLS);
  for (int i = 0; i < NUM_BALLS; i++) {
    Ball b;
    b.position = XMFLOAT3(posDist(gen), 2.0f, posDist(gen));
    b.velocity = XMFLOAT3(velDist(gen), velDist(gen), velDist(gen));
    // Create more varied and vibrant colors
    b.color = XMFLOAT4(redDist(gen), greenDist(gen), blueDist(gen), 1.0f);
    balls.push_back(b);
  }

  // Optional wireframe for sphere overlay
  D3D11_RASTERIZER_DESC wireDesc = {};
  wireDesc.FillMode = D3D11_FILL_WIREFRAME;
  wireDesc.CullMode = D3D11_CULL_BACK;
  hr = device->CreateRasterizerState(&wireDesc, &wireframeRS);
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create wireframe rasterizer.";
    return false;
  }

  // Initialize lighting parameters
  CB_Lighting lighting;
  // Example: Light coming from above and to the front
  lighting.lightDirection =
    XMFLOAT4(0.577f, -0.577f, 0.577f, 0.0f);  // Normalized direction
  lighting.lightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);  // White light

  // Update lighting constant buffer
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  HRESULT hr_map =
    context->Map(lightingCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (SUCCEEDED(hr_map)) {
    memcpy(mapped.pData, &lighting, sizeof(CB_Lighting));
    context->Unmap(lightingCB, 0);
  } else {
    LOG_ERROR << "Failed to map lighting constant buffer.";
    return false;
  }

  // Bind lighting constant buffer to pixel shader
  context->PSSetConstantBuffers(1, 1, &lightingCB);  // Register b1

  return true;
}

// --------------------------------------------------------------------------------------
bool GPUTest::CreateDepthBuffer(UINT width, UINT height) {
  // Create a depth-stencil texture
  D3D11_TEXTURE2D_DESC descDepth = {};
  descDepth.Width = width;
  descDepth.Height = height;
  descDepth.MipLevels = 1;
  descDepth.ArraySize = 1;
  descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  descDepth.SampleDesc.Count = 1;
  descDepth.SampleDesc.Quality = 0;
  descDepth.Usage = D3D11_USAGE_DEFAULT;
  descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  descDepth.CPUAccessFlags = 0;
  descDepth.MiscFlags = 0;

  HRESULT hr =
    device->CreateTexture2D(&descDepth, nullptr, &depthStencilBuffer);
  if (FAILED(hr)) {
    return false;
  }

  // Create the depth stencil view
  D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
  descDSV.Format = descDepth.Format;
  descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  descDSV.Texture2D.MipSlice = 0;

  hr = device->CreateDepthStencilView(depthStencilBuffer, &descDSV,
                                      &depthStencilView);
  if (FAILED(hr)) {
    return false;
  }
  return true;
}

// --------------------------------------------------------------------------------------
bool GPUTest::Create3DEnvironment() {
  // Basic “room” (5 planes) + lines on all surfaces
  // 1) Box geometry (gray walls/floor/ceiling)
  XMFLOAT4 grey(0.5f, 0.5f, 0.5f, 1.0f);

  // Floor, back wall, left, right, ceiling => 20 vertices with normals
  Vertex3D boxVerts[] = {
    // Floor (y=0) - Normal (0,1,0)
    {XMFLOAT3(-4, 0, -4), grey, XMFLOAT3(0, 1, 0)},
    {XMFLOAT3(-4, 0, 4), grey, XMFLOAT3(0, 1, 0)},
    {XMFLOAT3(4, 0, 4), grey, XMFLOAT3(0, 1, 0)},
    {XMFLOAT3(4, 0, -4), grey, XMFLOAT3(0, 1, 0)},

    // Back wall (z=4) - Normal (0,0,-1)
    {XMFLOAT3(-4, 0, 4), grey, XMFLOAT3(0, 0, -1)},
    {XMFLOAT3(-4, 4, 4), grey, XMFLOAT3(0, 0, -1)},
    {XMFLOAT3(4, 4, 4), grey, XMFLOAT3(0, 0, -1)},
    {XMFLOAT3(4, 0, 4), grey, XMFLOAT3(0, 0, -1)},

    // Left wall (x=-4) - Normal (1,0,0)
    {XMFLOAT3(-4, 0, -4), grey, XMFLOAT3(1, 0, 0)},
    {XMFLOAT3(-4, 4, -4), grey, XMFLOAT3(1, 0, 0)},
    {XMFLOAT3(-4, 4, 4), grey, XMFLOAT3(1, 0, 0)},
    {XMFLOAT3(-4, 0, 4), grey, XMFLOAT3(1, 0, 0)},

    // Right wall (x=4) - Normal (-1,0,0)
    {XMFLOAT3(4, 0, 4), grey, XMFLOAT3(-1, 0, 0)},
    {XMFLOAT3(4, 4, 4), grey, XMFLOAT3(-1, 0, 0)},
    {XMFLOAT3(4, 4, -4), grey, XMFLOAT3(-1, 0, 0)},
    {XMFLOAT3(4, 0, -4), grey, XMFLOAT3(-1, 0, 0)},

    // Ceiling (y=4) - Normal (0,-1,0)
    {XMFLOAT3(-4, 4, 4), grey, XMFLOAT3(0, -1, 0)},
    {XMFLOAT3(-4, 4, -4), grey, XMFLOAT3(0, -1, 0)},
    {XMFLOAT3(4, 4, -4), grey, XMFLOAT3(0, -1, 0)},
    {XMFLOAT3(4, 4, 4), grey, XMFLOAT3(0, -1, 0)},
  };

  WORD boxIndices[] = {// Floor
                       0, 1, 2, 0, 2, 3,
                       // Back wall
                       4, 5, 6, 4, 6, 7,
                       // Left wall
                       8, 9, 10, 8, 10, 11,
                       // Right wall
                       12, 13, 14, 12, 14, 15,
                       // Ceiling
                       16, 17, 18, 16, 18, 19};

  // Create VB/IB for box
  {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(boxVerts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = boxVerts;
    HRESULT hr = device->CreateBuffer(&bd, &initData, &envVertexBuffer);
    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create environment VB.";
      return false;
    }

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(boxIndices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;

    initData.pSysMem = boxIndices;
    hr = device->CreateBuffer(&bd, &initData, &envIndexBuffer);
    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create environment IB.";
      return false;
    }
  }

  // 2) Build line vertices for all surfaces
  //    We'll offset them slightly so they don't z-fight with the polygons
  std::vector<Vertex3D> gridLines;
  gridLines.reserve(600);  // just a guess
  XMFLOAT4 white(1, 1, 1, 1);

  // Helper lambdas to push back lines
  auto lineY = [&](float x1, float y1, float z1, float x2, float y2, float z2) {
    gridLines.push_back(
      {XMFLOAT3(x1, y1, z1), white,
       XMFLOAT3(0, 0, 0)});  // Normals for lines can be zero or any value
    gridLines.push_back({XMFLOAT3(x2, y2, z2), white, XMFLOAT3(0, 0, 0)});
  };

  // --------------- Floor (y=0 + small offset) & Ceiling (y=4 - small offset)
  // ---------------
  float floorY = 0.001f;    // slight offset above y=0
  float ceilingY = 3.999f;  // slight offset below y=4
  for (int i = -4; i <= 4; i++) {
    // lines parallel to X (varying Z)
    lineY(-4, floorY, float(i), 4, floorY, float(i));      // floor
    lineY(-4, ceilingY, float(i), 4, ceilingY, float(i));  // ceiling
    // lines parallel to Z (varying X)
    lineY(float(i), floorY, -4, float(i), floorY, 4);
    lineY(float(i), ceilingY, -4, float(i), ceilingY, 4);
  }

  // --------------- Walls ---------------
  // We'll do a small offset inward for the walls, so lines are visible:
  float leftX = -3.999f;
  float rightX = 3.999f;
  float backZ = 3.999f;
  float frontZ = -3.999f;  // even though we skip actual front wall, we can add
                           // lines if we want

  // Left (x=-4)
  for (int y = 0; y <= 4; y++) {
    lineY(leftX, (float)y, -4, leftX, (float)y, 4);
  }
  for (int z = -4; z <= 4; z++) {
    lineY(leftX, 0, (float)z, leftX, 4, (float)z);
  }

  // Right (x=4)
  for (int y = 0; y <= 4; y++) {
    lineY(rightX, (float)y, -4, rightX, (float)y, 4);
  }
  for (int z = -4; z <= 4; z++) {
    lineY(rightX, 0, (float)z, rightX, 4, (float)z);
  }

  // Back (z=4)
  for (int y = 0; y <= 4; y++) {
    lineY(-4, (float)y, backZ, 4, (float)y, backZ);
  }
  for (int x = -4; x <= 4; x++) {
    lineY((float)x, 0, backZ, (float)x, 4, backZ);
  }

  // (Optionally) front lines (z=-4) if you want them
  // for demonstration, let's add them:
  for (int y = 0; y <= 4; y++) {
    lineY(-4, (float)y, frontZ, 4, (float)y, frontZ);
  }
  for (int x = -4; x <= 4; x++) {
    lineY((float)x, 0, frontZ, (float)x, 4, frontZ);
  }

  // Create buffer
  gridLineCount = (UINT)gridLines.size();
  {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = (UINT)(gridLines.size() * sizeof(Vertex3D));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = gridLines.data();
    HRESULT hr = device->CreateBuffer(&bd, &srd, &gridVertexBuffer);
    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create grid VB.";
      return false;
    }
  }

  return true;
}

// --------------------------------------------------------------------------------------
bool GPUTest::CreateSphereGeometry() {
  // Approximate a sphere with lat/long
  const int numStacks = 16;  // Increased for higher detail
  const int numSlices = 32;  // Increased for higher detail

  std::vector<Vertex3D> verts;
  std::vector<WORD> indices;
  verts.reserve((numStacks + 1) * (numSlices + 1));

  XMFLOAT4 white(1, 1, 1, 1);

  for (int i = 0; i <= numStacks; i++) {
    float v = (float)i / (float)numStacks;  // 0..1
    float phi = XM_PI * v;                  // 0..pi

    for (int j = 0; j <= numSlices; j++) {
      float u = (float)j / (float)numSlices;
      float theta = 2.f * XM_PI * u;  // 0..2pi

      float x = sinf(phi) * cosf(theta);
      float y = cosf(phi);
      float z = sinf(phi) * sinf(theta);

      // Normal is the same as position normalized
      XMFLOAT3 normal(x, y, z);

      verts.push_back({XMFLOAT3(x, y, z), white, normal});
    }
  }

  int rowVerts = numSlices + 1;
  for (int i = 0; i < numStacks; i++) {
    for (int j = 0; j < numSlices; j++) {
      int i0 = i * rowVerts + j;
      int i1 = i * rowVerts + (j + 1);
      int i2 = (i + 1) * rowVerts + j;
      int i3 = (i + 1) * rowVerts + (j + 1);

      // two triangles
      indices.push_back((WORD)i0);
      indices.push_back((WORD)i2);
      indices.push_back((WORD)i1);

      indices.push_back((WORD)i1);
      indices.push_back((WORD)i2);
      indices.push_back((WORD)i3);
    }
  }
  sphereIndexCount = (UINT)indices.size();

  // VB
  {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = (UINT)(verts.size() * sizeof(Vertex3D));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = verts.data();

    HRESULT hr = device->CreateBuffer(&bd, &srd, &sphereVertexBuffer);
    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create sphere VB";
      return false;
    }
  }

  // IB
  {
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = (UINT)(indices.size() * sizeof(WORD));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = indices.data();

    HRESULT hr = device->CreateBuffer(&bd, &srd, &sphereIndexBuffer);
    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create sphere IB";
      return false;
    }
  }

  return true;
}

// --------------------------------------------------------------------------------------
bool GPUTest::CreateShaders() {
  // Compile & create VS
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* psBlob = nullptr;
  ID3DBlob* errBlob = nullptr;

  HRESULT hr =
    D3DCompile(vertexShaderCode, strlen(vertexShaderCode), nullptr, nullptr,
               nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
  if (FAILED(hr)) {
    if (errBlob) {
      LOG_ERROR << "Vertex shader error: " << (char*)errBlob->GetBufferPointer();
      errBlob->Release();
    }
    return false;
  }
  hr =
    device->CreateVertexShader(vsBlob->GetBufferPointer(),
                               vsBlob->GetBufferSize(), nullptr, &vertexShader);
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create VS.";
    vsBlob->Release();
    return false;
  }

  // Input layout
  D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
     D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28,
     D3D11_INPUT_PER_VERTEX_DATA, 0},  // Offset is 28 bytes
  };

  // **Corrected element count from 2 to 3**
  hr = device->CreateInputLayout(layoutDesc, 3,  // Changed from 2 to 3
                                 vsBlob->GetBufferPointer(),
                                 vsBlob->GetBufferSize(), &inputLayout);
  vsBlob->Release();
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create input layout.";
    return false;
  }

  // Compile & create PS
  hr = D3DCompile(pixelShaderCode, strlen(pixelShaderCode), nullptr, nullptr,
                  nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errBlob);
  if (FAILED(hr)) {
    if (errBlob) {
      LOG_ERROR << "Pixel shader error: " << (char*)errBlob->GetBufferPointer();
      errBlob->Release();
    }
    return false;
  }
  hr = device->CreatePixelShader(
    psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
  psBlob->Release();
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create PS.";
    return false;
  }

  // Constant buffer for world/view/proj
  D3D11_BUFFER_DESC cbd = {};
  cbd.Usage = D3D11_USAGE_DYNAMIC;
  cbd.ByteWidth = sizeof(CB_Matrices);
  cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device->CreateBuffer(&cbd, nullptr, &matricesCB);
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create constant buffer.";
    return false;
  }

  // Create constant buffer for lighting
  D3D11_BUFFER_DESC cbd_light = {};
  cbd_light.Usage = D3D11_USAGE_DYNAMIC;
  cbd_light.ByteWidth = sizeof(CB_Lighting);
  cbd_light.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbd_light.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = device->CreateBuffer(&cbd_light, nullptr, &lightingCB);
  if (FAILED(hr)) {
    LOG_ERROR << "Failed to create lighting constant buffer.";
    return false;
  }

  return true;
}

// --------------------------------------------------------------------------------------
void GPUTest::Update(float deltaTime) {
  // Basic bouncing in 3D
  for (auto& b : balls) {
    b.position.x += b.velocity.x * deltaTime;
    b.position.y += b.velocity.y * deltaTime;
    b.position.z += b.velocity.z * deltaTime;

    if (b.position.x < minX) {
      b.position.x = minX;
      b.velocity.x *= -1;
    }
    if (b.position.x > maxX) {
      b.position.x = maxX;
      b.velocity.x *= -1;
    }
    if (b.position.y < minY) {
      b.position.y = minY;
      b.velocity.y *= -1;
    }
    if (b.position.y > maxY) {
      b.position.y = maxY;
      b.velocity.y *= -1;
    }
    if (b.position.z < minZ) {
      b.position.z = minZ;
      b.velocity.z *= -1;
    }
    if (b.position.z > maxZ) {
      b.position.z = maxZ;
      b.velocity.z *= -1;
    }
  }
}

// --------------------------------------------------------------------------------------
void GPUTest::Render() {
  if (!device || !context || !swapChain) {
    return;
  }

  // Clear RT + Depth
  float clearColor[4] = {0.2f, 0.3f, 0.6f, 1.0f};
  context->ClearRenderTargetView(renderTarget, clearColor);
  context->ClearDepthStencilView(
    depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

  // Set pipeline
  context->IASetInputLayout(inputLayout);
  context->VSSetShader(vertexShader, nullptr, 0);
  context->PSSetShader(pixelShader, nullptr, 0);

  // Update matrices constant buffer
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  HRESULT hr_map =
    context->Map(matricesCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
  if (FAILED(hr_map)) return;
  CB_Matrices* cbData = (CB_Matrices*)mapped.pData;
  XMMATRIX world = XMMatrixIdentity();
  cbData->world = XMMatrixTranspose(world);
  cbData->view = XMMatrixTranspose(viewMatrix);
  cbData->proj = XMMatrixTranspose(projMatrix);
  context->Unmap(matricesCB, 0);
  context->VSSetConstantBuffers(0, 1, &matricesCB);

  // Bind lighting constant buffer to pixel shader
  context->PSSetConstantBuffers(1, 1, &lightingCB);  // Register b1

  // 1) Draw room geometry (triangle list)
  UINT stride = sizeof(Vertex3D);
  UINT offset = 0;
  context->IASetVertexBuffers(0, 1, &envVertexBuffer, &stride, &offset);
  context->IASetIndexBuffer(envIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->DrawIndexed(30, 0, 0);  // 5 planes, 2 triangles each => 30 indices

  // 2) Draw grid lines (linelist)
  context->IASetVertexBuffers(0, 1, &gridVertexBuffer, &stride, &offset);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  context->Draw(gridLineCount, 0);

  // 3) Draw spheres
  context->IASetVertexBuffers(0, 1, &sphereVertexBuffer, &stride, &offset);
  context->IASetIndexBuffer(sphereIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  for (auto& b : balls) {
    // Update world matrix for each ball
    hr_map = context->Map(matricesCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr_map)) continue;

    // Make balls smaller
    float scale = 0.2f;  // Changed from 0.5f to 0.2f
    XMMATRIX mWorld =
      XMMatrixScaling(scale, scale, scale) *
      XMMatrixTranslation(b.position.x, b.position.y, b.position.z);

    cbData = (CB_Matrices*)mapped.pData;
    cbData->world = XMMatrixTranspose(mWorld);
    cbData->view = XMMatrixTranspose(viewMatrix);
    cbData->proj = XMMatrixTranspose(projMatrix);
    context->Unmap(matricesCB, 0);

    // Draw the solid sphere
    context->DrawIndexed(sphereIndexCount, 0, 0);

    // Optional wireframe overlay
    context->RSSetState(wireframeRS);
    context->DrawIndexed(sphereIndexCount, 0, 0);
    context->RSSetState(nullptr);
  }

  swapChain->Present(0, 0);
}

// --------------------------------------------------------------------------------------
void GPUTest::RunTest() {
  // Don't run the test if device or context is null (initialization failed)
  if (!device || !context || !swapChain) {
    LOG_ERROR << "Cannot run GPU test - DirectX initialization failed";

    // Still update metrics in DiagnosticDataStore to record the failure
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateGPUMetrics(0.0f, 0);
    return;
  }

  LOG_INFO << "Starting GPU test with lines on all surfaces...";

  using clock = std::chrono::high_resolution_clock;
  auto tStart = clock::now();
  auto tPrev = tStart;
  auto tLastLog = tStart;

  std::vector<double> frameTimes;

  MSG msg = {};
  while (true) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) break;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - tPrev).count();
    tPrev = now;

    Update(dt);
    Render();

    frameTimes.push_back(dt);

    // Print FPS every ~1s
    if (std::chrono::duration<float>(now - tLastLog).count() >= 1.0f) {
      tLastLog = now;
      float fps = 1.0f / dt;
      LOG_DEBUG << "FPS: " << fps;
    }

    // End after 10s
    double elapsed = std::chrono::duration<double>(now - tStart).count();
    if (elapsed >= 10.0) {
      break;
    }
  }

  // Compute some metrics
  double sum = 0.0;
  for (auto t : frameTimes) sum += t;
  double avgDt = sum / frameTimes.size();
  double avgFPS = 1.0 / avgDt;

  // Store results directly in DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  dataStore.updateGPUMetrics((float)avgFPS, (int)frameTimes.size());

  LOG_INFO << "GPU test done. Avg FPS: " << avgFPS << ", frames: " << frameTimes.size();
}

// --------------------------------------------------------------------------------------
void runGpuTests() {
  // Create a window for D3D rendering
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"GPUTestClass";
  RegisterClassExW(&wc);

  HWND hwnd = CreateWindowW(L"GPUTestClass", L"GPU Test", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr,
                            nullptr, GetModuleHandle(nullptr), nullptr);

  if (!hwnd) {
    LOG_ERROR << "Failed to create window";
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateGPUMetrics(0.0f, 0);  // Record failure
    return;
  }

  ShowWindow(hwnd, SW_SHOW);

  GPUTest test;
  if (!test.Initialize(hwnd)) {
    LOG_ERROR << "GPU test initialization failed, cannot run GPU benchmark.";
    LOG_ERROR << "Your system may not support DirectX 11 or the required feature level.";

    // Record failure in diagnostic data
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateGPUMetrics(0.0f, 0);
  } else {
    test.RunTest();
  }

  DestroyWindow(hwnd);
  UnregisterClassW(L"GPUTestClass", GetModuleHandle(nullptr));
}
