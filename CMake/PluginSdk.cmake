set(_pvzp_x64 FALSE)
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR
   (NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$"))
    set(_pvzp_x64 TRUE)
endif()
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND _pvzp_x64)
    target_compile_definitions(pvz-portable PRIVATE PVZP_BUILD_GAME)
    set_target_properties(pvz-portable PROPERTIES ENABLE_EXPORTS ON INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(MSVC)
        target_sources(pvz-portable PRIVATE "${PROJECT_SOURCE_DIR}/CMake/native-msvc.def")
    else()
        target_sources(pvz-portable PRIVATE "${PROJECT_SOURCE_DIR}/CMake/native-gnu.def")
    endif()
    add_library(pvzp-sdk INTERFACE)
    target_include_directories(pvzp-sdk INTERFACE
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/SexyAppFramework"
        "${PROJECT_SOURCE_DIR}/src/SexyAppFramework/sound/SDL-Mixer-X/include")
    target_link_libraries(pvzp-sdk INTERFACE pvz-portable SDL2::SDL2)
    target_compile_features(pvzp-sdk INTERFACE cxx_std_20)
    if(MSVC)
        target_compile_options(pvzp-sdk INTERFACE /utf-8)
        set(_pvzp_sdk_options "/utf-8")
    endif()
    target_compile_definitions(pvzp-sdk INTERFACE
        $<$<BOOL:${PVZ_DEBUG}>:PVZ_DEBUG> $<$<BOOL:${LOW_MEMORY}>:LOW_MEMORY>)
    option(PVZP_BUILD_PLUGIN_TESTS "Build the standalone SDK smoke plugin" OFF)
    if(PVZP_BUILD_PLUGIN_TESTS)
        add_library(pvzp-plugin-smoke SHARED tests/plugin/smoke.cpp)
        target_link_libraries(pvzp-plugin-smoke PRIVATE pvzp-sdk)
    endif()
    set(_pvzp_sdk "${PROJECT_BINARY_DIR}/sdk/$<CONFIG>")
    file(GENERATE OUTPUT "${_pvzp_sdk}/sdl-include.txt" CONTENT "${SDL2_INCLUDE_DIRS}")
    file(STRINGS "${PROJECT_SOURCE_DIR}/src/PvzpLib/Plugin.h" _pvzp_abi_line REGEX "AbiVersion = [0-9]+;")
    string(REGEX MATCH "AbiVersion = ([0-9]+)" _pvzp_abi_match "${_pvzp_abi_line}")
    set(_pvzp_sdk_abi "${CMAKE_MATCH_1}")
    if(NOT _pvzp_sdk_abi)
        message(FATAL_ERROR "Cannot read the native SDK ABI version")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/PvzpLib/Plugin.h")
    file(GENERATE OUTPUT "${_pvzp_sdk}/build.txt" CONTENT
        "abi=${_pvzp_sdk_abi}\ncompiler=${CMAKE_CXX_COMPILER_ID}\nversion=${CMAKE_CXX_COMPILER_VERSION}\nprofile=$<CONFIG>\npvz_debug=$<BOOL:${PVZ_DEBUG}>\nlow_memory=$<BOOL:${LOW_MEMORY}>\n")
    file(GENERATE OUTPUT "${_pvzp_sdk}/pvzp-sdk.cmake" CONTENT
        "add_library(pvzp-sdk INTERFACE IMPORTED)\nset_target_properties(pvzp-sdk PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/src;\${CMAKE_CURRENT_LIST_DIR}/src/SexyAppFramework;\${CMAKE_CURRENT_LIST_DIR}/src/SexyAppFramework/sound/SDL-Mixer-X/include;${SDL2_INCLUDE_DIRS}\" INTERFACE_LINK_LIBRARIES \"\${CMAKE_CURRENT_LIST_DIR}/lib/$<TARGET_LINKER_FILE_NAME:pvz-portable>\" INTERFACE_COMPILE_FEATURES cxx_std_20 INTERFACE_COMPILE_OPTIONS \"${_pvzp_sdk_options}\" INTERFACE_COMPILE_DEFINITIONS \"$<$<BOOL:${PVZ_DEBUG}>:PVZ_DEBUG>;$<$<BOOL:${LOW_MEMORY}>:LOW_MEMORY>\")\n")
    add_custom_command(TARGET pvz-portable POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DSOURCE_ROOT=${PROJECT_SOURCE_DIR} -DSDK_ROOT=${_pvzp_sdk}
            -DIMPORT_LIBRARY=$<TARGET_LINKER_FILE:pvz-portable> -P "${PROJECT_SOURCE_DIR}/CMake/CopyPluginSdk.cmake"
        VERBATIM)
endif()
