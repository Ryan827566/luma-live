#pragma once
#include "UiLocale.hpp"
namespace luma::client::ui::preview {
enum class AiMessage {SpeechLimit,Off,Enabled,Stopped,AudioFormat,Dropped,Translating,VoicePending,SummaryPending,KeywordsPending,VoiceReady,TranslationReady,SummaryReady,KeywordsReady,Transcribing,Limit,RequestFailed,NoTranscript,NoSummary,Busy};
inline std::string AiMessageText(AiMessage message){switch(message){
case AiMessage::SpeechLimit:return UiUtf8(UiLabel(L"\u6458\u8981\u8fc7\u957f\uff0c\u672c\u6b21\u65e0\u6cd5\u6717\u8bfb\u3002\u5b57\u5e55\u8bb0\u5f55\u4ecd\u53ef\u7ee7\u7eed\u3002",L"Summary is too long to read aloud. Caption collection can continue."));
case AiMessage::Off:return UiUtf8(UiLabel(L"AI \u5df2\u5173\u95ed\u3002\u8bf7\u5148\u914d\u7f6e\u5e76\u542f\u52a8\u672c\u5730 AI \u670d\u52a1\u3002",L"AI is off. Configure and start the local AI gateway before enabling."));
case AiMessage::Enabled:return UiUtf8(UiLabel(L"\u5b57\u5e55\u5df2\u542f\u7528\uff0c\u97f3\u9891\u5c06\u53d1\u9001\u81f3\u5df2\u914d\u7f6e\u7684\u670d\u52a1\u3002",L"Transcription enabled; audio is sent to the configured service."));
case AiMessage::Stopped:return UiUtf8(UiLabel(L"\u5b57\u5e55\u5df2\u505c\u6b62\u3002",L"Transcription stopped."));
case AiMessage::AudioFormat:return UiUtf8(UiLabel(L"AI \u9700\u8981 48 kHz \u5355\u58f0\u9053 16 \u4f4d\u97f3\u9891\u3002",L"AI requires 48 kHz mono S16 audio."));
case AiMessage::Dropped:return UiUtf8(UiLabel(L"AI \u5904\u7406\u7e41\u5fd9\uff0c\u5df2\u8df3\u8fc7\u4e00\u6bb5\u97f3\u9891\u3002",L"AI is busy; an audio segment was dropped."));
case AiMessage::Translating:return UiUtf8(UiLabel(L"\u6b63\u5728\u7ffb\u8bd1\u2026",L"Translating..."));
case AiMessage::VoicePending:return UiUtf8(UiLabel(L"\u6b63\u5728\u751f\u6210 AI \u8bed\u97f3\u2026",L"Generating AI voice..."));
case AiMessage::SummaryPending:return UiUtf8(UiLabel(L"\u6b63\u5728\u751f\u6210\u6458\u8981\u2026",L"Generating summary..."));
case AiMessage::KeywordsPending:return UiUtf8(UiLabel(L"\u6b63\u5728\u63d0\u53d6\u5173\u952e\u8bcd\u2026",L"Extracting keywords..."));
case AiMessage::VoiceReady:return UiUtf8(UiLabel(L"AI \u751f\u6210\u7684\u8bed\u97f3\u5df2\u5c31\u7eea\u3002",L"AI-generated voice ready."));
case AiMessage::TranslationReady:return UiUtf8(UiLabel(L"\u7ffb\u8bd1\u5df2\u5b8c\u6210\uff0c\u8bf7\u6838\u5bf9 AI \u7ed3\u679c\u3002",L"Translation ready. Verify AI output."));
case AiMessage::SummaryReady:return UiUtf8(UiLabel(L"\u6458\u8981\u5df2\u5b8c\u6210\uff0c\u8bf7\u5bf9\u7167\u8bb0\u5f55\u6838\u5bf9 AI \u7ed3\u679c\u3002",L"Summary ready. Verify AI output against the transcript."));
case AiMessage::KeywordsReady:return UiUtf8(UiLabel(L"\u5173\u952e\u8bcd\u5df2\u63d0\u53d6\uff0c\u8bf7\u6838\u5bf9 AI \u7ed3\u679c\u3002",L"Keywords ready. Verify AI output."));
case AiMessage::Transcribing:return UiUtf8(UiLabel(L"\u6b63\u5728\u8f6c\u5199\uff08\u6bcf\u4e94\u79d2\u4e00\u6bb5\uff09\u2026",L"Transcribing (five-second segments)..."));
case AiMessage::Limit:return UiUtf8(UiLabel(L"\u8bb0\u5f55\u5df2\u8fbe\u4e0a\u9650\uff0c\u8bf7\u5148\u590d\u5236\u4fdd\u5b58\uff0c\u518d\u5f00\u59cb\u65b0\u4f1a\u8bdd\u3002",L"Transcript limit reached. Copy your transcript before starting a new session."));
case AiMessage::RequestFailed:return UiUtf8(UiLabel(L"AI \u8bf7\u6c42\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u670d\u52a1\u914d\u7f6e\u540e\u91cd\u8bd5\u3002",L"AI request failed. Check the gateway configuration and try again."));
case AiMessage::NoTranscript:return UiUtf8(UiLabel(L"\u8bf7\u5148\u542f\u7528\u5b57\u5e55\u5e76\u83b7\u53d6\u8bed\u97f3\u8bb0\u5f55\u3002",L"Start captions and collect a transcript first."));
case AiMessage::NoSummary:return UiUtf8(UiLabel(L"\u8bf7\u5148\u751f\u6210\u6458\u8981\uff0c\u518d\u5f00\u59cb\u6717\u8bfb\u3002",L"Generate a summary before reading it aloud."));
case AiMessage::Busy:return UiUtf8(UiLabel(L"AI \u6b63\u5728\u5904\u7406\u8bf7\u6c42\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5\u3002",L"AI is processing another request. Try again shortly."));
}return {}; }
}
