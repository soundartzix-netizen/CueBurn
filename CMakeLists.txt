cmake_minimum_required(VERSION 3.22)

# ランタイムを静的リンク (VC++ランタイム未導入のPCでも動くように)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(CueBurn VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# プリセットサンプル (Samples/*.flac) を、プラグインに埋め込む
file(GLOB SCRATCH_SAMPLE_FILES CONFIGURE_DEPENDS Samples/*.flac)
juce_add_binary_data(ScratchSamples SOURCES ${SCRATCH_SAMPLE_FILES})

set(SCRATCH_SOURCES
    Source/PluginProcessor.cpp
    Source/Fills.cpp
    Source/UserPatterns.cpp
    Source/PluginEditor.cpp
    Source/DeckStyle.cpp
    Common/UiKit.cpp)

# 楽器(ジェネレーター)として読み込むタイプ。ピアノロールのノートで鳴らす
juce_add_plugin(CueBurn
    COMPANY_NAME "SoundArtz"
    PLUGIN_MANUFACTURER_CODE Sndz
    PLUGIN_CODE Scsy
    FORMATS VST3 Standalone
    PRODUCT_NAME "Cue Burn"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(CueBurn PRIVATE ${SCRATCH_SOURCES})
target_include_directories(CueBurn PRIVATE Common Source)

set(SCRATCH_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_MP3AUDIOFORMAT=1)

target_compile_definitions(CueBurn PUBLIC ${SCRATCH_DEFS})

target_link_libraries(CueBurn PRIVATE
    ScratchSamples
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(CueBurn PRIVATE /utf-8)
endif()

# --- DSPの単体テスト (ホスト無しでノートを送って、出力を数値で確認する) ---
juce_add_console_app(DspTest PRODUCT_NAME "DspTest")
target_sources(DspTest PRIVATE Tests/DspTest.cpp ${SCRATCH_SOURCES})
target_include_directories(DspTest PRIVATE Common Source)
target_compile_definitions(DspTest PUBLIC ${SCRATCH_DEFS})
target_link_libraries(DspTest PRIVATE ScratchSamples juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
if (MSVC)
    target_compile_options(DspTest PRIVATE /utf-8)
endif()

# --- 見た目の確認用: エディタを画面外で描画してPNGに保存する ---
juce_add_console_app(UiSnapshot PRODUCT_NAME "UiSnapshot")
target_sources(UiSnapshot PRIVATE Tests/UiSnapshot.cpp ${SCRATCH_SOURCES})
target_include_directories(UiSnapshot PRIVATE Common Source)
target_compile_definitions(UiSnapshot PUBLIC ${SCRATCH_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(UiSnapshot PRIVATE ScratchSamples juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
if (MSVC)
    target_compile_options(UiSnapshot PRIVATE /utf-8)
endif()

# --- 聴き比べ用: テンプレートを指定テンポで WAV に書き出す ---
juce_add_console_app(RenderWav PRODUCT_NAME "RenderWav")
target_sources(RenderWav PRIVATE Tests/RenderWav.cpp ${SCRATCH_SOURCES})
target_include_directories(RenderWav PRIVATE Common Source)
target_compile_definitions(RenderWav PUBLIC ${SCRATCH_DEFS})
target_link_libraries(RenderWav PRIVATE ScratchSamples juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
if (MSVC)
    target_compile_options(RenderWav PRIVATE /utf-8)
endif()
