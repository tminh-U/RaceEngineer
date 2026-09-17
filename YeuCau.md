Build a Windows desktop application that acts like CrewChief, but uses a cloud LLM API for natural-language interaction.

For the first version, only support:

- Assetto Corsa (AC)
- Assetto Corsa Competizione (ACC)

Do not implement other simulators yet.

The architecture must make it easy to add more simulators later.

The primary goal is very low CPU/GPU usage on the gaming PC because AC/ACC may already use almost all available CPU resources.

Do NOT run the LLM locally in the MVP.

Use a remote LLM API.

Default provider:

- Mistral API
- default model: Ministral 3B or the current small Ministral equivalent

However, the application must NOT be tightly coupled to Mistral.

Design the LLM layer so other OpenAI-compatible or custom API providers can be added later, including:

- Groq
- OpenRouter
- Cloudflare Workers AI
- local llama.cpp server
- other OpenAI-compatible endpoints

---

# Main behavior

The application has two completely separate systems.

## 1. Real-time deterministic race engineer / spotter

This MUST NOT use the LLM.

Examples:

- Car left
- Car right
- Still there
- Clear left
- Clear right
- Yellow flag
- Blue flag
- Pit limiter
- Fuel critical
- Engine overheating
- Major damage

These events must have minimal latency and must continue working even if:

- internet is unavailable
- API fails
- API rate limit is reached
- LLM provider is offline

## 2. Conversational AI race engineer

The user should be able to speak naturally.

Example:

"Do I have enough fuel to finish?"

Pipeline:

Microphone
→ VAD
→ STT
→ remote LLM
→ telemetry tool call
→ remote LLM
→ response
→ TTS
→ headphones

The user should also be able to ask follow-up questions.

Example:

User:
"How are my tyres?"

Assistant:
"Fronts are slightly hotter than the rears."

User:
"What about pressures?"

The application should preserve enough conversational context to understand that "pressures" refers to the tyres.

---

# Technology

Use:

- C++20
- CMake
- Qt 6
- Qt Quick / QML
- Windows x64
- MSVC

Use native C++ wherever possible.

Do not use Python for the production application.

Recommended components:

- TEN VAD for voice activity detection
- whisper.cpp for local speech-to-text
- Piper-compatible ONNX TTS for local speech output
- Qt Network for HTTP/API requests

The LLM runs remotely.

The gaming PC should NOT load a GGUF model.

---

# Performance goals

The application runs alongside AC or ACC.

CPU and GPU usage must be minimized.

Important rules:

- LLM inference must happen remotely.
- Do not use gaming GPU VRAM for AI.
- Telemetry processing must be lightweight.
- Do not feed 60 Hz telemetry continuously to the LLM.
- Do not constantly run Whisper.
- Only run speech recognition when the user actually speaks.
- Do not block the Qt UI thread.
- Avoid aggressive polling.

The LLM should only receive compact structured information when requested.

---

# High-level architecture

Suggested structure:

src/
    app/
    telemetry/
        common/
        ac/
        acc/
        mock/
    race/
    events/
    spotter/
    audio/
    vad/
    stt/
    llm/
        providers/
        tools/
    tts/
    ui/
    config/
    utils/

Suggested classes:

Application

TelemetryManager
ISimTelemetryProvider
ACTelemetryProvider
ACCTelemetryProvider
MockTelemetryProvider

RaceState
RaceHistory

EventEngine
SpotterEngine

AudioCapture
VadProcessor
SpeechRecognizer

ILLMProvider
MistralProvider
OpenAICompatibleProvider
LLMManager
ToolRegistry
ConversationManager

ITtsBackend
PiperTtsBackend

AudioPlayback
MessageDispatcher

SettingsManager

Use interfaces only where they provide a real benefit.

Avoid unnecessary over-engineering.

---

# Simulator detection

Automatically detect whether:

- Assetto Corsa is running
- Assetto Corsa Competizione is running

Only one simulator needs to be active at a time.

TelemetryManager should automatically connect to the supported running simulator.

UI status examples:

Not Connected

Assetto Corsa
Connected

Assetto Corsa Competizione
Connected

If the simulator closes, disconnect cleanly.

---

# Telemetry architecture

Create:

ISimTelemetryProvider

with functionality similar to:

start()
stop()
isConnected()
simName()
update()
getCurrentState()

