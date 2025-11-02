cmake_minimum_required(VERSION 3.30)

# --------------- #
# Project defines #
# --------------- #

project(vmchroma)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(TARGET_DETOURS lib_detours)
set(TARGET_ADDIMPORT addimport)
set(TARGET_VMCHROMA vmchroma)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_POSTFIX "64")
else ()
    set(ARCH_POSTFIX "32")
endif ()

# ---------------- #
# Compiler defines #
# ---------------- #

add_compile_definitions(
        VMCHROMA_VERSION="vmchroma 0.3.0"
        UNICODE
        ARCH_POSTFIX="${ARCH_POSTFIX}"
        SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

if (MSVC)
    add_compile_options(/utf-8)
endif ()

# --------------------------- #
# External: Microsoft Detours #
# --------------------------- #

FetchContent_Declare(
        detours
        URL https://github.com/microsoft/Detours/archive/9764cebcb1a75940e68fa83d6730ffaf0f669401.zip
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/detours
)
FetchContent_MakeAvailable(detours)
set(DETOURS_SOURCE ${CMAKE_SOURCE_DIR}/external/detours/src)
add_library(${TARGET_DETOURS} STATIC
        ${DETOURS_SOURCE}/creatwth.cpp
        ${DETOURS_SOURCE}/detours.cpp
        ${DETOURS_SOURCE}/detours.h
        ${DETOURS_SOURCE}/detver.h
        ${DETOURS_SOURCE}/disasm.cpp
        ${DETOURS_SOURCE}/disolarm.cpp
        ${DETOURS_SOURCE}/disolarm64.cpp
        ${DETOURS_SOURCE}/disolia64.cpp
        ${DETOURS_SOURCE}/disolx64.cpp
        ${DETOURS_SOURCE}/disolx86.cpp
        ${DETOURS_SOURCE}/image.cpp
        ${DETOURS_SOURCE}/modules.cpp
        ${DETOURS_SOURCE}/uimports.cpp
)
set_property(SOURCE ${DETOURS_SOURCE}/uimports.cpp APPEND PROPERTY HEADER_FILE_ONLY true)
target_compile_options(${TARGET_DETOURS} PRIVATE /W4 /WX /Zi /MT /Gy /Gm- /Zl /Od)
target_include_directories(${TARGET_DETOURS} PUBLIC ${DETOURS_SOURCE})

# ------------------ #
# External: yaml-cpp #
# ------------------ #

FetchContent_Declare(
        yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/2f86d13775d119edbb69af52e5f566fd65c6953b.zip
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/yaml-cpp
)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)
FetchContent_MakeAvailable(yaml-cpp)
set(SPDLOG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/yaml-cpp/include)
set(YAML_CPP_BUILD_FLAGS /MT)
target_compile_options(yaml-cpp PRIVATE /W0)

# ------------------ #
# External: spdlog   #
# ------------------ #

FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/spdlog
)
FetchContent_MakeAvailable(spdlog)
set(SPDLOG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/external/spdlog/include)

# -------------------------- #
# Target: vmchroma[32|64].dll #
# -------------------------- #

set(VMCHROMA_SOURCES
        src/vmchroma/vmchroma.cpp
        src/vmchroma/utils.hpp
        src/vmchroma/utils.cpp
        src/vmchroma/winapi_hook_defs.hpp
        src/vmchroma/window_manager.cpp
        src/vmchroma/window_manager.hpp
        src/vmchroma/config_manager.cpp
        src/vmchroma/config_manager.hpp
)

if (EXISTS "${CMAKE_SOURCE_DIR}/src/vmchroma/vmchroma.rc")
    list(APPEND VMCHROMA_SOURCES src/vmchroma/vmchroma.rc)
endif ()

add_library(${TARGET_VMCHROMA} SHARED ${VMCHROMA_SOURCES})
target_include_directories(${TARGET_VMCHROMA} PUBLIC ${DETOURS_SOURCE})
target_include_directories(${TARGET_VMCHROMA} PUBLIC ${SPDLOG_INCLUDE_DIR})
target_link_libraries(${TARGET_VMCHROMA} PRIVATE
        lib_detours
        yaml-cpp::yaml-cpp
        Msimg32
        Shlwapi
        d2d1
        d3d11
        dxgi
        Version
        advapi32
        windowsapp
        ole32
)
set_target_properties(${TARGET_VMCHROMA} PROPERTIES
        LINK_FLAGS "/EXPORT:dummy_export,@1"
        OUTPUT_NAME "vmchroma${ARCH_POSTFIX}"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/out
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/out
)

# ---------------------------- #
# Target: addimport[32|64].exe #
# ---------------------------- #

set(ADDIMPORT_SOURCES src/addimport/addimport.cpp)

if (EXISTS "${CMAKE_SOURCE_DIR}/src/addimport/addimport.rc")
    list(APPEND ADDIMPORT_SOURCES src/addimport/addimport.rc)
endif ()

add_executable(${TARGET_ADDIMPORT} ${ADDIMPORT_SOURCES})
target_compile_options(${TARGET_ADDIMPORT} PRIVATE /MD)
target_include_directories(${TARGET_ADDIMPORT} PUBLIC ${DETOURS_SOURCE})
target_link_libraries(${TARGET_ADDIMPORT} PRIVATE
        lib_detours
)
set_target_properties(${TARGET_ADDIMPORT} PROPERTIES
        OUTPUT_NAME "addimport${ARCH_POSTFIX}"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/out
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/out
)

# --------------------------------- #
# Target: copy vmchroma_patcher.ps1 #
# --------------------------------- #

set(PATCHER_SOURCE_FILE "${CMAKE_SOURCE_DIR}/src/vmchroma_patcher.ps1")
set(PATCHER_DEST_FILE "${CMAKE_SOURCE_DIR}/out/vmchroma_patcher.ps1")
add_custom_command(
        OUTPUT ${PATCHER_DEST_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy ${PATCHER_SOURCE_FILE} ${PATCHER_DEST_FILE}
        DEPENDS ${PATCHER_SOURCE_FILE}
)
add_custom_target(CopyPatcher ALL DEPENDS ${PATCHER_DEST_FILE})

# -------------------------- #
# Target: copy vmchroma.yaml #
# -------------------------- #

set(CONFIG_SOURCE_FILE "${CMAKE_SOURCE_DIR}/src/vmchroma.yaml")
set(CONFIG_DEST_FILE "${CMAKE_SOURCE_DIR}/out/vmchroma.yaml")
add_custom_command(
        OUTPUT ${CONFIG_DEST_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy ${CONFIG_SOURCE_FILE} ${CONFIG_DEST_FILE}
        DEPENDS ${CONFIG_SOURCE_FILE}
)
add_custom_target(CopyConfig ALL DEPENDS ${CONFIG_DEST_FILE})

# ------------------------------------------------- #
# Target (debug): copy DLLs to Voicemeeter dir      #
# ------------------------------------------------- #
set(VOICEMEETER_INSTALL_DIR "C:/Program Files (x86)/VB/Voicemeeter")
add_custom_target(copy_dlls_for_debugging
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:vmchroma> "${VOICEMEETER_INSTALL_DIR}/vmchroma${ARCH_POSTFIX}.dll"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:vmchroma> "${VOICEMEETER_INSTALL_DIR}/vmchroma${ARCH_POSTFIX}.pdb"
        DEPENDS vmchroma
)
