cmake_minimum_required(VERSION 3.24)
project(LumaLive VERSION 1.0.0 LANGUAGES CXX)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(LUMALIVE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../output" CACHE PATH "Directory for final LumaLive executables, DLLs and LIB files")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${LUMALIVE_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${LUMALIVE_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${LUMALIVE_OUTPUT_DIR}")
foreach(CONFIG Debug Release RelWithDebInfo MinSizeRel)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${LUMALIVE_OUTPUT_DIR}/${CONFIG}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG} "${LUMALIVE_OUTPUT_DIR}/${CONFIG}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG} "${LUMALIVE_OUTPUT_DIR}/${CONFIG}")
endforeach()
enable_testing()
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
  add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
  add_compile_definitions($<$<CONFIG:Debug>:NDEBUG>)
  add_compile_options(/W4 /permissive- /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(shared)
add_subdirectory(apps/luma-studio)
add_subdirectory(apps/luma-admin)
add_subdirectory(client/application/studio)
foreach(MOD
 client/platform client/domain/studio client/domain/effects client/domain/media client/domain/audio
 client/connect client/ai client/application client/ui
 ai/core ai/gateway ai/model-router ai/orchestrator ai/features ai/tool-registry ai/memory ai/rag ai/safety ai/agents ai/content
 server/api-gateway server/auth server/live server/chat server/im server/signaling server/sfu server/ai-gateway
 server/moderation server/media server/analytics server/storage
 adapters/model adapters/database adapters/storage adapters/encoder adapters/transport)
  add_subdirectory(${MOD})
endforeach()
set(LUMALIVE_PHASE3_15_MODULES
 client/account client/media-pipeline client/webrtc client/signaling client/live client/chat client/social client/interaction client/alert client/sfu client/im client/studio-copilot client/ai-director client/ai-host client/ai-chat client/ai-moderation client/ai-subtitles client/ai-vision client/ai-editor client/analytics client/installer
 server/database server/cache server/message-queue server/search server/vector server/observability server/rtc server/identity server/room server/notification server/content server/rate-limit server/analytics-pipeline server/admin-api
 admin/gateway admin/auth admin/authorization admin/users admin/live admin/moderation admin/ai admin/config admin/analytics admin/audit
 ai/translation ai/asr ai/tts ai/vision ai/effect-agent ai/editor ai/analytics ai/guardrails ai/budget ai/task ai/providers ai/memory-rag
 adapters/http adapters/websocket adapters/webrtc adapters/stun-turn adapters/sfu adapters/postgresql adapters/redis adapters/nats adapters/opensearch adapters/pgvector adapters/s3 adapters/opentelemetry adapters/prometheus adapters/asr adapters/tts adapters/translation adapters/vision adapters/model-openai adapters/model-anthropic adapters/model-deepseek adapters/model-ollama)
foreach(MOD IN LISTS LUMALIVE_PHASE3_15_MODULES)
  add_subdirectory(${MOD})
endforeach()
add_subdirectory(adapters/registry)
add_subdirectory(apps/luma-publisher)
add_subdirectory(apps/luma-signaling-server)
