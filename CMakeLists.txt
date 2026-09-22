cmake_minimum_required(VERSION 3.24)

project(RaceEngineer VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Multimedia Network Quick QuickControls2)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS_3RD_PARTY OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
# Enable the Vulkan backend automatically when a Vulkan SDK is present.  The
# CPU build remains available on machines without glslc/the Vulkan loader.
if(DEFINED ENV{VULKAN_SDK})
    set(GGML_VULKAN ON CACHE BOOL "" FORCE)
else()
    set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

add_library(vieneu-tts SHARED IMPORTED GLOBAL)
set_target_properties(vieneu-tts PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vieneu-bin/bin/vieneu-tts.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vieneu-bin/lib/vieneu-tts.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vieneu-bin/include"
)

qt_standard_project_setup(REQUIRES 6.5)

set(RACEENGINEER_ICON_RESOURCES
    assets/final_icon_16.png
    assets/final_icon_24.png
    assets/final_icon_32.png
    assets/final_icon_48.png
    assets/final_icon_64.png
    assets/final_icon_128.png
    assets/final_icon_180.png
    assets/final_icon_192.png
    assets/final_icon_256.png
    assets/final_icon_512.png
    assets/final_icon.ico
)
set(RACEENGINEER_ICON_FILES)
foreach(icon_resource IN LISTS RACEENGINEER_ICON_RESOURCES)
    list(APPEND RACEENGINEER_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${icon_resource}")
endforeach()

qt_add_executable(RaceEngineer
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
    src/telemetry/ac/AcExtensionClient.cpp
    src/telemetry/ac/AcExtensionClient.h
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
    src/audio/AudioDucker.cpp
    src/audio/AudioDucker.h
    src/audio/MessageDispatcher.cpp
    src/audio/MessageDispatcher.h
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
    src/tts/VieNeuTtsBackend.cpp
    src/tts/VieNeuTtsBackend.h
    src/tts/RacingTextNormalizer.cpp
    src/tts/RacingTextNormalizer.h
    src/spotter/SpotterEngine.cpp
    src/spotter/SpotterEngine.h
    src/input/DInputButtonMonitor.cpp
    src/input/DInputButtonMonitor.h
)

if(WIN32)
    set_target_properties(RaceEngineer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

qt_add_qml_module(RaceEngineer
    URI RaceEngineer
    VERSION 1.0
    QML_FILES qml/Main.qml
    RESOURCES
        ${RACEENGINEER_ICON_RESOURCES}
)

if(WIN32)
    target_sources(RaceEngineer PRIVATE assets/final_icon.rc)
endif()

target_include_directories(RaceEngineer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vieneu-bin/include
)

target_link_libraries(RaceEngineer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
    whisper
    vieneu-tts
)

option(RACEENGINEER_ENABLE_LTCG "Enable Link-Time Code Generation (LTCG) for release packages" OFF)
if(MSVC AND RACEENGINEER_ENABLE_LTCG)
    # Whole-program optimization for production packaging. Disabled by default
    # during active development to reduce incremental build/link times by 10x.
    set_property(TARGET RaceEngineer PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

add_custom_command(TARGET RaceEngineer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sound.mp3"
        "$<TARGET_FILE_DIR:RaceEngineer>/sound.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${RACEENGINEER_ICON_FILES}
        "$<TARGET_FILE_DIR:RaceEngineer>"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-phowhisper-small-q5_1.bin")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/models"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-phowhisper-small-q5_1.bin"
            "$<TARGET_FILE_DIR:RaceEngineer>/models/ggml-phowhisper-small-q5_1.bin"
    )
endif()
add_custom_command(TARGET RaceEngineer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vieneu-bin/bin"
        "$<TARGET_FILE_DIR:RaceEngineer>"
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/vieneu-v3")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/models/vieneu-v3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/models/vieneu-v3"
            "$<TARGET_FILE_DIR:RaceEngineer>/models/vieneu-v3"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices/vieneu")
    add_custom_command(TARGET RaceEngineer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RaceEngineer>/voices/vieneu"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/voices/vieneu"
            "$<TARGET_FILE_DIR:RaceEngineer>/voices/vieneu"
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

option(RACEENGINEER_BUILD_STT_BENCHMARK "Build the local whisper.cpp STT benchmark harness" OFF)
if(RACEENGINEER_BUILD_STT_BENCHMARK)
    add_executable(RaceEngineerSttBench tools/stt_benchmark.cpp)
    target_include_directories(RaceEngineerSttBench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp/include
    )
    target_link_libraries(RaceEngineerSttBench PRIVATE whisper)
    if(MSVC)
        target_compile_options(RaceEngineerSttBench PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(RaceEngineerSttBench PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MSVC)
    target_compile_options(RaceEngineer PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(RaceEngineer PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(RaceStateTests
        tests/RaceStateTests.cpp
        src/config/SettingsManager.cpp
        src/telemetry/common/RaceState.cpp
        src/telemetry/mock/MockTelemetryProvider.cpp
        src/race/RaceHistory.cpp
        src/events/EventEngine.cpp
        src/llm/ConversationManager.cpp
        src/llm/tools/ToolRegistry.cpp
        src/llm/providers/OpenAICompatibleProvider.cpp
        src/llm/providers/OpenAICompatibleProvider.h
        src/llm/ILLMProvider.h
        src/llm/LLMManager.cpp
        src/llm/LLMManager.h
        src/utils/Logging.cpp
        src/spotter/SpotterEngine.cpp
        src/telemetry/ac/AcExtensionClient.cpp
        src/telemetry/common/WindowsSharedMemory.cpp
        src/tts/RacingTextNormalizer.cpp
        src/audio/AudioDucker.cpp
    )
    target_include_directories(RaceStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(RaceStateTests PRIVATE Qt6::Core Qt6::Network whisper)
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
install(FILES sound.mp3 DESTINATION bin)
install(FILES ${RACEENGINEER_ICON_FILES} DESTINATION bin)
install(DIRECTORY third_party/vieneu-bin/bin/ DESTINATION bin)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/vieneu-v3")
    install(DIRECTORY models/vieneu-v3/ DESTINATION bin/models/vieneu-v3)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/voices/vieneu")
    install(DIRECTORY voices/vieneu/ DESTINATION bin/voices/vieneu)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/ggml-phowhisper-small-q5_1.bin")
    install(FILES models/ggml-phowhisper-small-q5_1.bin DESTINATION bin/models)
endif()
if(WIN32)
 find_program(RACEENGINEER_WINDEPLOYQT_EXECUTABLE windeployqt
  HINTS
   "${CMAKE_PREFIX_PATH}/bin"
   "${Qt6Core_DIR}/../../../bin"
 )
 if(RACEENGINEER_WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET RaceEngineer POST_BUILD
   COMMAND "${RACEENGINEER_WINDEPLOYQT_EXECUTABLE}"
   --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
   --no-translations
   --compiler-runtime
   "$<TARGET_FILE:RaceEngineer>"
   VERBATIM
  )
 endif()
endif()
