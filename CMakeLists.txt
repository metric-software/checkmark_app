cmake_minimum_required(VERSION 3.31...4.0)

include("${CMAKE_SOURCE_DIR}/cmake/bootstrap.cmake")

project(checkmark VERSION 0.1.0 LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/configure.cmake")

# ----------------------------------------------------------------------------
# Server URL Configuration
# - Default: Production server (https://checkmark.gg)
# - For local development: configure with -DCHECKMARK_USE_LOCAL_SERVER=ON
# - You can also override at runtime with env var CHECKMARK_BASE_URL
# ----------------------------------------------------------------------------
option(CHECKMARK_USE_LOCAL_SERVER "Use localhost instead of production server (for development)" OFF)

# Production URL - this is what release builds and users will connect to
set(CHECKMARK_PRODUCTION_URL "https://checkmark.gg")
# Local development URL - only used when CHECKMARK_USE_LOCAL_SERVER=ON
set(CHECKMARK_LOCAL_URL "https://localhost" CACHE STRING "Local development server URL")

if(CHECKMARK_USE_LOCAL_SERVER)
  set(_CHECKMARK_DEFAULT_BASE_URL "${CHECKMARK_LOCAL_URL}")
  message(STATUS "Checkmark: Building with LOCAL server: ${_CHECKMARK_DEFAULT_BASE_URL}")
else()
  set(_CHECKMARK_DEFAULT_BASE_URL "${CHECKMARK_PRODUCTION_URL}")
  message(STATUS "Checkmark: Building with PRODUCTION server: ${_CHECKMARK_DEFAULT_BASE_URL}")
endif()

# Expose default base URL to code as string macro CHECKMARK_DEFAULT_BASE_URL
target_compile_definitions(${PROJECT_NAME} PRIVATE CHECKMARK_DEFAULT_BASE_URL="${_CHECKMARK_DEFAULT_BASE_URL}")

# include directories for compiler
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src")

# Versioning: allow a manual build number bump and generate a header
set(CHECKMARK_VERSION_BUILD 0 CACHE STRING "Build number for fine-grained updates")
configure_file(
  ${CMAKE_SOURCE_DIR}/src/version/checkmark_version.h.in
  ${CMAKE_BINARY_DIR}/generated/checkmark_version.h
  @ONLY
)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/generated")

# Generate protobuf C++ files
add_proto_library(diagnostic_proto PROTOS diagnostic.proto)
add_proto_library(benchmark_common_proto PROTOS benchmark_common.proto)
add_proto_library(benchmark_public_proto PROTOS benchmark_public.proto)
add_proto_library(benchmark_full_proto PROTOS benchmark_full.proto)
add_proto_library(benchmark_upload_proto PROTOS benchmark_upload.proto)

# libraries for linker
target_link_libraries(${PROJECT_NAME} PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network cpuid::cpuid presentmon::core
  CUDA::nvml nvidia::nvapi
  winsparkle::winsparkle spdlog::spdlog protobuf::libprotobuf diagnostic_proto
  benchmark_common_proto benchmark_public_proto benchmark_full_proto benchmark_upload_proto
  d3d11.lib dxgi.lib d3dcompiler.lib
  powrprof.lib Pdh.Lib shcore.lib WS2_32.lib
  Crypt32.lib iphlpapi.lib wbemuuid.lib
  dwmapi.lib wininet.dll setupapi.lib
  psapi.lib Rpcrt4.lib Shlwapi.lib
  tdh.lib
)

# Ensure generated protobuf headers exist before compiling the main target
add_dependencies(${PROJECT_NAME}
  benchmark_common_proto benchmark_public_proto benchmark_full_proto benchmark_upload_proto)

target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:NO")
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX CAN_USE_NVML)

# Optional tests (disabled by default to keep production builds lean)
option(CHECKMARK_BUILD_TESTS "Build Checkmark unit tests" OFF)
if(CHECKMARK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
