add_library(luma_client_webrtc STATIC src/WebRtcClientService.cpp src/NativeWebRtcPeerConnection.cpp)
target_include_directories(luma_client_webrtc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../media-pipeline/include
)
target_compile_features(luma_client_webrtc PUBLIC cxx_std_20)

# WebRTC is vendored with the LumaLive repository so a clean checkout does not
# depend on a developer-specific path outside the repository.
set(LUMALIVE_WEBRTC_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/../third_party/webrtc/windows-x64"
    CACHE PATH "Vendored WebRTC SDK root (contains include/ and lib/)")

set(_luma_webrtc_enabled FALSE)

if(WIN32)
  set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/lib/webrtc.lib")
  if(NOT EXISTS "${_luma_webrtc_lib}")
    set(_luma_webrtc_lib "${LUMALIVE_WEBRTC_SDK_ROOT}/webrtc.lib")
  endif()

  if(EXISTS "${LUMALIVE_WEBRTC_SDK_ROOT}/include/api/peer_connection_interface.h" AND
     EXISTS "${_luma_webrtc_lib}")
    set(_luma_webrtc_enabled TRUE)
    target_compile_definitions(luma_client_webrtc PUBLIC
      LUMALIVE_HAS_WEBRTC=1
      WEBRTC_WIN=1
    )
    target_include_directories(luma_client_webrtc PUBLIC
      "${LUMALIVE_WEBRTC_SDK_ROOT}/include")
    if(MSVC)
      target_link_libraries(luma_client_webrtc PUBLIC
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_luma_webrtc_lib}>"
        ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    else()
      target_link_libraries(luma_client_webrtc PUBLIC
        "${_luma_webrtc_lib}"
        ws2_32 winmm dmoguids msdmo wmcodecdspuuid)
    endif()
    message(STATUS "LumaLive: using repository WebRTC SDK: ${LUMALIVE_WEBRTC_SDK_ROOT}")
  else()
    message(FATAL_ERROR
      "Vendored WebRTC SDK is incomplete. Expected "
      "${LUMALIVE_WEBRTC_SDK_ROOT}/include/api/peer_connection_interface.h "
      "and ${LUMALIVE_WEBRTC_SDK_ROOT}/lib/webrtc.lib")
  endif()
endif()

if(NOT _luma_webrtc_enabled)
  message(STATUS "LumaLive: native WebRTC integration disabled; using stub implementation")
endif()

add_executable(luma_client_webrtc_tests tests/WebRtcClientServiceTests.cpp)
target_link_libraries(luma_client_webrtc_tests PRIVATE luma_client_webrtc)
add_test(NAME luma_client_webrtc_tests COMMAND luma_client_webrtc_tests)