Implement:

ACTelemetryProvider

ACCTelemetryProvider

Use the actual supported shared-memory / telemetry APIs provided by AC and ACC.

Do NOT use screen scraping.

Do NOT invent telemetry fields.

Verify the actual shared-memory structures before implementing them.

Simulator-specific structures must remain isolated inside their provider.

The rest of the application should consume a normalized RaceState.

---

# RaceState

RaceState should contain normalized fields.

Use std::optional for values not available in a specific simulator.

Example categories:

session:
- simulator
- track
- session type
- session time
- time remaining
- current lap
- total laps if available
- laps remaining if available

driver:
- position
- speed
- rpm
- gear
- throttle
- brake
- clutch
- steering

lap:
- current lap time
- previous lap time
- best lap time
- current delta
- sector times

fuel:
- fuel liters
- fuel capacity

car:
- tyre temperatures
- tyre pressures
- tyre wear if available
- brake temperatures if available
- engine temperature
- oil temperature
- water temperature
- damage if available
- pit limiter
- TC
- ABS

race:
- flag state
- gap ahead
- gap behind
- opponent ahead
- opponent behind
- pit state

Never fabricate unavailable values.

---

# RaceHistory

Create RaceHistory.

Do not store every telemetry frame indefinitely.

Store useful derived historical data:

- lap history
- fuel used per lap
- average fuel consumption
- recent lap times
- best lap
- sector history
- gap history
- position history
- tyre trends when available

This should support questions such as:

"How much fuel am I using per lap?"

"Am I getting faster?"

"Where am I losing time?"

"How consistent are my last five laps?"

---

# Deterministic calculations

Whenever something can be reliably calculated in C++, calculate it in C++ instead of asking the LLM.

Examples:

averageFuelConsumption

estimatedFuelLapsRemaining

estimatedFuelMargin

averageLapTime

lapConsistency

bestSector

recentLapTrend

Example:

estimatedFuelLapsRemaining =
    currentFuel / averageFuelConsumption

Do not make the LLM perform arithmetic that the application can compute accurately.

---

# EventEngine

Implement deterministic event detection.

Examples:

FuelLow
FuelCritical

EngineHot
EngineCritical

YellowFlag
BlueFlag

PitLimiterOn
PitLimiterOff

SessionStarted
SessionEnded

NewBestLap

Do not generate repeated events every telemetry update.

Use state transitions.

Example:

FuelState:

NORMAL
LOW
CRITICAL

Only emit an event when the state changes.

Implement configurable cooldowns where appropriate.

---

# SpotterEngine

Spotter behavior must never depend on the LLM.

Potential messages:

"Car left."

"Car right."

"Still there."

"Clear left."

"Clear right."

"Three wide."

SpotterEngine should consume normalized positional/opponent data.

Only implement functionality supported reliably by AC/ACC telemetry.

If the available simulator data is not reliable enough for a feature, do not fake it.

---

# Voice input

Use:

AudioCapture
→ TEN VAD
→ whisper.cpp

Audio processing must happen outside the UI thread.

Use TEN VAD to determine:

SpeechStarted
SpeechEnded

Do not continuously send audio to Whisper.

Maintain approximately 200-300 ms of microphone pre-roll so the beginning of speech is not cut off.

Configurable values:

- VAD sensitivity
- speech threshold
- minimum speech duration
- end-of-speech silence duration

---

# Speech recognition

Use whisper.cpp locally.

Support:

- English
- Vietnamese
- automatic language detection

Allow selecting the Whisper model.

Initial supported model sizes:

- tiny
- base
- small

The app should not start Whisper inference until an utterance is complete.

Create basic racing terminology normalization.

Examples:

DRS
ERS
ABS
TC
pit
box
delta
sector
understeer
oversteer
soft
medium
hard
slick
wet
T1
T2
T3

Do not implement a giant fixed command system.

Natural-language understanding belongs to the LLM.

---

# LLM architecture

Create:

ILLMProvider

Example interface:

sendChatRequest(...)
supportsToolCalling()
cancelRequest()
providerName()

Implement at least:

MistralProvider

and preferably a generic:

OpenAICompatibleProvider

The generic provider should support configuration of:

- base URL
- API key
- model name

Do not expose provider-specific logic throughout the application.

LLMManager should depend only on ILLMProvider.

