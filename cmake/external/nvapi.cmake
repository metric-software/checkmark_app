# CMake nvapi import/configuration module

include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
	nvapi
	GIT_REPOSITORY https://github.com/NVIDIA/nvapi.git
	GIT_TAG 7cb76fce2f52de818b3da497af646af1ec16ce27
)

FetchContent_MakeAvailable(nvapi)

if (NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(WARNING "NVAPI validated only for windows MSVC-x64-x86")
endif()

# annoying code - just needed it done
set(NVAPI_LIB "amd64/nvapi64.lib")
if(NOT ${CMAKE_CL_64})
	set(NVAPI_LIB "x86/nvapi.lib")
endif()
add_library(nvidia::nvapi STATIC IMPORTED GLOBAL)
set_target_properties(nvidia::nvapi PROPERTIES
	IMPORTED_LOCATION "${nvapi_SOURCE_DIR}/${NVAPI_LIB}"
	INTERFACE_INCLUDE_DIRECTORIES "${nvapi_SOURCE_DIR}"
)
