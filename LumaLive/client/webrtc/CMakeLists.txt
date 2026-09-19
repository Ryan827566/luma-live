add_library(luma_client_webrtc STATIC src/WebRtcClientService.cpp src/NativeWebRtcPeerConnection.cpp)
target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)
target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

# If Prepare-WebRTC.ps1 has generated the source-build cache file, load it so
# the user's matching WebRTC checkout/output can be used directly.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../build/webrtc/LumaLiveWebRTC.cmake" OPTIONAL)

# WebRTC is also vendored with the repository for clean-checkout builds.
set(LUMALIVE_WEBRTC_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/../third_party/webrtc/windows-x64"
    CACHE PATH "Vendored WebRTC SDK root (contains include/ and lib/)")

set(_luma_webrtc_enabled FALSE)
set(_luma_webrtc_include "")
set(_luma_webrtc_lib "")

if(WIN32)
  # Prefer a prepared matching source build when both the headers and umbrella
  # library exist; otherwise fall back to the repository-vendored SDK.
  if(DEFINED LUMALIVE_WEBRTC_ROOT AND DEFINED LUMALIVE_WEBRTC_LIB AND
     EXISTS "${LUMALIVE_WEBRTC_ROOT}/api/peer_connection_interface.h" AND
     EXISTS "${LUMALIVE_WEBRTC_LIB}")
    set(_luma_webrtc_include "${LUMALIVE_WEBRTC_ROOT}")
    set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_LIB}")
    message(STATUS "LumaLive: using prepared WebRTC source build: ${LUMALIVE_WEBRTC_ROOT}")
  else()
    set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/lib/webrtc.lib")
    if(NOT EXISTS "${_luma_webrtc_lib}")
      set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/webrtc.lib")
    endif()
    if(EXISTS "${LUMALIVE_WEBRTC_SDK_ROOT}/include/api/peer_connection_interface.h" AND
       EXISTS "${_luma_webrtc_lib}")
      set(_luma_webrtc_include "${LUMALIVE_WEBRTC_SDK_ROOT}/include")
      message(STATUS "LumaLive: using repository WebRTC SDK: ${LUMALIVE_WEBRTC_SDK_ROOT}")
    endif()
  endif()

  if(EXISTS "${_luma_webrtc_include}/api/peer_connection_interface.h" AND
     EXISTS "${_luma_webrtc_lib}")
    set(_luma_webrtc_enabled TRUE)
    target_compile_definitions(luma_client_webrtc PUBLIC
      LUMALIVE_HAS_WEBRTC=1
      WEBRTC_WIN=1
    )
    target_include_directories(luma_client_webrtc PUBLIC
      "${_luma_webrtc_include}")
    if(MSVC)
      target_link_libraries(luma_client_webrtc PUBLIC
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_luma_webrtc_lib}>"
        ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    else()
      target_link_libraries(luma_client_webrtc PUBLIC
        "${_luma_webrtc_lib}"
        ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    endif()
  else()
    message(FATAL_ERROR
      "WebRTC SDK is incomplete. Expected api/peer_connection_interface.h "
      "and a matching webrtc.lib.")
  endif()
endif()

if(NOT _luma_webrtc_enabled)
  message(STATUS "LumaLive: native WebRTC integration disabled; using stub implementation")
endif()

add_executable(luma_client_webrtc_tests tests/WebRtcClientServiceTests.cpp)
target_link_libraries(luma_client_webrtc_tests PRIVATE luma_client_webrtc)
add_test(NAME luma_client_webrtc_tests COMMAND luma_client_webrtc_tests)