---

# Default API configuration

Default provider:

Mistral

Default target model:

small Ministral-class model, approximately 3B parameters

Do NOT hardcode assumptions about model capabilities other than:

- chat completion
- short natural-language output
- tool/function calling if available

Model name must be configurable.

API key must never be hardcoded.

Store API keys using an appropriate local secure mechanism where practical.

At minimum:

- do not print API keys into logs
- do not commit keys into the repository
- do not include keys in example configuration files

---

# Network behavior

The race engineer must remain usable if the network fails.

Implement API states:

Connected

Requesting

RateLimited

AuthenticationError

NetworkError

ProviderError

Unavailable

If a request fails:

- show a concise UI error
- deterministic spotter continues
- deterministic event engine continues
- telemetry continues
- application must not crash

Implement request timeout.

Implement cancellation.

Do not endlessly retry failed requests during a race.

Use a small bounded retry policy only for temporary network failures.

---

# Rate-limit awareness

Free API tiers may have request or token limits.

The application must therefore minimize API usage.

Important:

Do NOT make automatic LLM calls every telemetry frame.

Do NOT ask the LLM periodically just to check telemetry.

Use deterministic C++ logic for proactive notifications.

The LLM API should normally be called only when:

1. the user asks something
2. an optional high-level AI analysis is explicitly requested

Expose useful statistics in Debug:

API requests this session

input tokens if provided by API

output tokens if provided by API

failed requests

rate-limit responses

average API latency

first-token latency if streaming is available

---

# Tool calling

The LLM must not receive raw giant telemetry dumps.

Implement ToolRegistry.

Initial tools:

get_session_status()

get_position()

get_gap_ahead()

get_gap_behind()

get_fuel_status()

get_tyre_status()

get_brake_status()

get_engine_status()

get_damage_status()

get_current_lap()

get_lap_times()

get_recent_laps()

get_best_lap()

get_sector_analysis()

get_pit_status()

get_flag_status()

get_race_summary()

Tools return structured JSON.

Example:

get_fuel_status()

returns:

{
    "fuel_liters": 18.4,
    "average_liters_per_lap": 2.15,
    "estimated_laps_remaining": 8.56,
    "race_laps_remaining": 7,
    "estimated_margin_laps": 1.56
}

The LLM must use tool results instead of making up telemetry values.

If data is unavailable, the tool should explicitly return:

{
    "available": false
}

or equivalent.

---

# Important tool-calling flow

Example:

User:

"Do I have enough fuel to finish?"

STT produces text.

LLM receives:

- system prompt
- conversation context
- available tool definitions
- user message

LLM requests:

get_fuel_status()

Application executes the tool LOCALLY.

Application sends the tool result back to the LLM API.

LLM returns:

"Yes. About one and a half laps of margin."

Do not send the entire RaceState unless a specific provider cannot support tool calling.

---

# System prompt

Use a compact system prompt conceptually similar to:

"You are a race engineer assisting a driver in real time.

Keep spoken responses very short and immediately useful.

Prefer one short sentence.

Never invent telemetry.

Use available telemetry tools whenever race, car, tyre, fuel, lap, gap, damage, pit or session information is required.

If requested data is unavailable, say so briefly.

Do not explain your reasoning unless the driver explicitly asks.

Use natural motorsport terminology.

The driver may speak English or Vietnamese.

Answer in the driver's configured language."

Keep the system prompt small to reduce token usage and API cost/quota usage.

---

# Context optimization

API quota is important.

Do not resend an unlimited conversation history.

ConversationManager should retain only:

- system instructions
- recent relevant conversation turns
- current user request
- necessary tool results

Implement context trimming.

Suggested maximum:

approximately 4-8 recent conversational turns

Do NOT continuously insert telemetry into conversation history.

Do NOT insert logs into the LLM prompt.

---

# Streaming

If the API supports streamed responses, support streaming.

Architecture:

LLM API
→ partial text tokens
→ response chunker
→ TTS queue

However, do not send individual tokens directly to TTS.

Buffer into sensible phrase chunks.

For example:

"You have enough fuel"
→ TTS

"for the finish."
→ TTS

This should reduce perceived response latency.

The implementation must also work with providers that do not support streaming.

---

# TTS

Use Piper-compatible ONNX TTS locally.

Create:

ITtsBackend

