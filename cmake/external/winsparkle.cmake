# CMake WinSparkle auto-updater import/configuration module

include_guard(GLOBAL)

if (NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(WARNING "WinSparkle validated only for windows MSVC-x64")
endif()

# Find WinSparkle library files via vcpkg
find_path(WINSPARKLE_INCLUDE_DIR
	NAMES winsparkle/winsparkle.h
	PATHS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
	NO_DEFAULT_PATH
)

find_library(WINSPARKLE_LIBRARY
	NAMES WinSparkle
	PATHS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib
	NO_DEFAULT_PATH
)

find_file(WINSPARKLE_DLL
	NAMES WinSparkle.dll
	PATHS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin
	NO_DEFAULT_PATH
)

if(WINSPARKLE_INCLUDE_DIR AND WINSPARKLE_LIBRARY AND WINSPARKLE_DLL)
	message(STATUS "Found WinSparkle via vcpkg")
	
	# Create imported target
	add_library(winsparkle::winsparkle SHARED IMPORTED)
	set_target_properties(winsparkle::winsparkle PROPERTIES
		IMPORTED_LOCATION "${WINSPARKLE_DLL}"
		IMPORTED_IMPLIB "${WINSPARKLE_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${WINSPARKLE_INCLUDE_DIR}"
	)
	
	# Store DLL path for later use by main target
	set(WINSPARKLE_DLL_TO_COPY "${WINSPARKLE_DLL}" CACHE INTERNAL "")

	# Ensure the WinSparkle runtime DLL is present in the build output dir
	# so that local runs and the installer can pick it up.
	add_custom_target(winsparkle_copy_runtime ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/$<CONFIG>"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${WINSPARKLE_DLL}" "${CMAKE_BINARY_DIR}/$<CONFIG>/"
		COMMENT "Copying WinSparkle.dll to ${CMAKE_BINARY_DIR}/$<CONFIG>"
	)
else()
	message(FATAL_ERROR "WinSparkle not found. Please install via vcpkg: vcpkg install winsparkle")
endif()