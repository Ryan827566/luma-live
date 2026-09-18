add_library(luma_client_webrtc STATIC src/WebRtcClientService.cpp src/NativeWebRtcPeerConnection.cpp)
target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)
target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

# Optional native WebRTC integration.
# The WebRTC checkout/build is intentionally external to the LumaLive repository.
set(LUMALIVE_WEBRTC_SDK_ROOT "" CACHE PATH "Prebuilt WebRTC SDK root (contains include/ and lib/)")
set(LUMALIVE_WEBRTC_ROOT "" CACHE PATH "WebRTC source/header root")
set(LUMALIVE_WEBRTC_OUT "" CACHE PATH "WebRTC GN output directory")

# Allow the preparation script to provide paths through the current process
# environment, while still allowing explicit CMake cache values to override it.
if(NOT LUMALIVE_WEBRTC_ROOT AND DEFINED ENV{LUMALIVE_WEBRTC_ROOT})
  set(LUMALIVE_WEBRTC_ROOT "$ENV{LUMALIVE_WEBRTC_ROOT}")
endif()
if(NOT LUMALIVE_WEBRTC_OUT AND DEFINED ENV{LUMALIVE_WEBRTC_OUT})
  set(LUMALIVE_WEBRTC_OUT "$ENV{LUMALIVE_WEBRTC_OUT}")
endif()

set(_luma_webrtc_enabled FALSE)

if(WIN32 AND LUMALIVE_WEBRTC_SDK_ROOT)
  set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/lib/libwebrtc.lib")
  if(NOT EXISTS "${_luma_webrtc_lib}")
    set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/libwebrtc.lib")
  endif()

  if(EXISTS "${LUMALIVE_WEBRTC_SDK_ROOT}/include/api/peer_connection_interface.h" AND
     EXISTS "${_luma_webrtc_lib}")
    set(_luma_webrtc_enabled TRUE)
    target_compile_definitions(luma_client_webrtc PUBLIC LUMALIVE_HAS_WEBRTC=1)
    target_include_directories(luma_client_webrtc PUBLIC
      "${LUMALIVE_WEBRTC_SDK_ROOT}/include")
    target_link_libraries(luma_client_webrtc PRIVATE
      "${_luma_webrtc_lib}"
      ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    message(STATUS "LumaLive: using prebuilt WebRTC SDK: ${LUMALIVE_WEBRTC_SDK_ROOT}")
  else()
    message(FATAL_ERROR
      "LUMALIVE_WEBRTC_SDK_ROOT must contain include/api/peer_connection_interface.h "
      "and lib/libwebrtc.lib (or libwebrtc.lib)")
  endif()
endif()

if(WIN32 AND NOT _luma_webrtc_enabled AND
   LUMALIVE_WEBRTC_ROOT AND LUMALIVE_WEBRTC_OUT)
  if(EXISTS "${LUMALIVE_WEBRTC_ROOT}/api/peer_connection_interface.h" AND
     EXISTS "${LUMALIVE_WEBRTC_OUT}/obj/webrtc.lib")
    set(_luma_webrtc_enabled TRUE)
    target_compile_definitions(luma_client_webrtc PUBLIC LUMALIVE_HAS_WEBRTC=1)
    target_include_directories(luma_client_webrtc PUBLIC
      "${LUMALIVE_WEBRTC_ROOT}")
    target_link_libraries(luma_client_webrtc PRIVATE
      "${LUMALIVE_WEBRTC_OUT}/obj/webrtc.lib"
      ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    message(STATUS "LumaLive: using GN WebRTC build: ${LUMALIVE_WEBRTC_OUT}/obj/webrtc.lib")
  else()
    message(FATAL_ERROR
      "LUMALIVE_WEBRTC_ROOT/LUMALIVE_WEBRTC_OUT were supplied but WebRTC headers "
      "or obj/webrtc.lib were not found")
  endif()
endif()

if(NOT _luma_webrtc_enabled)
  message(STATUS "LumaLive: native WebRTC integration disabled; using stub implementation")
endif()

add_executable(luma_client_webrtc_tests tests/WebRtcClientServiceTests.cpp)
target_link_libraries(luma_client_webrtc_tests PRIVATE luma_client_webrtc)
add_test(NAME luma_client_webrtc_tests COMMAND luma_client_webrtc_tests)