PiperTtsBackend

Allow:

- voice selection
- volume
- speed
- output device

The architecture should allow adding another TTS backend later.

TTS must be lower priority than critical spotter messages.

---

# Audio priority system

Create MessageDispatcher or AudioPriorityManager.

Priorities:

CRITICAL
SPOTTER
IMPORTANT
ENGINEER
CONVERSATION

Example priority order:

CRITICAL

"Engine overheating."

SPOTTER

"Car right."

IMPORTANT

"Fuel critical."

ENGINEER

"You're losing two tenths in sector two."

CONVERSATION

normal LLM answer

A spotter/critical message must be able to interrupt lower-priority speech.

Long AI conversation must never hide:

- car left/right
- flags
- critical car warnings

---

# Push-to-talk

Support:

1. Voice activation
2. Push-to-talk

For MVP:

keyboard push-to-talk is enough.

Architecture should allow controller / steering wheel buttons later.

---

# UI

Create a modern dark Qt Quick / QML interface.

Do not make it look like an old Win32 application.

Main dashboard:

AI Race Engineer

Simulator:

AC / ACC / Not Connected

Voice status:

Idle
Listening
Recognizing
Thinking
Speaking

API:

Provider
Model
Connected / Error

Microphone:

selected device
level meter

Latest interaction:

User:
"How is the fuel?"

Engineer:
"You're good to the finish. One lap spare."

---

# Settings

## General

Start with Windows

Minimize to tray

Minimize instead of close

Language:
English
Vietnamese

## Simulator

Auto Detect

Enable AC

Enable ACC

## Voice Input

Microphone

VAD enabled

VAD sensitivity

Push-to-talk key

Whisper model

Recognition language

## AI Provider

Provider:

Mistral

Custom OpenAI-compatible

Add architecture support for more later.

Fields:

API Base URL

API Key

Model

Test Connection button

Streaming enabled

Request timeout

Maximum response tokens

Temperature

Recommended defaults:

temperature:
low, around 0.1-0.3

maximum response tokens:
approximately 48

Keep race engineer responses concise.

## Voice Output

TTS voice

Speech speed

Volume

Output device

## Engineer

Verbosity:

Minimal
Normal
Detailed

Enable proactive deterministic engineer messages

## Spotter

Enabled

Volume

## Debug

Telemetry Viewer

Event Log

STT Log

LLM Requests

Tool Calls

API Statistics

---

# Telemetry Debug page

Create a live debug page.

Example:

Simulator: ACC

Connected: true

Speed: 181.4 km/h

RPM: 7210

Gear: 4

Throttle: 82%

Brake: 0%

Fuel: 31.2 L

Average Fuel/Lap: 2.58 L

Lap: 7

Position: P4

Gap Ahead: 1.42

Gap Behind: 2.83

Tyres:

FL
FR
RL
RR

Flags:

Green

Also provide simulator-specific raw telemetry in a separate developer view if useful.

Do not spam raw telemetry to log files.

---

# API Debug page

Show:

Provider

Model

Last HTTP status

Last latency

Average latency

Requests this session

Requests failed

Rate-limit responses

Input tokens

Output tokens

Streaming supported

Last tool called

Do not show API keys.

---

# Mock telemetry

Implement MockTelemetryProvider.

It should simulate:

RPM

speed

laps

fuel consumption

tyre temperatures

position

gaps

flags

pit limiter

This should allow the entire application to be tested without launching AC or ACC.

Debug builds should have:

Use Mock Telemetry

---

# Threading

Do not block the UI.

Suggested execution:

UI thread

Telemetry worker

Audio capture worker

STT worker

Network/API worker

TTS worker

Use Qt signals/slots or another safe asynchronous mechanism.

Avoid excessive mutex contention.

Use immutable telemetry snapshots where practical.

---

# Network implementation

Use Qt Network.

Prefer asynchronous HTTP requests.

Do not block waiting for API responses.

Support streamed HTTP responses where the provider supports it.

Implement clean cancellation when:

- user asks another question
- app shuts down
- simulator disconnects
- conversation is reset

---

# Logging

Logging categories:

APP

TELEMETRY

AC

ACC

EVENT

SPOTTER

AUDIO

VAD

STT

LLM

API

TOOL

TTS

Include timestamps.

Never log:

API keys

authorization headers

