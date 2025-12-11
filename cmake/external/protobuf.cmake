# CMake Protocol Buffers import/configuration module

include_guard(GLOBAL)

if (NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(WARNING "Protobuf validated only for windows MSVC-x64")
endif()

# Find Protobuf via vcpkg
find_package(protobuf CONFIG REQUIRED)

if(protobuf_FOUND)
	message(STATUS "Found Protobuf via vcpkg")
	
	# Create alias target for consistency
	if(NOT TARGET protobuf::protobuf)
		add_library(protobuf::protobuf ALIAS protobuf::libprotobuf)
	endif()
else()
	message(FATAL_ERROR "Protobuf not found. Please install via vcpkg: vcpkg install protobuf")
endif()

# Function to generate C++ files from .proto files
function(add_proto_library target_name)
	cmake_parse_arguments(PROTO "" "PROTO_DIR" "PROTOS" ${ARGN})
	
	if(NOT PROTO_PROTO_DIR)
		set(PROTO_PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
	endif()
	
	set(PROTO_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
	
	foreach(PROTO_FILE ${PROTO_PROTOS})
		get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
		set(PROTO_SRC "${PROTO_GENERATED_DIR}/${PROTO_NAME}.pb.cc")
		set(PROTO_HDR "${PROTO_GENERATED_DIR}/${PROTO_NAME}.pb.h")
		
		add_custom_command(
			OUTPUT ${PROTO_SRC} ${PROTO_HDR}
			COMMAND protobuf::protoc
			ARGS --cpp_out=${PROTO_GENERATED_DIR}
				 --proto_path=${PROTO_PROTO_DIR}
				 ${PROTO_PROTO_DIR}/${PROTO_FILE}
			DEPENDS ${PROTO_PROTO_DIR}/${PROTO_FILE}
			COMMENT "Generating C++ protobuf files for ${PROTO_FILE}"
		)
		
		list(APPEND PROTO_SRCS ${PROTO_SRC})
		list(APPEND PROTO_HDRS ${PROTO_HDR})
	endforeach()
	
	# Create library target
	add_library(${target_name} ${PROTO_SRCS} ${PROTO_HDRS})
	target_link_libraries(${target_name} PUBLIC protobuf::libprotobuf)
	target_include_directories(${target_name} PUBLIC ${PROTO_GENERATED_DIR})
	
	# Set C++ standard
	set_target_properties(${target_name} PROPERTIES
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED ON
	)
endfunction()