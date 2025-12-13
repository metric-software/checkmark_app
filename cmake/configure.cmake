# CMake project configuration module

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_SOURCE_DIR}/cmake/external/presentmon.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/external/nvapi.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/external/protobuf.cmake")

include("${CMAKE_SOURCE_DIR}/cmake/external/licenses.cmake")

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(cpuid CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CUDAToolkit REQUIRED)

# sets automoc, autorcc, autouic, and other qt related
qt_standard_project_setup()

file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/*.c*" "${CMAKE_SOURCE_DIR}/src/*.h*")
file(GLOB_RECURSE RESOURCES "${CMAKE_SOURCE_DIR}/cmake/windows/*.ico" "${CMAKE_SOURCE_DIR}/cmake/windows/*.rc" "${CMAKE_SOURCE_DIR}/cmake/*.manifest")
qt_add_resources(QTRESOURCES "cmake/windows/checkmark.qrc")

if(DEFINED CHECKMARK_VERSION_RC AND EXISTS "${CHECKMARK_VERSION_RC}")
  list(APPEND RESOURCES "${CHECKMARK_VERSION_RC}")
endif()

qt_add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${RESOURCES} ${QTRESOURCES})

# qt post build step - bundles runtime dependencies for deployment
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND Qt6::windeployqt ARGS $<TARGET_FILE:${PROJECT_NAME}>)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/misc $<TARGET_FILE_DIR:${PROJECT_NAME}> VERBATIM)

# Copy generated appcast (version-synced with version/app_version.iss)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/generated/appcast.xml"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/appcast.xml"
  COMMENT "Copying generated appcast.xml to output directory"
)
