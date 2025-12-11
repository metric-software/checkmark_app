# CMake presentmon post-vcpkg pull configuration module

include_guard(GLOBAL)

if (NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(WARNING "Presentmon validated only for windows MSVC-x64")
endif()

# annoying code - just needed it done
add_library(presentmon::core STATIC IMPORTED GLOBAL)
set_target_properties(presentmon::core PROPERTIES
	IMPORTED_LOCATION_DEBUG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib/PresentData.lib"
	IMPORTED_LOCATION_RELEASE "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/PresentData.lib"
	IMPORTED_LOCATION_MINSIZEREL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/PresentData.lib"
	IMPORTED_LOCATION_RELWITHDEBINFO "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/PresentData.lib"
	INTERFACE_INCLUDE_DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/presentmon"
)
