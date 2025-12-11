#pragma once

#include <windows.h>
#include <SDKDDKVer.h>
#include <WinUser.h>
#include <windowsx.h>

// DirectX headers
#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi.h>

// Standard headers
#include <atomic>
#include <vector>

class GPUTest {
 public:
  GPUTest();
  ~GPUTest();

  bool Initialize(HWND hwnd);
  void Update(float deltaTime);
  void Render();
  void RunTest();

 private:
  bool Create3DEnvironment();
  bool CreateSphereGeometry();
  bool CreateShaders();
  bool CreateDepthBuffer(UINT width, UINT height);

  struct Vertex3D {
    DirectX::XMFLOAT3 pos;    // 3D position (x,y,z) for vertex
    DirectX::XMFLOAT4 color;  // RGBA color values (0.0-1.0)
    DirectX::XMFLOAT3 normal;
  };

  struct CB_Matrices {
    DirectX::XMMATRIX world;  // World transformation matrix
    DirectX::XMMATRIX view;   // Camera view matrix
    DirectX::XMMATRIX proj;   // Projection matrix for perspective
  };

  struct Ball {
    DirectX::XMFLOAT3 position;  // Current 3D position
    DirectX::XMFLOAT3 velocity;  // Movement direction and speed
    DirectX::XMFLOAT4 color;     // RGBA color of the ball
  };

  struct CB_Lighting {
    DirectX::XMFLOAT4
      lightDirection;  // Directional light direction (in world space)
    DirectX::XMFLOAT4 lightColor;  // Light color
  };

 private:
  // D3D resources
  ID3D11Device* device;                  // D3D device for creating resources
  ID3D11DeviceContext* context;          // D3D context for rendering commands
  IDXGISwapChain* swapChain;             // Manages the render buffers
  ID3D11RenderTargetView* renderTarget;  // View of back buffer for rendering
  ID3D11RasterizerState* solidRS;        // Rasterizer state for solid fill mode

  // Depth/Stencil resources for 3D rendering
  ID3D11Texture2D* depthStencilBuffer;       // Z-buffer texture
  ID3D11DepthStencilView* depthStencilView;  // View of depth buffer

  // Geometry buffers
  ID3D11Buffer* envVertexBuffer;     // Vertices for room geometry
  ID3D11Buffer* envIndexBuffer;      // Indices for room geometry
  ID3D11Buffer* gridVertexBuffer;    // Vertices for grid lines
  ID3D11Buffer* sphereVertexBuffer;  // Vertices for sphere geometry
  ID3D11Buffer* sphereIndexBuffer;   // Indices for sphere geometry
  ID3D11Buffer* matricesCB;  // Constant buffer for transformation matrices
  ID3D11Buffer* lightingCB;

  // Shader resources
  ID3D11VertexShader* vertexShader;  // Shader for vertex processing
  ID3D11PixelShader* pixelShader;    // Shader for pixel processing
  ID3D11InputLayout* inputLayout;    // Defines vertex data layout

  // Visualization options
  ID3D11RasterizerState* wireframeRS;  // Rasterizer state for wireframe mode

  // Geometry counters
  UINT sphereIndexCount;  // Number of indices in sphere geometry
  UINT gridLineCount;     // Number of vertices in grid lines

  // Camera matrices
  DirectX::XMMATRIX viewMatrix;  // Camera view transformation
  DirectX::XMMATRIX projMatrix;  // Perspective projection

  // Physics boundary constraints
  float minX, maxX;  // X-axis movement limits
  float minY, maxY;  // Y-axis movement limits
  float minZ, maxZ;  // Z-axis movement limits

  // Add this with other member variables:
  bool tearingAllowed;  // Flag for tearing support in the display

  // Animation data
  static constexpr int NUM_BALLS = 500;  // Increased from 100 to 500 balls
  std::vector<Ball> balls;               // Container for ball data
};

// Only keep the runGpuTests declaration here
void runGpuTests();
