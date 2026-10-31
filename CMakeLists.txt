cmake_minimum_required(VERSION 3.16)

project(Gothic2Notr LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/opengothic)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/opengothic)
set(CMAKE_DEBUG_POSTFIX "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
  add_definitions(-D__MOBILE_PLATFORM__=1 -DOG_WEB_UI=1)
  # Tempest sets this for its own directory only: without it mkApi() (game/main.cpp) has no graphics api
  add_definitions(-DTEMPEST_BUILD_VULKAN)
  add_library(${PROJECT_NAME} SHARED)
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "opengothic")
elseif(EMSCRIPTEN)
  add_definitions(-D__WEB_PLATFORM__=1 -DOG_WEB_UI=1)
  add_executable(${PROJECT_NAME})
  set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "opengothic")
  set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")
  # native wasm exceptions: JS-emulated ones route every try-block call through JS
  # bulk memory: memcpy/memset become memory.copy/fill instead of calls into JS;
  # nontrapping-fptoint: float->int casts are single instructions
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -msimd128 -mbulk-memory -mnontrapping-fptoint -Wno-unused-command-line-argument")
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -sSUPPORT_LONGJMP=wasm -msimd128 -mbulk-memory -mnontrapping-fptoint")
  # threads: npc animation runs on Web Workers. Needs a cross-origin isolated
  # page (COOP/COEP headers), as SharedArrayBuffer does.
  option(OPENGOTHIC_WEB_THREADS "Web build with threads (Web Workers)" ON)
  if(OPENGOTHIC_WEB_THREADS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -pthread")
    target_link_options(${PROJECT_NAME} PRIVATE
      "-pthread"
      "-sPTHREAD_POOL_SIZE=Math.max(1,Math.min(16,(navigator.hardwareConcurrency||4)-1))+1"
      "--pre-js=${CMAKE_SOURCE_DIR}/web/threads_check.js"
    )
    # JS reads of the heap check for growth: the hot WebGPU calls are bundled.
    # Must come after -Wall on the link line
    target_link_libraries(${PROJECT_NAME} "-Wno-pthreads-mem-growth")
  endif()
  target_link_options(${PROJECT_NAME} PRIVATE
    "-sUSE_WEBGPU=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sMAXIMUM_MEMORY=4GB"
    "-sSTACK_SIZE=5MB"
    "-sNO_EXIT_RUNTIME=1"
    "-fwasm-exceptions"
    "-sSUPPORT_LONGJMP=wasm"
    "-mbulk-memory"
    "-mnontrapping-fptoint"
    "--profiling-funcs"
    "-lidbfs.js"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS','callMain','mmapAlloc']"
    "-sFORCE_FILESYSTEM=1"
    "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
    "--js-library=${CMAKE_SOURCE_DIR}/web/webgpu_fast.js"
  )
  # word-wide memcmp in place of musl's byte loop (-fno-builtin: the loop must
  # not be turned back into a memcmp call)
  target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/web/memcmp.c")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/web/memcmp.c" PROPERTIES COMPILE_OPTIONS "-fno-builtin")
  # relink when the html shell changes
  set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html;${CMAKE_SOURCE_DIR}/web/webgpu_fast.js")
else()
  add_executable(${PROJECT_NAME})
endif()

if(MSVC)
  add_definitions(-D_USE_MATH_DEFINES)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
  add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4800")
endif()

if(NOT MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wconversion -Wno-strict-aliasing )
  if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-class-memaccess)
  endif()
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "7.1" AND NOT APPLE AND NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-format-truncation)
  endif()
  if(UNIX)
    # -rdynamic is needed to allow obtaining meaningful backtraces
    target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU") # gcc
    # required by pthread_setname_np()
    add_definitions(-D_GNU_SOURCE)
  endif()
endif()

if(WIN32)
  add_definitions(-DNOMINMAX)
  if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup" )
  elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows -lpthread")
  else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows -lstdc++fs")
  endif()
