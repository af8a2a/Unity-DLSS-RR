cmake_minimum_required(VERSION 3.20)

project(UnityPlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

# Paths
set(PLUGIN_API_DIR ${CMAKE_SOURCE_DIR}/PluginAPI)
set(DLSS_DIR ${CMAKE_SOURCE_DIR}/External/NVIDIA-DLSS)
set(DLSS_INCLUDE_DIR ${DLSS_DIR}/include)
set(DLSS_LIB_DIR ${DLSS_DIR}/lib)
set(DLSS_LIB_DIR_DEV ${DLSS_LIB_DIR}/Dev)
set(DLSS_LIB_DIR_REL ${DLSS_LIB_DIR}/Rel)


add_library(UnityDLSS SHARED
        src/Plugin.h
        src/Plugin.cpp
        src/DLSSPlugin.h
        src/DLSSPlugin.cpp
        src/DLSSContext.h
        src/DLSSContext.cpp
)

target_include_directories(UnityDLSS
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PLUGIN_API_DIR}
        ${DLSS_INCLUDE_DIR}
)

# Add DLSS library directory to linker search path
if (EXISTS ${DLSS_LIB_DIR})
    target_link_directories(UnityDLSS PRIVATE ${DLSS_LIB_DIR})

    # Link against NGX SDK library (nvsdk_ngx_d.lib for dynamic linking)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
        set(NGX_LIB_PATH "${DLSS_LIB_DIR}/nvsdk_ngx_d_dbg.lib")
    else ()
        set(NGX_LIB_PATH "${DLSS_LIB_DIR}/nvsdk_ngx_d.lib")
    endif ()
    
    if (EXISTS ${NGX_LIB_PATH})
        target_link_libraries(UnityDLSS PRIVATE ${NGX_LIB_PATH})
        message(STATUS "Linked against NGX SDK library: ${NGX_LIB_PATH}")
    else()
        message(FATAL_ERROR 
            "NGX SDK library not found!\n"
            "  Expected: ${NGX_LIB_PATH}\n"
            "  \n"
            "  Please run fetch_dlss.ps1 to download the DLSS SDK including the NGX SDK library.\n"
            "  The library file (nvsdk_ngx_d.lib) should be in: ${DLSS_LIB_DIR}"
        )
    endif()
endif()




if (WIN32)
    set_target_properties(UnityDLSS PROPERTIES
            OUTPUT_NAME "UnityDLSS"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    
    # Check if DLSS is available
    if (EXISTS ${DLSS_INCLUDE_DIR} AND EXISTS ${DLSS_LIB_DIR})
        message(STATUS "DLSS SDK found at: ${DLSS_DIR}")
        
        # Determine which DLL directory to use based on build type
        # Debug builds use Dev DLLs, Release builds use Rel DLLs
        if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
            # Debug build - use Dev DLLs
            set(DLSS_DLL_SOURCE_DIR ${DLSS_LIB_DIR_DEV})
            set(DLSS_BUILD_TYPE "Debug (Dev)")
        else()
            # Release build - use Rel DLLs
            set(DLSS_DLL_SOURCE_DIR ${DLSS_LIB_DIR_REL})
            set(DLSS_BUILD_TYPE "Release (Rel)")
        endif()
        
        message(STATUS "Using DLSS DLLs from: ${DLSS_DLL_SOURCE_DIR} (${DLSS_BUILD_TYPE})")
        
        # Find and copy DLSS DLL files from the appropriate directory
        file(GLOB DLSS_DLLS "${DLSS_DLL_SOURCE_DIR}/*.dll")
        if (DLSS_DLLS)
            # Add DLL directory to PATH for runtime (so NGX SDK can find the DLLs)
            # This sets the PATH environment variable for the target
            set_target_properties(UnityDLSS PROPERTIES
                VS_DEBUGGER_ENVIRONMENT "PATH=${DLSS_DLL_SOURCE_DIR};$ENV{PATH}"
            )
            
            # Copy DLLs to output directory (so they're next to UnityDLSS.dll)
            foreach(DLL ${DLSS_DLLS})
                get_filename_component(DLL_NAME ${DLL} NAME)
                add_custom_command(TARGET UnityDLSS POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${DLL}
                    $<TARGET_FILE_DIR:UnityDLSS>
                    COMMENT "Copying ${DLL_NAME} (${DLSS_BUILD_TYPE}) to output directory"
                )
                message(STATUS "  Found DLSS DLL: ${DLL_NAME}")
            endforeach()
            message(STATUS "DLSS DLLs (${DLSS_BUILD_TYPE}) will be copied to output directory: ${CMAKE_BINARY_DIR}/bin")
        else()
            message(WARNING "DLSS DLLs not found in ${DLSS_DLL_SOURCE_DIR}")
            message(STATUS "  Expected DLLs: nvngx_dlss.dll, nvngx_dlssd.dll, nvngx_dlssg.dll")
            message(STATUS "  Please run fetch_dlss.ps1 to download the DLLs")
        endif()
    else()
        message(WARNING "DLSS SDK not found. Run fetch_dlss.ps1 to download it.")
        message(STATUS "  Expected location: ${DLSS_DIR}")
    endif()
endif ()




