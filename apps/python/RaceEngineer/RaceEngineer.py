"""
RaceEngineer Assetto Corsa Companion Python App
Publishes live opponent telemetry, 3D world coordinates, gaps, and deltas
from Assetto Corsa / Content Manager to RaceEngineer via Windows Shared Memory ("Local\\race_engineer_ac_ext")
and local UDP (127.0.0.1:9996) if socket is available.
"""

import mmap
import math
import struct
import time

try:
    import ac
    import acsys
except ImportError:
    ac = None
    acsys = None

# Optional UDP support (only if _socket is available in Python runtime)
try:
    import json
    import socket
    _has_socket = True
except Exception:
    _has_socket = False

APP_NAME = "RaceEngineer"
SHM_NAME = "race_engineer_ac_ext"
UPDATE_INTERVAL = 1.0 / 30.0  # Target 30 Hz update rate.
UDP_IP = "127.0.0.1"
UDP_PORT = 9996

# Header: magic(4s), version(I), sequence(I), timestamp_ms(q), num_records(i),
#         gap_ahead(f), gap_behind(f), sectors(3f), brake_temps(4f),
#         opp_ahead(64s), opp_behind(64s)
HDR_FMT = "<4sIIqiff3f4f64s64s"
HDR_SIZE = struct.calcsize(HDR_FMT)  # 188 bytes

# Car: car_id(i), pos(i), speed_kmh(f), last_lap(f), best_lap(f),
#      world_pos(3f), driver(64s), car(64s)
CAR_FMT = "<iifff3f64s64s"
CAR_SIZE = struct.calcsize(CAR_FMT)  # 160 bytes
MAX_CARS = 64
# Must match sizeof(AcExtSharedData) = 188 + 64 * 160 = 10428 bytes
SHM_SIZE = HDR_SIZE + MAX_CARS * CAR_SIZE

shm = None
sock = None
last_update = 0
last_shm_retry = 0
sequence = 0
app_window = 0
status_label = 0


def get_or_init_shm():
    global shm, last_shm_retry
    if shm is not None:
        return shm

    now = time.time()
    if now - last_shm_retry < 1.0:
        return None
    last_shm_retry = now

    for name in ("Local\\" + SHM_NAME, SHM_NAME):
        try:
            shm = mmap.mmap(-1, SHM_SIZE, name)
            # Check or write magic
            if shm[0:4] != b"RAEX":
                shm[0:4] = b"RAEX"
                struct.pack_into("<I", shm, 4, 1)  # version 1
            if ac:
                ac.log("RaceEngineer: Connected to SHM (" + name + ", " + str(SHM_SIZE) + " bytes)")
            return shm
        except Exception as e:
            if ac:
                ac.log("RaceEngineer: SHM try failed for " + name + ": " + str(e))
    shm = None
    return None


def acMain(ac_version):
    global app_window, status_label, sock

    get_or_init_shm()

    # Try UDP socket as secondary optional transport
    if _has_socket:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        except Exception:
            sock = None

    if ac:
        app_window = ac.newApp(APP_NAME)
        ac.setSize(app_window, 220, 65)
        ac.setTitle(app_window, "RaceEngineer")

        status_label = ac.addLabel(app_window, "RaceEngineer: Initializing...")
        ac.setPosition(status_label, 10, 30)

        ac.log("RaceEngineer AC Python App initialized.")
    return APP_NAME


def car_position(car_id):
    for getter_name in (
        "getCarRealTimeLeaderboardPosition",
        "getCarLeaderboardPosition",
    ):
        getter = getattr(ac, getter_name, None)
        if getter is None:
            continue
        try:
            # AC returns a zero-based leaderboard index; RaceEngineer exposes
            # the human-facing one-based position.
            position = int(getter(car_id))
            if position >= 0:
                return position + 1
        except Exception:
            pass
    return 0


def car_state_float(car_id, state_name):
    try:
        value = float(ac.getCarState(car_id, getattr(acsys.CS, state_name)))
        return value if math.isfinite(value) else None
    except Exception:
        return None


def car_progress(car_id):
    spline = car_state_float(car_id, "NormalizedSplinePosition")
    laps = car_state_float(car_id, "LapCount")
    if spline is None or laps is None or not 0.0 <= spline <= 1.0:
        return None
    return laps + spline


def relative_gap_seconds(player_progress, player_speed, other_progress,
                         other_speed, ahead):
    if player_progress is None or other_progress is None:
        return None
    try:
        track_length = float(ac.getTrackLength(0))
    except Exception:
        return None
    average_speed_mps = (player_speed + other_speed) / 7.2
    if not math.isfinite(track_length) or track_length <= 0.0:
        return None
    if not math.isfinite(average_speed_mps) or average_speed_mps <= 0.5:
        return None
    delta = (other_progress - player_progress) if ahead else (player_progress - other_progress)
    delta %= 1.0
    if delta <= 0.0:
        return None
    return delta * track_length / average_speed_mps


