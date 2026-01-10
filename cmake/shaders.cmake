include_guard(GLOBAL)

function(_find_fxc out_var)
  if(NOT WIN32)
    message(FATAL_ERROR "fxc.exe is only available on Windows; install the Windows SDK.")
  endif()

  if(DEFINED FXC_EXE)
    set(${out_var} "${FXC_EXE}" PARENT_SCOPE)
    return()
  endif()

  set(_candidates)
  if(DEFINED ENV{WindowsSdkDir})
    list(APPEND _candidates "$ENV{WindowsSdkDir}/bin/*/x64/fxc.exe")
  endif()
  list(APPEND _candidates
    "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe"
    "C:/Program Files (x86)/Windows Kits/11/bin/*/x64/fxc.exe")

  set(_found "")
  foreach(pattern IN LISTS _candidates)
    file(GLOB _matches LIST_DIRECTORIES FALSE "${pattern}")
    if(_matches)
      list(SORT _matches ORDER DESCENDING)
      list(GET _matches 0 _found)
      break()
    endif()
  endforeach()

  if(NOT _found)
    message(FATAL_ERROR "fxc.exe not found. Install the Windows 10/11 SDK (includes fxc).")
  endif()

  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

function(compile_gpu_test_shaders target)
  _find_fxc(FXC_EXE)

  set(_shader_dir "${CMAKE_SOURCE_DIR}/src/diagnostic/shaders")
  set(_out_dir "${CMAKE_BINARY_DIR}/generated/shaders")
  file(MAKE_DIRECTORY "${_out_dir}")

  set(_vs_hlsl "${_shader_dir}/gpu_test_vs.hlsl")
  set(_ps_hlsl "${_shader_dir}/gpu_test_ps.hlsl")
  set_source_files_properties(${_vs_hlsl} ${_ps_hlsl} PROPERTIES HEADER_FILE_ONLY ON)

  set(_vs50 "${_out_dir}/gpu_test_vs_5_0.cso")
  add_custom_command(
    OUTPUT "${_vs50}"
    COMMAND "${FXC_EXE}" /nologo /E main /T vs_5_0 /Fo "${_vs50}" "${_vs_hlsl}"
    DEPENDS "${_vs_hlsl}"
    COMMENT "Compiling GPU test vertex shader (vs_5_0)")

  set(_ps50 "${_out_dir}/gpu_test_ps_5_0.cso")
  add_custom_command(
    OUTPUT "${_ps50}"
    COMMAND "${FXC_EXE}" /nologo /E main /T ps_5_0 /Fo "${_ps50}" "${_ps_hlsl}"
    DEPENDS "${_ps_hlsl}"
    COMMENT "Compiling GPU test pixel shader (ps_5_0)")

  set(_vs40 "${_out_dir}/gpu_test_vs_4_0.cso")
  add_custom_command(
    OUTPUT "${_vs40}"
    COMMAND "${FXC_EXE}" /nologo /E main /T vs_4_0 /Fo "${_vs40}" "${_vs_hlsl}"
    DEPENDS "${_vs_hlsl}"
    COMMENT "Compiling GPU test vertex shader (vs_4_0)")

  set(_ps40 "${_out_dir}/gpu_test_ps_4_0.cso")
  add_custom_command(
    OUTPUT "${_ps40}"
    COMMAND "${FXC_EXE}" /nologo /E main /T ps_4_0 /Fo "${_ps40}" "${_ps_hlsl}"
    DEPENDS "${_ps_hlsl}"
    COMMENT "Compiling GPU test pixel shader (ps_4_0)")

  set(_header "${_out_dir}/gpu_test_shaders.h")
  add_custom_command(
    OUTPUT "${_header}"
    COMMAND ${CMAKE_COMMAND}
      -DOUTPUT_FILE="${_header}"
      -DVS_5_0="${_vs50}"
      -DPS_5_0="${_ps50}"
      -DVS_4_0="${_vs40}"
      -DPS_4_0="${_ps40}"
      -P "${CMAKE_SOURCE_DIR}/cmake/emit_shader_header.cmake"
    DEPENDS "${_vs50}" "${_ps50}" "${_vs40}" "${_ps40}"
    COMMENT "Embedding GPU test shader bytecode")

  add_custom_target(gpu_test_shaders ALL
    DEPENDS "${_header}")

  target_sources(${target} PRIVATE "${_header}")
  target_include_directories(${target} PRIVATE "${_out_dir}")
  add_dependencies(${target} gpu_test_shaders)
endfunction()
