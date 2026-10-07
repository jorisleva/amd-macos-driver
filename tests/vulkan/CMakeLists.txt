cmake_minimum_required(VERSION 3.21)
project(amd_gpu_probe LANGUAGES CXX)

option(PROBE_SOFTWARE_ONLY "Build GPU-free graphics admission/reference tests without a Vulkan SDK" OFF)
add_executable(graphics_contract_test graphics_contract_test.cpp)
target_compile_features(graphics_contract_test PRIVATE cxx_std_17)
enable_testing()
add_test(NAME graphics_reference_standalone COMMAND graphics_contract_test --reference)
set(METAL_GRAPHICS "${CMAKE_CURRENT_SOURCE_DIR}/../shaders/apple/graphics")
if(EXISTS "${METAL_GRAPHICS}/texture.frag.spv")
    add_test(NAME metal_graphics_contract COMMAND graphics_contract_test "${METAL_GRAPHICS}" metal-air)
    add_test(NAME metal_graphics_contract_mutations COMMAND graphics_contract_test "${METAL_GRAPHICS}" metal-air --mutations)
    add_test(NAME reject_metal_under_glsl_profile COMMAND graphics_contract_test "${METAL_GRAPHICS}" glsl-control)
    set_tests_properties(reject_metal_under_glsl_profile PROPERTIES WILL_FAIL TRUE)
endif()
if(PROBE_SOFTWARE_ONLY)
    return()
endif()

find_package(Vulkan 1.2 REQUIRED COMPONENTS glslangValidator)
find_program(SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE CODE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT CODE_REVISION)
    set(CODE_REVISION "unknown")
endif()

add_executable(amd_gpu_probe main.cpp)
# Keep the reference and GPU harness explicit build inputs, including existing
# build directories configured with a different MSVC diagnostic language.
set_property(SOURCE main.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/offscreen.h" "${CMAKE_CURRENT_SOURCE_DIR}/offscreen_reference.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/graphics_shader_contract.h")
target_compile_features(amd_gpu_probe PRIVATE cxx_std_17)
target_link_libraries(amd_gpu_probe PRIVATE Vulkan::Vulkan)
target_compile_definitions(amd_gpu_probe PRIVATE CODE_REVISION="${CODE_REVISION}")
if(MSVC)
    target_compile_options(amd_gpu_probe PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(amd_gpu_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv"
    COMMAND Vulkan::glslangValidator -V --target-env vulkan1.2
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vector_add.comp"
            -o "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv"
    COMMAND "${SPIRV_VAL}" --target-env vulkan1.2 "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv"
    DEPENDS shaders/vector_add.comp VERBATIM)
add_custom_target(probe_shader ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
set(GRAPHICS_SPIRV)
foreach(SHADER fullscreen.vert texture.frag triangle.vert solid.frag)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${SHADER}.spv")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND Vulkan::glslangValidator -V --target-env vulkan1.2
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
        COMMAND "${SPIRV_VAL}" --target-env vulkan1.2 "${OUTPUT}"
        DEPENDS "shaders/${SHADER}" VERBATIM)
    list(APPEND GRAPHICS_SPIRV "${OUTPUT}")
endforeach()
add_custom_target(graphics_shaders ALL DEPENDS ${GRAPHICS_SPIRV})

enable_testing()
add_test(NAME verifier_and_device_selection COMMAND amd_gpu_probe --self-test)
add_test(NAME control_shader_contract COMMAND amd_gpu_probe --check-shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
add_test(NAME reject_wrong_entry COMMAND amd_gpu_probe --check-shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv" --entry wrong_entry)
set_tests_properties(reject_wrong_entry PROPERTIES WILL_FAIL TRUE)
add_test(NAME require_explicit_pci_id COMMAND amd_gpu_probe --shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
set_tests_properties(require_explicit_pci_id PROPERTIES WILL_FAIL TRUE)
add_test(NAME graphics_reference_and_verifier COMMAND amd_gpu_probe --self-test-graphics)
add_test(NAME graphics_require_explicit_pci_id COMMAND amd_gpu_probe --graphics
    --shader-directory "${CMAKE_CURRENT_BINARY_DIR}" --image-directory "${CMAKE_CURRENT_BINARY_DIR}/unused-images")
set_tests_properties(graphics_require_explicit_pci_id PROPERTIES WILL_FAIL TRUE)
add_test(NAME reject_mixed_graphics_compute COMMAND amd_gpu_probe --graphics --shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
set_tests_properties(reject_mixed_graphics_compute PROPERTIES WILL_FAIL TRUE)
add_test(NAME glsl_graphics_contract COMMAND amd_gpu_probe --check-graphics-shaders "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME reject_glsl_under_metal_profile COMMAND amd_gpu_probe --check-graphics-shaders "${CMAKE_CURRENT_BINARY_DIR}" --graphics-shader-origin metal-air)
set_tests_properties(reject_glsl_under_metal_profile PROPERTIES WILL_FAIL TRUE)