def acUpdate(delta_t):
    global last_update, sequence, shm, sock, status_label
    now = time.time()
    if now - last_update < UPDATE_INTERVAL:
        return
    last_update = now

    if not ac:
        return

    cur_shm = get_or_init_shm()
    if cur_shm is None:
        if status_label:
            ac.setText(status_label, "RaceEngineer: Waiting SHM...")
        return

    try:
        car_count = ac.getCarsCount()
        if car_count <= 0:
            if status_label:
                ac.setText(status_label, "RaceEngineer: No cars")
            return

        player_pos = car_position(0)
        player_speed = car_state_float(0, "SpeedKMH") or 0.0
        player_progress = car_progress(0)

        cars_data = []
        # Reserve car id 0 for the player in the fixed-size shared-memory
        # stream; the native client consumes its live position and excludes it
        # from the opponent list.
        if player_pos > 0:
            cars_data.append((0, player_pos, 0.0, 0.0, 0.0,
                              0.0, 0.0, 0.0, b"", b""))
        opp_ahead = ""
        opp_behind = ""
        gap_ahead = -1.0
        gap_behind = -1.0
        ahead_car = None
        behind_car = None

        for car_id in range(1, min(car_count, MAX_CARS + 1)):
            try:
                if hasattr(ac, "isConnected") and not ac.isConnected(car_id):
                    continue

                name = ac.getDriverName(car_id)
                if not name or str(name) == "-1":
                    continue

                car_name = ac.getCarName(car_id)
                if not car_name or str(car_name) == "-1":
                    continue

                coords = ac.getCarState(car_id, acsys.CS.WorldPosition)
                if not isinstance(coords, (tuple, list)) or len(coords) < 3:
                    continue

                pos = car_position(car_id) or (car_id + 1)

                try:
                    speed = float(ac.getCarState(car_id, acsys.CS.SpeedKMH))
                except Exception:
                    speed = 0.0

                try:
                    last_lap = float(ac.getCarState(car_id, acsys.CS.LastLap)) / 1000.0
                except Exception:
                    last_lap = 0.0

                try:
                    best_lap = float(ac.getCarState(car_id, acsys.CS.BestLap)) / 1000.0
                except Exception:
                    best_lap = 0.0

                driver_bytes = str(name).encode("utf-8")[:63]
                car_bytes = str(car_name).encode("utf-8")[:63]

                cars_data.append((
                    car_id,
                    pos,
                    speed,
                    last_lap if last_lap > 0 else 0.0,
                    best_lap if best_lap > 0 else 0.0,
                    float(coords[0]), float(coords[1]), float(coords[2]),
                    driver_bytes,
                    car_bytes
                ))

                if pos == player_pos - 1:
                    opp_ahead = str(name)
                    ahead_car = (speed, car_progress(car_id))
                elif pos == player_pos + 1:
                    opp_behind = str(name)
                    behind_car = (speed, car_progress(car_id))
            except Exception:
                continue

        if ahead_car:
            gap = relative_gap_seconds(
                player_progress, player_speed, ahead_car[1], ahead_car[0], True
            )
            if gap is not None:
                gap_ahead = gap
        if behind_car:
            gap = relative_gap_seconds(
                player_progress, player_speed, behind_car[1], behind_car[0], False
            )
            if gap is not None:
                gap_behind = gap

        # Player splits
        player_splits = [0.0, 0.0, 0.0]
        if hasattr(ac, "getLastSplits"):
            try:
                raw_splits = ac.getLastSplits(0)
                if raw_splits and len(raw_splits) >= 3:
                    player_splits = [
                        float(s) / 1000.0 if s > 0 else 0.0
                        for s in raw_splits[:3]
                    ]
            except Exception:
                pass

        # Seqlock: odd sequence marks write in progress
        sequence = (sequence + 1) & 0xFFFFFFFF
        if sequence % 2 == 0:
            sequence += 1
        struct.pack_into("<I", cur_shm, 8, sequence)

        # Pack header (188 bytes)
        now_ms = int(now * 1000)
        opp_ahead_bytes = opp_ahead.encode("utf-8")[:63]
        opp_behind_bytes = opp_behind.encode("utf-8")[:63]

        struct.pack_into(
            HDR_FMT,
            cur_shm,
            0,
            b"RAEX",
            1,
            sequence,
            now_ms,
            len(cars_data),
            gap_ahead,
            gap_behind,
            player_splits[0], player_splits[1], player_splits[2],
            0.0, 0.0, 0.0, 0.0,
            opp_ahead_bytes,
            opp_behind_bytes
        )

        # Pack cars
        offset = HDR_SIZE
        for car in cars_data:
            struct.pack_into(
                CAR_FMT,
                cur_shm,
                offset,
                car[0],
                car[1],
                car[2],
                car[3],
                car[4],
                car[5], car[6], car[7],
                car[8],
                car[9]
            )
            offset += CAR_SIZE

        # Seqlock: even sequence marks write complete
        sequence = (sequence + 1) & 0xFFFFFFFF
        if sequence % 2 != 0:
            sequence += 1
        struct.pack_into("<I", cur_shm, 8, sequence)

        if status_label:
            ac.setText(status_label, "SHM: OK | Cars: " + str(len(cars_data)))

        # Optional UDP packet sending
        if sock:
            try:
                payload = {
                    "version": 1,
                    "timestamp": now_ms,
                    "cars": [
                        {
                            "id": c[0],
                            "pos": c[1],
                            "speed": round(c[2], 1),
                            "coords": [c[5], c[6], c[7]],
                            "driver": c[8].decode("utf-8", "ignore"),
                            "car": c[9].decode("utf-8", "ignore")
                        }
                        for c in cars_data
                    ],
                    "player": {
                        "position": player_pos,
                        "opp_ahead": opp_ahead,
                        "opp_behind": opp_behind,
                        "sectors": player_splits
                    }
                }
                sock.sendto(json.dumps(payload).encode("utf-8"), (UDP_IP, UDP_PORT))
            except Exception:
                pass

    except Exception as e:
        if ac:
            ac.log("RaceEngineer error: " + str(e))


def acShutdown():
    global shm, sock
    if shm:
        try:
            shm.close()
        except Exception:
            pass
        shm = None
    if sock:
        try:
            sock.close()
        except Exception:
            pass
        sock = None
