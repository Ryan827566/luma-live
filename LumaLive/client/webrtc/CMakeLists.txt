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
# 运行库统一交给顶层 CMAKE_MSVC_RUNTIME_LIBRARY（/MT）决定。
# 预编译 webrtc.lib 是 /MT + _ITERATOR_DEBUG_LEVEL=0 + NDEBUG 编译的，
# 所以本库和依赖它的目标都必须对齐 /MT，否则会报 LNK2038 / LNK2019。
# 不要在此处硬编码 /MTd，也不要通过 INTERFACE 传染给依赖者。
# ---------------------------------------------------------------------------

# WebRTC is a repository-owned SDK. No absolute developer-machine paths and no
# configure-time network downloads are allowed.
set(LUMALIVE_WEBRTC_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/../third_party/webrtc"
    CACHE PATH "LumaLive bundled WebRTC SDK root")

if(NOT WIN32)
  message(FATAL_ERROR "LumaLive native WebRTC currently supports Windows only.")
endif()

set(LUMALIVE_WEBRTC_INCLUDE_DIR "${LUMALIVE_WEBRTC_SDK_ROOT}/include")

# 预编译包只有一个 webrtc.lib（内部就是 /MT），Debug/Release 都用它。
set(LUMALIVE_WEBRTC_LIB_DIR
    "${LUMALIVE_WEBRTC_SDK_ROOT}/release"
    CACHE PATH "WebRTC library directory")

set(LUMALIVE_WEBRTC_LIBRARY "${LUMALIVE_WEBRTC_LIB_DIR}/webrtc.lib")

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
add_executable(luma_webrtc_media_loopback_tests tests/WebRtcMediaLoopbackTests.cpp)
target_link_libraries(luma_webrtc_media_loopback_tests PRIVATE luma_client_webrtc)
add_test(NAME luma_webrtc_media_loopback_tests COMMAND luma_webrtc_media_loopback_tests)
set_tests_properties(luma_webrtc_media_loopback_tests PROPERTIES TIMEOUT 30)