elseif(IOS)
  enable_language(OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
  set_directory_properties("${CMAKE_SOURCE_DIR}" PROPERTIES
    PROPERTY XCODE_STARTUP_PROJECT "${PROJECT_NAME}")
  set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "opengothic.gothic2"
    MACOSX_BUNDLE_BUNDLE_VERSION 1
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer")
elseif(APPLE)
  enable_language(OBJCXX)
endif()

if(IOS)
  file(GLOB_RECURSE ObjCSOURCES "game/*.mm")
elseif(APPLE)
  target_link_libraries(${PROJECT_NAME} "-framework AppKit")
  file(GLOB_RECURSE ObjCSOURCES "game/*.mm")
endif()

## OpenGothic

# sources
file(GLOB_RECURSE OPENGOTHIC_SOURCES
    "game/*.h"
    "game/*.cpp")

target_sources(${PROJECT_NAME} PRIVATE ${OPENGOTHIC_SOURCES} ${ObjCSOURCES} icon.rc)

# shaders
add_subdirectory(shader)
target_link_libraries(${PROJECT_NAME} GothicShaders)

include_directories("game")

## submodule dependencies

# edd-dbg
if(WIN32)
  add_subdirectory(lib/edd-dbg)
  include_directories(lib/edd-dbg/include)
  target_link_libraries(${PROJECT_NAME} edd_dbg)
endif()

if(WIN32)
  target_link_libraries(${PROJECT_NAME} shlwapi DbgHelp)
elseif(ANDROID)
  target_link_libraries(${PROJECT_NAME} android log)
elseif(EMSCRIPTEN)
  # Emscripten provides built-in browser runtime and audio
elseif(UNIX)
  target_link_libraries(${PROJECT_NAME} -lpthread -ldl)
endif()

# ZenKit
set(BUILD_SQUISH_WITH_SSE2     OFF CACHE INTERNAL "")
set(ZK_ENABLE_ASAN             OFF CACHE INTERNAL "")
set(ZK_ENABLE_DEPRECATION      OFF CACHE INTERNAL "")

add_subdirectory(lib/ZenKit)
target_link_libraries(${PROJECT_NAME} zenkit)

# dmusic
set(DM_BUILD_STATIC            ON  CACHE INTERNAL "")
set(DM_ENABLE_ASAN             OFF CACHE INTERNAL "")

add_subdirectory(lib/dmusic)
target_link_libraries(${PROJECT_NAME} dmusic)

# opus (compressed speech/sfx in repacked archives)
set(OPUS_INSTALL_PKG_CONFIG_MODULE   OFF CACHE INTERNAL "")
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE INTERNAL "")
add_subdirectory(lib/opus EXCLUDE_FROM_ALL)
target_link_libraries(${PROJECT_NAME} opus)

# TinySoundFont
add_definitions(-DTSF_NO_STDIO)
include_directories(lib/TinySoundFont)

# Tempest
if(WIN32)
  set(TEMPEST_BUILD_SHARED ON  CACHE INTERNAL "")
else()
  set(TEMPEST_BUILD_SHARED OFF CACHE INTERNAL "")
endif()
add_subdirectory(lib/Tempest/Engine)
include_directories(lib/Tempest/Engine/include)
include_directories(lib/Tempest/Engine)
target_link_libraries(${PROJECT_NAME} Tempest)

# zip
include_directories(lib/miniz)
add_library(miniz STATIC "lib/miniz/miniz.h" "lib/miniz/miniz.c")
target_link_libraries(${PROJECT_NAME} miniz)

# bullet physics
set(BULLET2_MULTITHREADING       ON  CACHE INTERNAL "")
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON  CACHE INTERNAL "")
set(USE_GRAPHICAL_BENCHMARK      OFF CACHE INTERNAL "")
set(BUILD_BULLET2_DEMOS          OFF CACHE INTERNAL "") # No samples
set(BUILD_OPENGL3_DEMOS          OFF CACHE INTERNAL "") # No samples
set(BUILD_UNIT_TESTS             OFF CACHE INTERNAL "") # No tests
set(BUILD_CPU_DEMOS              OFF CACHE INTERNAL "") # No samples
set(BUILD_EXTRAS                 OFF CACHE INTERNAL "") # No bugs
set(GLFW_BUILD_EXAMPLES          OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS             OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS              OFF CACHE INTERNAL "")
set(BUILD_BULLET3                OFF CACHE INTERNAL "") # Can use bullet2, bullet3 wants to build examples...
add_subdirectory(lib/bullet3)
if(NOT MSVC)
  target_compile_options(BulletDynamics         PRIVATE -Wno-argument-outside-range)
  target_compile_options(BulletInverseDynamics  PRIVATE -Wno-argument-outside-range)
  target_compile_options(BulletSoftBody         PRIVATE -Wno-argument-outside-range)
  target_compile_options(Bullet3Common          PRIVATE -Wno-argument-outside-range)
  target_compile_options(BulletCollision        PRIVATE -Wno-argument-outside-range)
  target_compile_options(LinearMath             PRIVATE -Wno-argument-outside-range)
endif()
if(NOT MSVC AND NOT APPLE AND NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
  target_compile_options(BulletDynamics         PRIVATE -Wno-stringop-overflow)
  target_compile_options(BulletInverseDynamics  PRIVATE -Wno-stringop-overflow)
  target_compile_options(BulletSoftBody         PRIVATE -Wno-stringop-overflow)
  target_compile_options(Bullet3Common          PRIVATE -Wno-stringop-overflow)
  target_compile_options(BulletCollision        PRIVATE -Wno-stringop-overflow)
  target_compile_options(LinearMath             PRIVATE -Wno-stringop-overflow)
endif()

include_directories(lib/bullet3/src)
target_link_libraries(${PROJECT_NAME} BulletDynamics BulletCollision LinearMath)

# script for launching in binary directory
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/scripts/Gothic2Notr.bat
        ${CMAKE_CURRENT_BINARY_DIR}/opengothic/Gothic2Notr.bat)
endif()

# in debug mode, enable sanitizers
if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
  add_compile_options(-fsanitize=address)
  add_compile_options(-fsanitize=leak)
  add_link_options   (-fsanitize=address)
  add_link_options   (-fsanitize=leak)
endif()

# installation
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
