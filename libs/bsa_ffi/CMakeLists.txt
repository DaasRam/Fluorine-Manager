cmake_minimum_required(VERSION 3.16)
project(bsa_ffi)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CARGO_BUILD_TYPE "debug")
  set(CARGO_BUILD_FLAGS "")
else()
  set(CARGO_BUILD_TYPE "release")
  set(CARGO_BUILD_FLAGS "--release")
endif()

set(BSA_FFI_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BSA_FFI_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(BSA_FFI_LIB ${BSA_FFI_TARGET_DIR}/${CARGO_BUILD_TYPE}/libbsa_ffi.so)
file(GLOB_RECURSE BSA_FFI_SOURCES CONFIGURE_DEPENDS "${BSA_FFI_DIR}/src/*.rs")

add_custom_command(
  OUTPUT ${BSA_FFI_LIB}
  COMMAND cargo build --locked ${CARGO_BUILD_FLAGS} --target-dir "${BSA_FFI_TARGET_DIR}"
  WORKING_DIRECTORY ${BSA_FFI_DIR}
  COMMENT "Building BSA FFI library (Rust)"
  DEPENDS
    ${BSA_FFI_DIR}/Cargo.toml
    ${BSA_FFI_DIR}/Cargo.lock
    ${BSA_FFI_SOURCES}
  VERBATIM
)

add_custom_target(bsa_ffi_build DEPENDS ${BSA_FFI_LIB})

add_library(bsa_ffi_wrapper INTERFACE)
target_link_libraries(bsa_ffi_wrapper INTERFACE ${BSA_FFI_LIB})
target_include_directories(bsa_ffi_wrapper INTERFACE ${BSA_FFI_DIR}/include)
add_dependencies(bsa_ffi_wrapper bsa_ffi_build)
add_library(mo2::bsa_ffi ALIAS bsa_ffi_wrapper)

install(FILES ${BSA_FFI_LIB} DESTINATION lib)
install(DIRECTORY ${BSA_FFI_DIR}/include/ DESTINATION include)
