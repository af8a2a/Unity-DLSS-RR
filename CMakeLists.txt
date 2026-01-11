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


# Use the lightweight DLSS plugin (thin NGX wrapper, context management in C#)
add_library(UnityDLSS SHARED
        src/Plugin.h
        src/Plugin.cpp
        src/DLSSPluginLite.h
        src/DLSSPluginLite.cpp
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

    # NGX SDK library paths
    set(NGX_LIB_PATH_DEBUG "${DLSS_LIB_DIR}/nvsdk_ngx_d_dbg.lib")
    set(NGX_LIB_PATH_RELEASE "${DLSS_LIB_DIR}/nvsdk_ngx_d.lib")

    # Check that libraries exist
    if (NOT EXISTS ${NGX_LIB_PATH_RELEASE})
        message(FATAL_ERROR
            "NGX SDK library not found!\n"
            "  Expected: ${NGX_LIB_PATH_RELEASE}\n"
            "  \n"
            "  Please run fetch_dlss.ps1 to download the DLSS SDK including the NGX SDK library.\n"
            "  The library file (nvsdk_ngx_d.lib) should be in: ${DLSS_LIB_DIR}"
        )
    endif()

    # Link against appropriate library based on configuration (use generator expressions for multi-config)
    target_link_libraries(UnityDLSS PRIVATE
        $<$<CONFIG:Debug>:${NGX_LIB_PATH_DEBUG}>
        $<$<NOT:$<CONFIG:Debug>>:${NGX_LIB_PATH_RELEASE}>
    )
    message(STATUS "NGX SDK libraries configured:")
    message(STATUS "  Debug: ${NGX_LIB_PATH_DEBUG}")
    message(STATUS "  Release: ${NGX_LIB_PATH_RELEASE}")
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

        # For multi-config generators (like Visual Studio), use generator expressions
        # to copy the appropriate DLLs based on build configuration
        # Find all DLLs in both directories
        file(GLOB DLSS_DLLS_DEV "${DLSS_LIB_DIR_DEV}/*.dll")
        file(GLOB DLSS_DLLS_REL "${DLSS_LIB_DIR_REL}/*.dll")

        if (DLSS_DLLS_DEV OR DLSS_DLLS_REL)
            # Add DLL directories to PATH for runtime
            set_target_properties(UnityDLSS PROPERTIES
                VS_DEBUGGER_ENVIRONMENT "PATH=${DLSS_LIB_DIR_DEV};${DLSS_LIB_DIR_REL};$ENV{PATH}"
            )

            # Copy Release DLLs for Release/RelWithDebInfo/MinSizeRel configurations
            foreach(DLL ${DLSS_DLLS_REL})
                get_filename_component(DLL_NAME ${DLL} NAME)
                add_custom_command(TARGET UnityDLSS POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E
                        $<IF:$<CONFIG:Debug>,true,copy_if_different>
                        ${DLL}
                        $<TARGET_FILE_DIR:UnityDLSS>
                    COMMENT "Copying ${DLL_NAME} (Release) to output directory"
                )
                message(STATUS "  Found DLSS DLL (Release): ${DLL_NAME}")
            endforeach()

            # Copy Dev DLLs for Debug configuration
            foreach(DLL ${DLSS_DLLS_DEV})
                get_filename_component(DLL_NAME ${DLL} NAME)
                add_custom_command(TARGET UnityDLSS POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E
                        $<IF:$<CONFIG:Debug>,copy_if_different,true>
                        ${DLL}
                        $<TARGET_FILE_DIR:UnityDLSS>
                    COMMENT "Copying ${DLL_NAME} (Dev) to output directory"
                )
                message(STATUS "  Found DLSS DLL (Dev): ${DLL_NAME}")
            endforeach()

            message(STATUS "DLSS DLLs will be copied to output directory based on configuration")
        else()
            message(WARNING "DLSS DLLs not found")
            message(STATUS "  Expected DLLs in: ${DLSS_LIB_DIR_DEV} or ${DLSS_LIB_DIR_REL}")
            message(STATUS "  Please run fetch_dlss.ps1 to download the DLLs")
        endif()
    else()
        message(WARNING "DLSS SDK not found. Run fetch_dlss.ps1 to download it.")
        message(STATUS "  Expected location: ${DLSS_DIR}")
    endif()
endif ()




