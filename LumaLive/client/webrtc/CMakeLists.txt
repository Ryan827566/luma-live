add_library(luma_client_webrtc STATIC
  src/WebRtcClientService.cpp
  src/NativeWebRtcPeerConnection.cpp
)

target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)
target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

# WebRTC must be built with use_custom_libcxx=false because LumaLive is built
# with MSVC STL. A library containing std::__Cr symbols is a Chromium-libc++
# build and is not ABI-compatible with this application.
set(LUMALIVE_WEBRTC_SDK_ROOT "" CACHE PATH "Complete LumaLive WebRTC SDK root")
if(LUMALIVE_WEBRTC_SDK_ROOT)
  set(LUMALIVE_WEBRTC_ROOT "${LUMALIVE_WEBRTC_SDK_ROOT}")
else()
  set(LUMALIVE_WEBRTC_ROOT "${CMAKE_SOURCE_DIR}/../third_party/webrtc"
      CACHE PATH "LumaLive bundled WebRTC root")
endif()

set(LUMALIVE_WEBRTC_INCLUDE_DIR "${LUMALIVE_WEBRTC_ROOT}/include")
set(LUMALIVE_WEBRTC_LIB_DIR "${LUMALIVE_WEBRTC_ROOT}/windows-x64/lib")
set(LUMALIVE_WEBRTC_LIBRARY "${LUMALIVE_WEBRTC_LIB_DIR}/webrtc.lib")

if(NOT WIN32)
  message(FATAL_ERROR "LumaLive native WebRTC currently supports Windows only.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h")
  message(FATAL_ERROR
    "Bundled WebRTC headers were not found at ${LUMALIVE_WEBRTC_INCLUDE_DIR}. "
    "Run Build-WebRTC-MSVC.ps1 and Import-WebRTC-SDK.ps1.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_LIBRARY}")
  message(FATAL_ERROR
    "Bundled WebRTC library was not found at ${LUMALIVE_WEBRTC_LIBRARY}. "
    "Run Build-WebRTC-MSVC.ps1 and Import-WebRTC-SDK.ps1.")
endif()

# Older SDKs in this repository do not contain Abseil headers. Use a pinned
# header source only until Import-WebRTC-SDK.ps1 replaces it with the exact
# headers from the WebRTC checkout. No Abseil binary is linked here.
if(NOT EXISTS "${LUMALIVE_WEBRTC_INCLUDE_DIR}/absl/strings/string_view.h")
  include(FetchContent)
  FetchContent_Declare(
    lumalive_abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20260107.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(lumalive_abseil)
  target_include_directories(luma_client_webrtc PUBLIC "${lumalive_abseil_SOURCE_DIR}")
else()
  target_include_directories(luma_client_webrtc PUBLIC "${LUMALIVE_WEBRTC_INCLUDE_DIR}")
endif()

target_compile_definitions(luma_client_webrtc PUBLIC
  LUMALIVE_HAS_WEBRTC=1
  WEBRTC_WIN=1
)

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
target_link_libraries(luma_client_webrtc_tests PRIVATE luma_client_webrtc)
add_test(NAME luma_client_webrtc_tests COMMAND luma_client_webrtc_tests)
