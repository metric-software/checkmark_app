# CMake toolchain configuration module

include_guard(GLOBAL)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
  # no vcpkg toolchain was passed to cmake configure, so we will try to find it
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    # check environment variables for VCPKG_ROOT
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
  else()
    # else, we will assume the default location, and if it doesn't exist, we will clone & bootstrap it
    set(VCPKG_ROOT "C:/vcpkg")
    if(NOT EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
      file(REMOVE_RECURSE "${VCPKG_ROOT}")
      execute_process(COMMAND git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} RESULT_VARIABLE RESULT)
      if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "failed to clone vcpkg: ${RESULT}")
      endif()
      execute_process(COMMAND "${VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics WORKING_DIRECTORY ${VCPKG_ROOT} RESULT_VARIABLE RESULT)
      if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "failed to bootstrap vcpkg: ${RESULT}")
      endif()
    endif()
  endif()
  set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  message(STATUS "vcpkg found: ${VCPKG_ROOT}")
else()
  message(STATUS "using a toolchain passed as args: ${CMAKE_TOOLCHAIN_FILE}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
