cmake_minimum_required(VERSION 3.15)

message("Processing 'NRI samples'...")

set(DXC_CUSTOM_PATH "custom/path/to/dxc" CACHE STRING "This DXC will be used if Vulkan SDK is not installed")

# Settings
option(DISABLE_SHADER_COMPILATION "Disable compilation of shaders" OFF)
option(USE_MINIMAL_DATA "Use minimal '_Data' (90MB)" OFF)
option(USE_DXC_FROM_PACKMAN_ON_AARCH64 "Download DXC for aarch64 using Packman" ON)

project(NRISamples LANGUAGES C CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Detecting target architecture
if ((CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
    set(PACKMAN_PLATFORM_SUFFIX "x86_64")
elseif ((CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64"))
    set(PACKMAN_PLATFORM_SUFFIX "aarch64")
else()
    message(FATAL_ERROR "Unknown architecture: '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BUILD_ARCHITECTURE_SUFFIX "x86_64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(BUILD_ARCHITECTURE_SUFFIX "x86")
endif()

# Use different scripts on different platforms
if (WIN32)
    set(PACKMAN_SCRIPT "packman.cmd")
    set(PACKMAN_PLATFORM "windows-${PACKMAN_PLATFORM_SUFFIX}")
else()
    set(PACKMAN_SCRIPT "packman")
    set(PACKMAN_PLATFORM "linux-${PACKMAN_PLATFORM_SUFFIX}")
endif()

# Choose data package
if (USE_MINIMAL_DATA)
    set(NRI_DATA_VERSION 2.0_minimal)
    message(STATUS "Using minimal _Data.")
else()
    set(NRI_DATA_VERSION 2.1)
endif()

# Verify that packman is available
find_file(PACKMAN NAMES ${PACKMAN_SCRIPT} PATHS "${CMAKE_CURRENT_SOURCE_DIR}/External/Packman")
if (NOT PACKMAN)
    message(FATAL_ERROR "Can't find packman: '${CMAKE_CURRENT_SOURCE_DIR}/External/Packman/${PACKMAN_SCRIPT}' (did you init submodules?)")
endif()

# Loading dependcies using packman
execute_process(COMMAND "${PACKMAN}" pull "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.xml" -t nri_data_version=${NRI_DATA_VERSION}
    -p ${PACKMAN_PLATFORM} RESULT_VARIABLE PACKMAN_RESULT)
if ("${PACKMAN_RESULT}" STREQUAL "0")
    message(STATUS "Packman result: success (platform: '${PACKMAN_PLATFORM}')")
else()
    message(FATAL_ERROR "Packman result: ${PACKMAN_RESULT} (platform: '${PACKMAN_PLATFORM}')")
endif()

# Packman downloads some dependencies on aarch64
if ((CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64"))
    if (("${DXC_CUSTOM_PATH}" STREQUAL "") AND USE_DXC_FROM_PACKMAN_ON_AARCH64)
        set(DXC_CUSTOM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/DXC/bin/dxc" CACHE STRING)
    endif()
endif()

# Shader compilation
if (NOT DISABLE_SHADER_COMPILATION)
    # Include path for DXC and FXC (used in ShaderCompilation.cmake)
    set(SHADER_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Source/Shaders")
    set(MATHLIB_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/NRIFramework/External/MathLib")
    set(EXTERNAL_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External")
    set(SHADER_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/_Shaders")

    # List HLSL shaders and headers
    file(GLOB_RECURSE HLSL_FILES "Source/Shaders/*.hlsl" "Source/Shaders/*.hlsli")
    file(GLOB_RECURSE HLSL_MATHLIB_HEADER_FILES "${MATHLIB_INCLUDE_PATH}/*.hlsli")

    file(MAKE_DIRECTORY ${SHADER_OUTPUT_PATH})
    include(ShaderCompilation.cmake)

    # Define build rules for the shaders
    list_hlsl_headers("${HLSL_FILES}" HEADER_FILES)
    list_hlsl_headers("${HLSL_MATHLIB_HEADER_FILES}" HEADER_FILES)
    list_hlsl_shaders("${HLSL_FILES}" "${HEADER_FILES}" SHADER_FILES)
    add_custom_target(Shaders ALL DEPENDS ${SHADER_FILES} SOURCES "${HEADER_FILES}")
    set_property(TARGET Shaders PROPERTY FOLDER "Samples")
endif()

# MathLib requires SSE4.1
if ((CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
    set(ARCHITECTURE_COMPILE_OPTIONS -msse4.1)
endif()

# Compiler-specific options
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # -Wno-pragma-pack: assimp causes a number of these warnings
    # warning: the current #pragma pack alignment value is modified in the included file [-Wpragma-pack]
    add_compile_options(${ARCHITECTURE_COMPILE_OPTIONS} -Wno-missing-braces -Wno-return-type-c-linkage -Wno-pragma-pack -Wno-format-security)
    set(DETEX_COMPILE_OPTIONS -Wno-unused-function)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(${ARCHITECTURE_COMPILE_OPTIONS})
endif()

if (WIN32)
    # On Windows, GLFW requires /MT
    add_compile_options($<$<CONFIG:Debug>:/MTd> $<$<CONFIG:Release>:/MT> $<$<CONFIG:RelWithDebInfo>:/MT> $<$<CONFIG:MinSizeRel>:/MT>)
endif()

set(OUTPUT_BIN_PATH "${CMAKE_SOURCE_DIR}/_Build")

# NRI
set(NRI_PROJECT_FOLDER "External/NRI")
set(NRI_OUTPUT_BIN_PATH "${OUTPUT_BIN_PATH}")
option(NRI_DISABLE_WAYLAND_SUPPORT "" ON)
add_subdirectory("External/NRIFramework/External/NRI")

# NRI framework
set(NRIF_OUTPUT_PATH "${OUTPUT_BIN_PATH}")
add_subdirectory("External/NRIFramework")

# Defines for all projects
if (WIN32)
    set(PLATFORM_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE)
endif()

# Creates a project for sample and adds dependent operations
function(add_sample SAMPLE_NAME)
    add_executable(${SAMPLE_NAME} "Source/${SAMPLE_NAME}.cpp")
    source_group("" FILES "Source/${SAMPLE_NAME}.cpp")
    if (NOT DISABLE_SHADER_COMPILATION)
        add_dependencies(${SAMPLE_NAME} Shaders)
    endif()
    target_include_directories(${SAMPLE_NAME} PRIVATE "Source" "External")
    target_include_directories(${SAMPLE_NAME} PRIVATE "External/NRIFramework/Include")
    target_include_directories(${SAMPLE_NAME} PRIVATE "External/NRIFramework/External")
    target_include_directories(${SAMPLE_NAME} PRIVATE "External/NRIFramework/External/NRI/Include")
    target_compile_definitions(${SAMPLE_NAME} PRIVATE ${PLATFORM_DEFINITIONS} PROJECT_NAME=${SAMPLE_NAME})

    target_link_libraries(${SAMPLE_NAME} PRIVATE NRIFramework NRI)

    set_target_properties(${SAMPLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BIN_PATH}/$<CONFIG>")

    set_property(TARGET ${SAMPLE_NAME} PROPERTY FOLDER "Samples")
    set_property(TARGET ${SAMPLE_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Copy arguments for Visual Studio Smart Command Line Arguments extension
    if (WIN32 AND MSVC)
        configure_file(.args "${CMAKE_BINARY_DIR}/${SAMPLE_NAME}.args.json" COPYONLY)
    endif()
endfunction()

add_sample(00_Clear)
add_sample(01_Triangle)
add_sample(02_SceneViewer)
add_sample(03_Readback)
add_sample(04_AsyncCompute)
add_sample(05_Multithreading)
add_sample(06_MultiGPU)
add_sample(07_RayTracing_Triangle)
add_sample(08_RayTracing_Boxes)
add_sample(09_Wrapper)

if (WIN32)
    target_link_libraries(09_Wrapper PRIVATE d3d11 d3d12)  
endif()
target_include_directories(09_Wrapper PRIVATE "External/NRIFramework/External/NRI/External/vulkan/include")