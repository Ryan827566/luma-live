add_library(luma_client_webrtc STATIC
  src/WebRtcClientService.cpp
  src/NativeWebRtcPeerConnection.cpp
)

target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)
target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

if(MSVC)
  set_property(TARGET luma_client_webrtc PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(luma_client_webrtc INTERFACE
    "$<$<CONFIG:Debug>:/MTd>$<$<NOT:$<CONFIG:Debug>>:/MT>")
endif()

# WebRTC is a repository-owned SDK. No absolute developer-machine paths and no
# configure-time network downloads are allowed.
set(LUMALIVE_WEBRTC_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/../third_party/webrtc"
    CACHE PATH "LumaLive bundled WebRTC SDK root")

if(NOT WIN32)
  message(FATAL_ERROR "LumaLive native WebRTC currently supports Windows only.")
endif()

set(LUMALIVE_WEBRTC_INCLUDE_DIR "${LUMALIVE_WEBRTC_SDK_ROOT}/include")

if(CMAKE_CONFIGURATION_TYPES)
  set(LUMALIVE_WEBRTC_LIB_DIR
      "${LUMALIVE_WEBRTC_SDK_ROOT}/release"
      CACHE PATH "WebRTC Release library directory")
else()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LUMALIVE_WEBRTC_LIB_DIR
        "${LUMALIVE_WEBRTC_SDK_ROOT}/debug"
        CACHE PATH "WebRTC Debug library directory")
  else()
    set(LUMALIVE_WEBRTC_LIB_DIR
        "${LUMALIVE_WEBRTC_SDK_ROOT}/release"
        CACHE PATH "WebRTC Release library directory")
  endif()
endif()

set(LUMALIVE_WEBRTC_LIBRARY
    "${LUMALIVE_WEBRTC_LIB_DIR}/webrtc.lib")

if(NOT EXISTS "${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h")
  message(FATAL_ERROR
    "Bundled WebRTC headers were not found at ${LUMALIVE_WEBRTC_INCLUDE_DIR}. "
    "Run scripts/windows/Import-WebRTC-SDK.ps1.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_INCLUDE_DIR}/absl/strings/string_view.h")
  message(FATAL_ERROR
    "Matching Abseil headers were not found under ${LUMALIVE_WEBRTC_INCLUDE_DIR}/absl. "
    "Run scripts/windows/Import-WebRTC-SDK.ps1.")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_LIBRARY}")
  message(FATAL_ERROR
    "Bundled WebRTC library was not found at ${LUMALIVE_WEBRTC_LIBRARY}. "
    "Run scripts/windows/Import-WebRTC-SDK.ps1.")
endif()

target_include_directories(luma_client_webrtc PUBLIC
  "${LUMALIVE_WEBRTC_INCLUDE_DIR}"
)

target_compile_definitions(luma_client_webrtc PUBLIC
  LUMALIVE_HAS_WEBRTC=1
  WEBRTC_WIN=1
)

target_link_libraries(luma_client_webrtc PUBLIC
  "${LUMALIVE_WEBRTC_LIBRARY}"
  ws2_32
  iphlpapi
  secur32
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
