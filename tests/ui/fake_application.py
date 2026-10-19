"""Minimal QObject facade for rendering and interaction tests of the QML UI.

This fake deliberately mirrors the public names consumed by qml/ and records
invocations.  It does not simulate RaceEngineer native services.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from PySide6.QtCore import QObject, Property, Signal, Slot
from native_contract import load_schema, validate_sources


class FakeStrategyData(QObject):
    changed = Signal()

    _defaults = {
        "enabled": False,
        "busy": False,
        "simulatorConnected": False,
        "status": "Ghi dữ liệu đã tắt.",
        "storagePath": "/tmp/raceengineer-test-data",
        "savedRecords": 0,
        "sharingEnabled": False,
        "realisticSetupConfirmed": False,
        "raceActive": False,
        "sharingEndpoint": "",
        "sharingConfigured": False,
        "sharingStatus": "Chưa cấu hình chia sẻ.",
    }

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self.values = dict(self._defaults)
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.property_writes: list[tuple[str, Any]] = []
        self.allowed_method_arities: dict[str, set[int]] = getattr(type(self), "native_method_arities", {})

    def value(self, name: str) -> Any:
        return self.values.get(name)

    def set_value(self, name: str, value: Any, *, qml_write: bool = False) -> None:
        self.values[name] = value
        if qml_write:
            self.property_writes.append((name, value))
        self.valueChanged.emit()

    def invoke(self, name: str, *args: Any) -> None:
        allowed = self.allowed_method_arities.get(name, set())
        if len(args) not in allowed:
            raise AssertionError(f"Fake StrategyRecorder.{name} received arity {len(args)}; native allows {sorted(allowed)}")
        self.calls.append((name, args))


def _make_strategy_class(native_methods: dict[str, dict[str, Any]]) -> type[FakeStrategyData]:
    # A fresh signal is needed: PySide does not attach an inherited signal as a
    # NOTIFY signal for a Q_PROPERTY added by a Python QObject subclass.
    attrs: dict[str, Any] = {"valueChanged": Signal()}
    allowed_method_arities: dict[str, set[int]] = {}
    for name, value in FakeStrategyData._defaults.items():
        def getter(self: FakeStrategyData, key: str = name) -> Any:
            return self.value(key)

        def setter(self: FakeStrategyData, new_value: Any, key: str = name) -> None:
            self.set_value(key, new_value, qml_write=True)

        writable = name in {"enabled", "sharingEnabled", "realisticSetupConfirmed"}
        attrs[name] = Property("QVariant", getter, setter if writable else None,
                               notify=attrs["valueChanged"])

    for method, spec in native_methods.items():
        allowed = set(range(spec["minimum_arity"], spec["maximum_arity"] + 1))
        allowed_method_arities[method] = allowed
        def action(self: FakeStrategyData, *args: Any, _method: str = method) -> None:
            self.invoke(_method, *args)

        action.__name__ = method
        for arity in sorted(allowed, reverse=True):
            action = Slot(*(["QVariant"] * arity))(action)
        attrs[method] = action
    attrs["native_method_arities"] = allowed_method_arities
    return type("_FakeStrategyData", (FakeStrategyData,), attrs)


def _default_for(name: str) -> Any:
    explicit: dict[str, Any] = {
        "simulatorName": "",
        "connected": False,
        "connectionText": "Chưa kết nối AC / ACC",
        "telemetry": {},
        "mockAvailable": False,
        "mockEnabled": False,
        "latestEvent": "",
        "eventLog": [],
        "voiceStatus": "Idle",
        "microphoneName": "Không có mic",
        "microphoneLevel": 0.0,
        "latestUserText": "",
        "latestEngineerText": "",
        "conversationLog": [],
        "driverName": "Driver",
        "responseStyle": "Tiêu chuẩn",
        "apiProvider": "OpenAI Compatible",
        "apiBaseUrl": "",
        "apiKey": "",
        "apiModel": "",
        "apiStreaming": True,
        "apiTimeoutMilliseconds": 15000,
        "apiMaximumTokens": 64,
        "apiTemperature": 0.2,
        "apiReasoning": False,
        "apiConfigured": False,
        "apiState": "NotConfigured",
        "apiDetail": "",
        "apiStatistics": {},
        "ttsAvailable": False,
        "ttsBackend": "VieNeu-TTS",
        "ttsStatus": "Chưa sẵn sàng",
        "googleTtsApiKeyConfigured": False,
        "googleTtsModel": "gemini-3.8-flash-lite-tts",
        "googleTtsVoice": "",
        "googleTtsVoices": [],
        "googleTtsFirstAudioMs": -1,
        "ttsCpuThreads": 2,
        "ttsExternalThreadOverride": False,
        "availableTtsVoices": [],
        "selectedTtsVoice": "",
        "audioOutputDevices": [],
        "selectedAudioOutput": "",
        "audioInputDevices": [],
        "selectedAudioInput": "",
        "selectedAudioInputId": "",
        "ttsVolume": 0.8,
        "toolLog": [],
        "keyboardPttEnabled": True,
        "directInputPttEnabled": False,
        "directInputBinding": "Chưa gán",
        "directInputStatus": "DirectInput not initialized",
        "audioDuckingEnabled": False,
        "isAudioDucked": False,
        "startWithWindows": False,
        "trayAvailable": False,
        "minimizeToTray": False,
        "minimizeOnClose": False,
        "gpuRendererEnabled": False,
        "gpuRendererRestartRequired": False,
        "startupReady": True,
        "setupVisible": False,
        "setupStep": 0,
        "ttsBenchmarkRunning": False,
        "ttsBenchmarkRestoring": False,
        "ttsBenchmarkProgress": 0.0,
        "ttsBenchmarkStatus": "Chưa chạy benchmark.",
        "ttsBenchmarkTimestamp": "",
        "ttsBenchmarkResults": [],
        "ttsBenchmarkStale": False,
        "ttsBenchmarkAllowed": True,
        "ttsBenchmarkUnavailableReason": "",
        "startupProgress": 1.0,
        "startupError": "",
        "aiComputeDevices": [{"id": "cpu", "label": "CPU"}],
        "selectedAiComputeDevice": "cpu",
        "aiComputeStatus": "CPU",
        "aiComputeRestartRequired": False,
        "strategyEnabled": False,
        "strategyAvailable": False,
        "strategyStatus": "Tạm thời chưa khả dụng",
        "strategyPitLap": 0,
        "strategyDetail": "",
        "spotterEnabled": False,
        "spotterStatus": "Chưa sẵn sàng",
        "spotterWarningGapMeters": 1.5,
        "fuelAlertsEnabled": True,
        "tyreAlertsEnabled": True,
        "lapDeltaEnabled": True,
        "flagAlertsEnabled": True,
        "damageAlertsEnabled": True,
        "lapSummaryEnabled": False,
        "lapSummaryStatus": "Tắt",
        "latestLapSummary": "",
        "strategyData": None,
    }
    if name in explicit:
        return explicit[name]
    if name.endswith(("Enabled", "Configured", "Available", "Required", "Running",
                      "Restoring", "Stale", "Allowed", "Ready", "Visible", "Connected")):
        return False
    if name.endswith(("Devices", "Voices", "Results", "Log")):
        return []
    if name.startswith(("set", "start", "cancel", "finish", "open", "retry", "preview",
                        "save", "test", "ask", "begin", "end", "create", "refresh")):
        return None
    if name in {"telemetry", "apiStatistics"}:
        return {}
    if name.endswith(("Progress", "Temperature", "Volume", "Level", "Meters", "PitLap")):
        return 0.0
    if name.endswith(("Name", "Text", "Status", "Detail", "Model", "Voice", "Binding",
                      "Endpoint", "Key", "Provider", "Backend", "Output", "Input", "Device")):
        return ""
    return ""


def _make_application_class(prop_names: set[str], methods: dict[str, set[int]],
                            native_signals: set[str]) -> type[QObject]:
    attrs: dict[str, Any] = {"changed": Signal()}
    attrs.update({name: Signal() for name in native_signals})
    for name in sorted(prop_names - set(methods)):
        default = _default_for(name)
        if name == "strategyData":
            def get_strategy(self: QObject) -> QObject:
                return self._strategy_data
            attrs[name] = Property(QObject, get_strategy, constant=True)
            continue

        def getter(self: QObject, key: str = name) -> Any:
            return self.values.get(key)

        def setter(self: QObject, value: Any, key: str = name) -> None:
            self.values[key] = value
            self.changed.emit()

        writable = name in {"audioDuckingEnabled", "driverName", "responseStyle"}
        attrs[name] = Property("QVariant", getter, setter if writable else None,
                               notify=attrs["changed"])

    for name, arities in methods.items():
        def invokable(self: QObject, *args: Any, _name: str = name) -> Any:
            self.calls.append((_name, args))
            if _name == "beginPushToTalk":
                self.ptt_depth += 1
            elif _name == "endPushToTalk":
                self.ptt_depth = max(0, self.ptt_depth - 1)
            elif _name == "openSetup":
                self.values["setupVisible"] = True
            elif _name == "finishSetup":
                self.values["setupVisible"] = False
            elif _name == "setSetupStep" and args:
                self.values["setupStep"] = int(args[0])
            elif _name == "retryStartup":
                self.values["startupReady"] = True
                self.values["startupError"] = ""
            elif _name == "bonoVoiceDescription":
                return "A calm, concise racing engineer voice with clear Vietnamese pronunciation."
            elif _name == "startTtsBenchmark":
                self.values["ttsBenchmarkRunning"] = True
            elif _name == "cancelTtsBenchmark":
                self.values["ttsBenchmarkRunning"] = False
            elif _name == "setPushToTalkOptions" and len(args) >= 2:
                self.values["keyboardPttEnabled"] = bool(args[0])
                self.values["directInputPttEnabled"] = bool(args[1])
            elif _name == "saveAiSettings" and len(args) >= 5:
                self.values["apiProvider"] = args[0]
                self.values["apiBaseUrl"] = args[1]
                self.values["apiKey"] = args[2]
                self.values["apiModel"] = args[3]
                self.values["apiStreaming"] = bool(args[4])
                if len(args) >= 9:
                    self.values["apiTimeoutMilliseconds"] = int(args[5])
                    self.values["apiMaximumTokens"] = int(args[6])
                    self.values["apiTemperature"] = float(args[7])
                    self.values["apiReasoning"] = bool(args[8])
                self.apiSettingsChanged.emit()
            elif _name.startswith("set") and len(args) == 1:
                property_name = {
                    "setTtsBackend": "ttsBackend",
                    "setTtsVoice": "selectedTtsVoice",
                    "setAudioInputDevice": "selectedAudioInputId",
                    "setAudioOutputDevice": "selectedAudioOutput",
                    "setAiComputeDevice": "selectedAiComputeDevice",
                }.get(_name, _name[3:4].lower() + _name[4:])
                if property_name in self.values:
                    self.values[property_name] = args[0]
                    if _name == "setApiReasoning":
                        self.apiSettingsChanged.emit()
            # Native testApiConnection changes apiState/voiceStatus signals,
            # but leaves apiSettingsChanged alone. Keeping this call quiet
            # preserves edits in bound TextFields just as the native object does.
            if _name != "testApiConnection":
                self.changed.emit()
            return True if _name == "configureStrategySharing" else None

        # Expose each call shape found in the app sources as a Qt invokable.
        invokable.__name__ = name
        for arity in sorted(arities, reverse=True):
            signature = ["QVariant"] * arity
            if name == "bonoVoiceDescription":
                invokable = Slot(*signature, result=str)(invokable)
            elif name == "configureStrategySharing":
                invokable = Slot(*signature, result=bool)(invokable)
            else:
                invokable = Slot(*signature)(invokable)
        attrs[name] = invokable

    def initialize(self: QObject, strategy_data: FakeStrategyData) -> None:
        QObject.__init__(self)
        self.values = {name: _default_for(name) for name in prop_names - set(methods)}
        self._strategy_data = strategy_data
        self.values["strategyData"] = strategy_data
        self.calls: list[tuple[str, tuple[Any, ...]]] = []
        self.ptt_depth = 0

    attrs["__init__"] = initialize
    return type("FakeApplication", (QObject,), attrs)


def make_fake_application(qml_root: Path, extra_sources: list[Path] | None = None) -> tuple[QObject, FakeStrategyData]:
    sources = [*qml_root.rglob("*.qml")]
    for source in extra_sources or []:
        if source.exists() and source not in sources:
            sources.append(source)
    validation = validate_sources(qml_root.parent, sources)
    if not validation["passed"]:
        raise AssertionError("QML references do not match the checked-in native QObject API:\n" +
                             "\n".join(validation["failures"]))

    schema = load_schema(qml_root.parent)
    props = set(schema["application"]["properties"])
    props.add("strategyData")
    arities: dict[str, set[int]] = {
        name: set(range(spec["minimum_arity"], spec["maximum_arity"] + 1))
        for name, spec in schema["application"]["methods"].items()
    }
    native_signals = set(schema["application"]["signals"])
    if "apiSettingsChanged" not in native_signals:
        raise AssertionError("Fake facade contract is missing native Application::apiSettingsChanged")
    strategy_props = set(schema["strategy_data"]["properties"])
    missing = strategy_props - set(FakeStrategyData._defaults)
    if missing:
        raise AssertionError(f"Fake facade is missing native StrategyRecorder properties: {sorted(missing)}")
    strategy_class = _make_strategy_class(schema["strategy_data"]["methods"])
    strategy = strategy_class()
    ApplicationClass = _make_application_class(props | {"strategyData"}, arities, native_signals)
    return ApplicationClass(strategy), strategy
