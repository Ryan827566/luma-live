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
# LumaLive bundled WebRTC
#
# WebRTC is now part of the LumaLive repository:
#
#   third_party/
#     webrtc/
#       include/
#       windows-x64/
#         lib/
#
# No external WebRTC checkout or LumaLive_Environment_Installer is required.
# ---------------------------------------------------------------------------

set(LUMALIVE_WEBRTC_ROOT
    "${CMAKE_SOURCE_DIR}/../third_party/webrtc"
    CACHE PATH "LumaLive bundled WebRTC root")

set(LUMALIVE_WEBRTC_INCLUDE_DIR
    "${LUMALIVE_WEBRTC_ROOT}/include")

set(LUMALIVE_WEBRTC_LIB_DIR
    "${LUMALIVE_WEBRTC_ROOT}/windows-x64/lib")

set(LUMALIVE_WEBRTC_LIBRARY
    "${LUMALIVE_WEBRTC_LIB_DIR}/webrtc.lib")

# ---------------------------------------------------------------------------
# Validate bundled WebRTC
# ---------------------------------------------------------------------------

if(NOT WIN32)
  message(FATAL_ERROR
    "LumaLive native WebRTC currently supports Windows only.")
endif()

if(NOT EXISTS
    "${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h")
  message(FATAL_ERROR
    "Bundled WebRTC headers were not found:\n"
    "  ${LUMALIVE_WEBRTC_INCLUDE_DIR}\n"
    "Expected:\n"
    "  ${LUMALIVE_WEBRTC_INCLUDE_DIR}/api/peer_connection_interface.h")
endif()

if(NOT EXISTS "${LUMALIVE_WEBRTC_LIBRARY}")
  message(FATAL_ERROR
    "Bundled WebRTC library was not found:\n"
    "  ${LUMALIVE_WEBRTC_LIBRARY}\n"
    "Expected the locally built WebRTC library to be migrated into:\n"
    "  ${LUMALIVE_WEBRTC_LIB_DIR}")
endif()

message(STATUS "LumaLive: using bundled WebRTC")
message(STATUS "  WebRTC root   : ${LUMALIVE_WEBRTC_ROOT}")
message(STATUS "  WebRTC include: ${LUMALIVE_WEBRTC_INCLUDE_DIR}")
message(STATUS "  WebRTC library: ${LUMALIVE_WEBRTC_LIBRARY}")

# ---------------------------------------------------------------------------
# WebRTC compile definitions
# ---------------------------------------------------------------------------

target_compile_definitions(luma_client_webrtc PUBLIC
  LUMALIVE_HAS_WEBRTC=1
  WEBRTC_WIN=1
)

target_include_directories(luma_client_webrtc PUBLIC
  "${LUMALIVE_WEBRTC_INCLUDE_DIR}"
)

# ---------------------------------------------------------------------------
# WebRTC libraries
#
# The main umbrella library is currently webrtc.lib.
# Additional WebRTC static libraries will be added to this directory during
# the migration of the locally compiled WebRTC dependency set.
# ---------------------------------------------------------------------------

if(MSVC)
  target_link_libraries(luma_client_webrtc PUBLIC
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,${LUMALIVE_WEBRTC_LIBRARY}>"
    ws2_32
    winmm
    dmoguids
    msdmo
    wmcodecdspuuid
  )
else()
  target_link_libraries(luma_client_webrtc PUBLIC
    "${LUMALIVE_WEBRTC_LIBRARY}"
    ws2_32
    winmm
    dmoguids
    msdmo
    wmcodecdspuuid
  )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

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