cmake_minimum_required(VERSION 3.21)
project(amd_gpu_probe LANGUAGES CXX)

find_package(Vulkan 1.2 REQUIRED COMPONENTS glslangValidator)
find_program(SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE CODE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT CODE_REVISION)
    set(CODE_REVISION "unknown")
endif()

add_executable(amd_gpu_probe main.cpp)
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

enable_testing()
add_test(NAME verifier_and_device_selection COMMAND amd_gpu_probe --self-test)
add_test(NAME control_shader_contract COMMAND amd_gpu_probe --check-shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
add_test(NAME reject_wrong_entry COMMAND amd_gpu_probe --check-shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv" --entry wrong_entry)
set_tests_properties(reject_wrong_entry PROPERTIES WILL_FAIL TRUE)
add_test(NAME require_explicit_pci_id COMMAND amd_gpu_probe --shader "${CMAKE_CURRENT_BINARY_DIR}/vector_add.spv")
set_tests_properties(require_explicit_pci_id PROPERTIES WILL_FAIL TRUE)
