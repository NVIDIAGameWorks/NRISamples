cmake_minimum_required(VERSION 3.15)

# Options
option(DISABLE_SHADER_COMPILATION "Disable compilation of shaders" OFF)
option(USE_MINIMAL_DATA "Use minimal '_Data'(90MB)" OFF)
option(USE_DXC_FROM_PACKMAN_ON_AARCH64 "Use DXC from Packman for `aarch64`" ON)

# Cached
set(DXC_CUSTOM_PATH "custom/path/to/dxc" CACHE STRING "This DXC will be used if Vulkan SDK is not installed")
set(GLOBAL_BIN_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/_Build" CACHE STRING "")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "")

# Create project
project(NRISamples LANGUAGES C CXX)

# Download dependencies using Packman
if(WIN32)
    set(NRI_PACKMAN_EXT ".cmd")
endif()

if((CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
    if(WIN32)
        set(PACKMAN_PLATFORM "windows-x86_64")
    else()
        set(PACKMAN_PLATFORM "linux-x86_64")
    endif()
elseif((CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64"))
    set(PACKMAN_PLATFORM "linux-aarch64")
else()
    message(FATAL_ERROR "Unsupported architecture: '${CMAKE_SYSTEM_PROCESSOR}'")
endif()
message("Packman platform: ${PACKMAN_PLATFORM}")

if(USE_MINIMAL_DATA)
    set(NRI_DATA_VERSION 2.0_minimal)
    message("Using minimal _Data.")
else()
    set(NRI_DATA_VERSION 2.1)
endif()

execute_process(COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/External/Packman/Packman${NRI_PACKMAN_EXT}" pull "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies.xml" -p ${PACKMAN_PLATFORM} -t nri_data_version=${NRI_DATA_VERSION}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE PACKMAN_RESULT)

if(NOT "${PACKMAN_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Packman failed(code = ${PACKMAN_RESULT})")
endif()

# Setup dependencies
if((CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64"))
    if(("${DXC_CUSTOM_PATH}" STREQUAL "") AND USE_DXC_FROM_PACKMAN_ON_AARCH64)
        set(DXC_CUSTOM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/DXC/bin/dxc" CACHE STRING "")
    endif()
endif()

# Shader compilation
if(NOT DISABLE_SHADER_COMPILATION)
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

    add_custom_target(${PROJECT_NAME}_Shaders ALL DEPENDS ${SHADER_FILES} SOURCES "${HEADER_FILES}")
    set_property(TARGET ${PROJECT_NAME}_Shaders PROPERTY FOLDER ${PROJECT_NAME})
endif()

# Globals?
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POSITION_INDEPENDENT_BINARIES ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

# Compile options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILE_OPTIONS -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS -Wextra)
elseif(MSVC)
    set(COMPILE_OPTIONS /W4 /WX /wd4324)
endif()

# Compile difinitions
if(WIN32)
    set(COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE)
endif()

# External/NRIFramework
add_subdirectory("External/NRIFramework")

# Samples
function(add_sample NAME EXT)
    add_executable(${NAME} "Source/${NAME}.${EXT}")
    source_group("" FILES "Source/${NAME}.${EXT}")

    if(NOT DISABLE_SHADER_COMPILATION)
        add_dependencies(${NAME} ${PROJECT_NAME}_Shaders)
    endif()

    target_include_directories(${NAME} PRIVATE "External/NRIFramework/Include")
    target_include_directories(${NAME} PRIVATE "External/NRIFramework/External")
    target_include_directories(${NAME} PRIVATE "External/NRIFramework/External/NRI/Include")
    target_compile_definitions(${NAME} PRIVATE ${COMPILE_DEFINITIONS} PROJECT_NAME=${NAME})
    target_compile_options(${NAME} PRIVATE ${COMPILE_OPTIONS})
    target_link_libraries(${NAME} PRIVATE NRIFramework NRI)

    set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GLOBAL_BIN_OUTPUT_PATH}/$<CONFIG>")

    set_property(TARGET ${NAME} PROPERTY FOLDER ${PROJECT_NAME})
    set_property(TARGET ${NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Copy arguments for Visual Studio Smart Command Line Arguments extension
    if(WIN32 AND MSVC)
        configure_file(.args "${CMAKE_BINARY_DIR}/${NAME}.args.json" COPYONLY)
    endif()
endfunction()

add_sample(AsyncCompute cpp)
add_sample(Clear cpp)
add_sample(CTest c)
add_sample(DeviceInfo cpp)
add_sample(MultiGPU cpp)
add_sample(Multithreading cpp)
add_sample(RayTracingTriangle cpp)
add_sample(RayTracingBoxes cpp)
add_sample(Readback cpp)
add_sample(SceneViewer cpp)
add_sample(Triangle cpp)
add_sample(Wrapper cpp)

# Samples - special
if(WIN32)
    target_link_libraries(Wrapper PRIVATE d3d11 d3d12)
endif()
target_include_directories(Wrapper PRIVATE "External/NRIFramework/External/NRI/External/vulkan/include")
