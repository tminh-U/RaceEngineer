cmake_minimum_required(VERSION 3.24)

project(RaceEngineer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia Network Widgets)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS_3RD_PARTY OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(RaceEngineer WIN32
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.h
    src/telemetry/common/RaceState.cpp
    src/telemetry/common/RaceState.h
    src/telemetry/common/ISimTelemetryProvider.h
    src/telemetry/common/WindowsSharedMemory.cpp
    src/telemetry/common/WindowsSharedMemory.h
    src/telemetry/common/WindowsProcess.cpp
    src/telemetry/common/WindowsProcess.h
    src/telemetry/TelemetryManager.cpp
    src/telemetry/TelemetryManager.h
    src/telemetry/ac/ACTelemetryProvider.cpp
    src/telemetry/ac/ACTelemetryProvider.h
    src/telemetry/acc/ACCTelemetryProvider.cpp
    src/telemetry/acc/ACCTelemetryProvider.h
    src/telemetry/acc/AccBroadcastClient.cpp
    src/telemetry/acc/AccBroadcastClient.h
    src/telemetry/mock/MockTelemetryProvider.cpp
    src/telemetry/mock/MockTelemetryProvider.h
    src/race/RaceHistory.cpp
    src/race/RaceHistory.h
    src/events/EventEngine.cpp
    src/events/EventEngine.h
    src/utils/Logging.cpp
    src/utils/Logging.h
    src/audio/AudioCapture.cpp
    src/audio/AudioCapture.h
    src/audio/VoiceInputController.cpp
    src/audio/VoiceInputController.h
    src/audio/MessageDispatcher.cpp
    src/audio/MessageDispatcher.h
    src/vad/TenVadProcessor.cpp
    src/vad/TenVadProcessor.h
    src/vad/VadProcessor.cpp
    src/vad/VadProcessor.h
    src/stt/WhisperRecognizer.cpp
    src/stt/WhisperRecognizer.h
    src/config/CredentialStore.cpp
    src/config/CredentialStore.h
    src/config/SettingsManager.cpp
    src/config/SettingsManager.h
    src/llm/ILLMProvider.h
    src/llm/ConversationManager.cpp
    src/llm/ConversationManager.h
    src/llm/LLMManager.cpp
    src/llm/LLMManager.h
    src/llm/providers/OpenAICompatibleProvider.cpp
    src/llm/providers/OpenAICompatibleProvider.h
    src/llm/tools/ToolRegistry.cpp
    src/llm/tools/ToolRegistry.h
    src/tts/ITtsBackend.h
    src/tts/GwenTtsBackend.cpp
    src/tts/GwenTtsBackend.h
    src/spotter/SpotterEngine.cpp
    src/spotter/SpotterEngine.h
    src/input/DInputButtonMonitor.cpp
    src/input/DInputButtonMonitor.h
)

add_library(TenVad SHARED IMPORTED)
set_target_properties(TenVad PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ten_vad/lib/ten_vad.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ten_vad/lib/ten_vad.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ten_vad/include"
)

set_source_files_properties(src/ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_qml_module(RaceEngineer
    URI RaceEngineer
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
)

target_include_directories(RaceEngineer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(RaceEngineer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Network
    Qt6::Widgets
    TenVad
    whisper
)

add_custom_command(TARGET RaceEngineer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ten_vad/lib/ten_vad.dll"
        "$<TARGET_FILE_DIR:RaceEngineer>/ten_vad.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sound.mp3"
        "$<TARGET_FILE_DIR:RaceEngineer>/sound.mp3"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-small-q5_1.bin")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-small-q5_1.bin"
            "$<TARGET_FILE_DIR:RaceEngineer>/models/ggml-small-q5_1.bin"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gwen-tts/crispasr-windows-x86_64-vulkan/crispasr.exe")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gwen-tts/crispasr-windows-x86_64-vulkan"
            "$<TARGET_FILE_DIR:RaceEngineer>/tools/gwen-tts"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices/gwen-tts/khanh_toan.wav")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/voices/gwen-tts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/voices/gwen-tts/khanh_toan.wav"
            "$<TARGET_FILE_DIR:RaceEngineer>/voices/gwen-tts/khanh_toan.wav"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/voices/gwen-tts/khanh_toan.txt"
            "$<TARGET_FILE_DIR:RaceEngineer>/voices/gwen-tts/khanh_toan.txt"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/gwen-tts/gwen-tts-0.6b-q8_0.gguf")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/models/gwen-tts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/gwen-tts/gwen-tts-0.6b-q8_0.gguf"
            "$<TARGET_FILE_DIR:RaceEngineer>/models/gwen-tts/gwen-tts-0.6b-q8_0.gguf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/gwen-tts/qwen3-tts-tokenizer-12hz.gguf"
            "$<TARGET_FILE_DIR:RaceEngineer>/models/gwen-tts/qwen3-tts-tokenizer-12hz.gguf"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/spotter/manifest.json")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/audio/spotter"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/spotter"
            "$<TARGET_FILE_DIR:RaceEngineer>/audio/spotter"
    )
endif()

if(WIN32)
    target_link_libraries(RaceEngineer PRIVATE Advapi32 Dinput8 Dxguid Ole32)
    target_compile_definitions(RaceEngineer PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

target_compile_definitions(RaceEngineer PRIVATE
    RACEENGINEER_ENABLE_MOCK=$<IF:$<CONFIG:Debug>,1,0>
)

if(MSVC)
    target_compile_options(RaceEngineer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(RaceEngineer PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(RaceStateTests
        tests/RaceStateTests.cpp
        src/telemetry/common/RaceState.cpp
        src/telemetry/mock/MockTelemetryProvider.cpp
        src/race/RaceHistory.cpp
        src/events/EventEngine.cpp
        src/vad/TenVadProcessor.cpp
        src/llm/ConversationManager.cpp
        src/llm/tools/ToolRegistry.cpp
        src/llm/providers/OpenAICompatibleProvider.cpp
        src/llm/providers/OpenAICompatibleProvider.h
        src/llm/ILLMProvider.h
        src/llm/LLMManager.cpp
        src/llm/LLMManager.h
        src/utils/Logging.cpp
        src/spotter/SpotterEngine.cpp
    )
    target_include_directories(RaceStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(RaceStateTests PRIVATE TenVad Qt6::Core Qt6::Network)
    add_custom_command(TARGET RaceStateTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ten_vad/lib/ten_vad.dll"
            "$<TARGET_FILE_DIR:RaceStateTests>/ten_vad.dll"
    )
    if(MSVC)
        target_compile_options(RaceStateTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(RaceStateTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME RaceStateTests COMMAND RaceStateTests)
endif()

install(TARGETS RaceEngineer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(FILES third_party/ten_vad/lib/ten_vad.dll DESTINATION bin)
install(FILES sound.mp3 DESTINATION bin)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gwen-tts/crispasr-windows-x86_64-vulkan/crispasr.exe")
    install(DIRECTORY runtime/gwen-tts/crispasr-windows-x86_64-vulkan/
        DESTINATION bin/tools/gwen-tts)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/gwen-tts/gwen-tts-0.6b-q8_0.gguf")
    install(DIRECTORY models/gwen-tts/ DESTINATION bin/models/gwen-tts)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices/gwen-tts/khanh_toan.wav")
    install(DIRECTORY voices/gwen-tts/ DESTINATION bin/voices/gwen-tts)
endif()
