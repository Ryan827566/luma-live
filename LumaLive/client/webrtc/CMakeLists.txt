add_library(luma_client_webrtc STATIC
  src/WebRtcClientService.cpp
  src/NativeWebRtcPeerConnection.cpp
)

target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)

target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# Bundled WebRTC SDK
#
# The SDK is built with WebRTC/Chromium clang-cl but with
# use_custom_libcxx=false. This deliberately uses the MSVC C++ standard
# library ABI so it can be consumed by the MSVC-built LumaLive application.
# Do not mix this with a WebRTC binary built against Chromium's custom libc++.
# ---------------------------------------------------------------------------

set(LUMALIVE_WEBRTC_SDK_ROOT "" CACHE PATH
    "Complete LumaLive WebRTC SDK root")
if(LUMALIVE_WEBRTC_SDK_ROOT)
  set(LUMALIVE_WEBRTC_ROOT "${LUMALIVE_WEBRTC_SDK_ROOT}")
else()
  set(LUMALIVE_WEBRTC_ROOT
      "${CMAKE_SOURCE_DIR}/../third_party/webrtc"
      CACHE PATH "LumaLive bundled WebRTC root")
endif()

set(LUMALIVE_WEBRTC_INCLUDE_DIR "${LUMALIVE_WEBRTC_ROOT}/include")
set(LUMALIVE_WEBRTC_LIB_DIR "${LUMALIVE_WEBRTC_ROOT}/windows-x64/lib")
set(LUMALIVE_WEBRTC_LIBRARY "${LUMALIVE_WEBRTC_LIB_DIR}/webrtc.lib")

if(NOT WIN32)
  message(FATAL_ERROR
    "LumaLive native WebRTC currently supports Windows only.")
endif()

if(NOT EXISTS
    "${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h")
  message(FATAL_ERROR
    "Bundled WebRTC headers were not found:\n"
    "  ${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h\n"
    "Run scripts/windows/Build-WebRTC-MSVC.ps1 followed by "
    "scripts/windows/Import-WebRTC-SDK.ps1.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_INCLUDE_DIR}/absl/strings/string_view.h")
  message(FATAL_ERROR
    "Bundled WebRTC Abseil headers were not found:\n"
    "  ${LUMALIVE_WEBRTC_INCLUDE_DIR}/absl/strings/string_view.h\n"
    "The SDK must contain the exact Abseil headers used by its WebRTC source tree.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_LIBRARY}")
  message(FATAL_ERROR
    "Bundled WebRTC library was not found:\n"
    "  ${LUMALIVE_WEBRTC_LIBRARY}\n"
    "Run scripts/windows/Build-WebRTC-MSVC.ps1 followed by "
    "scripts/windows/Import-WebRTC-SDK.ps1.")
endif()

message(STATUS "LumaLive: using bundled WebRTC SDK")
message(STATUS "  WebRTC root   : ${LUMALIVE_WEBRTC_ROOT}")
message(STATUS "  WebRTC include: ${LUMALIVE_WEBRTC_INCLUDE_DIR}")
message(STATUS "  WebRTC library: ${LUMALIVE_WEBRTC_LIBRARY}")

target_compile_definitions(luma_client_webrtc PUBLIC
  LUMALIVE_HAS_WEBRTC=1
  WEBRTC_WIN=1
)

target_include_directories(luma_client_webrtc PUBLIC
  "${LUMALIVE_WEBRTC_INCLUDE_DIR}"
)

# Do not use WHOLE_ARCHIVE here. The standalone WebRTC target is already a
# complete static library; forcing every object into the final link can expose
# unrelated implementation objects and makes ABI/runtime diagnostics harder.

target_link_libraries(luma_client_webrtc PUBLIC
  "${LUMALIVE_WEBRTC_LIBRARY}"
  ws2_32
  winmm
  dmoguids
  msdmo
  wmcodecdspuuid
)

add_executable(luma_client_webrtc_tests
  tests/WebRtcClientServiceTests.cpp
)

target_link_libraries(luma_client_webrtc_tests PRIVATE
  luma_client_webrtc
)

add_test(
  NAME luma_client_webrtc_tests
  COMMAND luma_client_webrtc_tests
)