sensitive credentials

Do not log full high-frequency telemetry frames by default.

---

# Error handling

The application must degrade gracefully.

If:

API unavailable

→ telemetry and spotter continue.

Internet unavailable

→ telemetry and spotter continue.

STT unavailable

→ push-to-talk transcription cannot work but telemetry continues.

TTS unavailable

→ display text response.

Simulator disconnects

→ return to Not Connected safely.

API key missing

→ show setup requirement.

Rate limit reached

→ show:

"AI API rate limit reached."

Do not crash.

---

# Config

Use a human-readable configuration format such as JSON.

Do not hardcode paths.

Separate secrets from normal settings where practical.

---

# Tests

Add tests for deterministic application logic.

At minimum:

RaceState normalization helpers

fuel calculation

fuel averaging

fuel margin

lap history

event transitions

event cooldowns

conversation trimming

ToolRegistry JSON output

provider request serialization

provider response parsing

rate-limit error parsing

Tests must not require:

AC

ACC

microphone

internet access

---

# Build

The repository must include:

CMakeLists.txt

README.md

dependency setup instructions

Windows MSVC build instructions

project structure description

AC telemetry documentation

ACC telemetry documentation

API setup instructions

Whisper model setup

Piper voice setup

Do not commit:

AI models

API keys

large binary dependencies unnecessarily

---

# Development milestones

Do NOT attempt everything simultaneously.

Implement incrementally.

## Milestone 1

Create the C++20 / Qt project.

Implement:

main UI

TelemetryManager

ISimTelemetryProvider

ACTelemetryProvider

ACCTelemetryProvider

normalized RaceState

automatic simulator detection

telemetry debug page

MockTelemetryProvider

Build and run successfully before continuing.

## Milestone 2

Implement:

RaceHistory

deterministic calculations

EventEngine

basic notifications

logging

Unit tests.

## Milestone 3

Implement:

AudioCapture

TEN VAD

push-to-talk

microphone configuration

Show voice state in UI.

## Milestone 4

Integrate whisper.cpp.

Show recognized text.

Support English and Vietnamese.

## Milestone 5

Implement:

ILLMProvider

MistralProvider

OpenAICompatibleProvider

LLMManager

ConversationManager

API settings

Test Connection

Network error handling

## Milestone 6

Implement ToolRegistry.

Initially support:

get_session_status

get_position

get_fuel_status

get_gap_ahead

get_gap_behind

get_tyre_status

get_lap_times

get_recent_laps

Connect:

STT
→ LLM
→ tool call
→ local tool execution
→ LLM
→ response text

## Milestone 7

Integrate Piper TTS.

Complete:

voice
→ STT
→ API LLM
→ tool calling
→ answer
→ TTS

## Milestone 8

Implement:

SpotterEngine

message priorities

speech interruption

## Milestone 9

Polish:

system tray

startup behavior

API statistics

settings

error handling

packaging

---

# Critical architecture rules

Keep this dependency direction:

AC / ACC telemetry
        ↓
normalized RaceState
        ↓
RaceHistory / deterministic analysis
        ↓
EventEngine / SpotterEngine
        ↓
ToolRegistry
        ↓
LLM API

The LLM must NEVER directly depend on AC/ACC memory structures.

The LLM must NEVER be used for time-critical spotter logic.

The LLM must NEVER run continuously.

The LLM must NEVER receive telemetry every frame.

The application should remain useful without internet connectivity.

---

# Optimization objective

Prioritize:

1. Gaming performance
2. Spotter latency
3. Voice-response latency
4. Reliability
5. Low API usage
6. Natural-language quality

Do not sacrifice game performance for AI quality.

The gaming PC may already be near 100% CPU usage while racing.

Keep CPU usage near zero when the user is not speaking.

---

# First task

Inspect the repository.

If it is empty:

create the project structure and implement Milestone 1.

Do not just produce a design document.

Create real compilable code.

After each milestone:

1. build the project
2. fix all compile errors
3. run tests
4. keep working functionality intact
5. commit logically separated changes if Git is available

Do not invent AC/ACC telemetry definitions.

Verify the actual telemetry/shared-memory API structures before implementing them.

Do not stop after writing placeholders if a working implementation can reasonably be completed.

The result should become a real low-latency race engineer application, not simply a chatbot wrapped in a telemetry UI.