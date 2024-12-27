cmake_minimum_required(VERSION 3.28)

set(GLSL_VALIDATOR_NAME "glslangValidator")

find_program(
    GLSL_VALIDATOR ${GLSL_VALIDATOR_NAME} 
    HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/
)
if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "${GLSL_VALIDATOR_NAME} not found - cannot compile shaders.")
else()
    message(STATUS "${GLSL_VALIDATOR_NAME} found - using ${GLSL_VALIDATOR} for shader compilation")
endif()

set(SHADERS_PYTHON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/compile_shaders.py")

add_custom_target(
    shaders 
    COMMAND 
        python "${SHADERS_PYTHON_SCRIPT}" -c "${GLSL_VALIDATOR}"
    WORKING_DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}"
)