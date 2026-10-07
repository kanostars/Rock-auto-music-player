cmake_minimum_required(VERSION 3.24)
project(RockAutoMusicPlay VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/Qt/lib/cmake/Qt6")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.deps/Qt")
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
add_library(midifile STATIC
    third_party/midifile/src/Binasc.cpp
    third_party/midifile/src/MidiEvent.cpp
    third_party/midifile/src/MidiEventList.cpp
    third_party/midifile/src/MidiFile.cpp
    third_party/midifile/src/MidiMessage.cpp)
target_include_directories(midifile PUBLIC third_party/midifile/include)
set_target_properties(midifile PROPERTIES AUTOMOC OFF)
add_library(rock_core src/core/music.cpp src/core/midi_export.cpp src/core/hand_score.cpp src/core/practice.cpp src/core/number_score.cpp src/core/timeline.cpp)
target_include_directories(rock_core PUBLIC src)
target_link_libraries(rock_core PUBLIC midifile)
set_target_properties(rock_core PROPERTIES AUTOMOC OFF)
add_library(miniaudio STATIC third_party/miniaudio/implementation.cpp)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_compile_definitions(miniaudio PUBLIC MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_GENERATION MA_NO_ENCODING MA_NO_FLAC MA_NO_MP3)
set_target_properties(miniaudio PROPERTIES AUTOMOC OFF)
add_library(rock_audio src/audio/handpan_mixer.cpp src/audio/audio_player.cpp src/audio/live_handpan.cpp src/audio/practice_metronome.cpp assets/soundbank.qrc)
set_target_properties(rock_audio PROPERTIES AUTOMOC OFF AUTORCC ON)
target_link_libraries(rock_audio PUBLIC rock_core Qt6::Core PRIVATE miniaudio)
add_library(rock_ui src/ui/main_window.cpp src/ui/main_window.h
    src/app/preferences.cpp src/app/preferences.h
    src/ui/theme.cpp src/ui/settings_page.cpp src/ui/settings_page.h
    src/ui/practice_page.cpp src/ui/practice_page.h
    src/ui/practice_settings_dialog.cpp src/ui/practice_settings_dialog.h
    src/ui/time_seek_edit.cpp src/ui/time_seek_edit.h
    src/ui/number_score_view.cpp src/ui/number_score_view.h
    src/ui/score_archive.cpp src/ui/score_archive.h
    src/platform/global_shortcut.cpp src/platform/global_shortcut.h
    src/ui/mini_player.cpp src/ui/mini_player.h src/ui/playback_icons.cpp
    src/playback/performance_engine.cpp src/playback/performance_controller.cpp src/playback/performance_controller.h src/platform/key_output.cpp
    src/ui/performance_panel.cpp src/ui/performance_panel.h
    src/ui/handpan_test.cpp src/ui/handpan_test.h
    src/platform/output_discovery.cpp src/platform/output_discovery.h
    src/ui/piano_roll.cpp src/ui/piano_roll.h)
target_link_libraries(rock_ui PUBLIC rock_core rock_audio Qt6::Widgets Qt6::Concurrent)
if(WIN32)
    add_library(interception SHARED third_party/interception/interception.c)
    target_compile_definitions(interception PRIVATE INTERCEPTION_EXPORT)
    set_target_properties(interception PROPERTIES AUTOMOC OFF PREFIX "")
    target_include_directories(rock_ui PRIVATE third_party/interception)
    add_dependencies(rock_ui interception)
    target_link_libraries(rock_ui PRIVATE setupapi cfgmgr32 user32 dwmapi)
endif()
add_executable(RockAutoMusicPlay WIN32 main.cpp)
target_link_libraries(RockAutoMusicPlay PRIVATE rock_ui)
target_compile_definitions(RockAutoMusicPlay PRIVATE ROCK_APP_VERSION="${PROJECT_VERSION}")
include(cmake/DeployRuntime.cmake)
rock_deploy_runtime(RockAutoMusicPlay TRUE)
