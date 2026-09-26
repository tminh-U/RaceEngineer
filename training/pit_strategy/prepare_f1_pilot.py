"""Build exploratory pit-lap rankings from public F1 timing, not an ACC model.

The counterfactuals use a deliberately simple stint-pace fit. They are useful
for exercising the notebook, but have no traffic, weather, fuel or race-control
model and must never be deployed as race recommendations.
"""

from pathlib import Path
import urllib.request

import numpy as np
import pandas as pd


ROOT = Path(__file__).parent / ".local_train"
DATA = ROOT / "data"
PROFILE = "f1_dry_one_stop_pilot"
SOURCE_COMMIT = "795f6d745d43a608944f14d5c9c42386a041a177"
SOURCE = f"https://github.com/Felipe-mendezp/f1-pit-stop-drl/tree/{SOURCE_COMMIT}/data"
VERSION = "f1_stint_linear_v1"


def load() -> pd.DataFrame:
    DATA.mkdir(parents=True, exist_ok=True)
    frames = []
    for year in range(2021, 2025):
        path = DATA / f"session_{year}_V2.csv"
        if not path.exists():
            url = f"https://raw.githubusercontent.com/Felipe-mendezp/f1-pit-stop-drl/{SOURCE_COMMIT}/data/{path.name}"
            urllib.request.urlretrieve(url, path)
        frames.append(pd.read_csv(path))
    return pd.concat(frames, ignore_index=True)


def stint_fit(laps: pd.DataFrame) -> tuple[float, float] | None:
    clean = laps[
        (laps.TrackStatus.astype(str) == "1")
        & laps.LapTime.between(60, 300)
        & laps.TyreLife.between(1, 70)
        & laps.PitIn.eq(0)
        & laps.PitOut.eq(0)
    ]
    if len(clean) < 5:
        return None
    # Robust one-dimensional pace fit; negative slopes may reflect fuel burn.
    x = clean.TyreLife.to_numpy(float)
    y = clean.LapTime.to_numpy(float)
    slope = float(np.clip(np.polyfit(x, y, 1)[0], -0.3, 0.8))
    intercept = float(np.median(y - slope * x))
    return intercept, slope


def candidates() -> pd.DataFrame:
    rows = []
    source = load()
    events = list(source[["Year", "GP"]].drop_duplicates().itertuples(index=False, name=None))
    for event_number, (year, gp) in enumerate(events):
        race = source[source.Year.eq(year) & source.GP.eq(gp)]
        race_finish = int(race.LapNumber.max())
        # Race dates are absent from source; retain year order, then source order.
        start = pd.Timestamp(f"{year}-01-01", tz="UTC") + pd.Timedelta(days=event_number % 300)
        for driver, laps in race.groupby("Driver"):
            laps = laps.sort_values("LapNumber").drop_duplicates("LapNumber")
            stops = laps[laps.PitIn.eq(1) & laps.LapNumber.gt(2)]
            if stops.empty:
                continue
            stop_lap = int(stops.LapNumber.iloc[-1])
            previous_stop = int(stops.LapNumber.iloc[-2]) if len(stops) > 1 else 0
            before = laps[laps.LapNumber.gt(previous_stop) & laps.LapNumber.le(stop_lap)]
            after = laps[laps.LapNumber.gt(stop_lap)]
            if len(after) < 6 or len(before) < 8:
                continue
            old_fit, new_fit = stint_fit(before), stint_fit(after)
            if old_fit is None or new_fit is None:
                continue
            finish = int(laps.LapNumber.max())
            if finish < race_finish - 1 or finish - stop_lap < 6:
                continue
            if not before.Compound.dropna().isin(["SOFT", "MEDIUM", "HARD"]).all() or not after.Compound.dropna().isin(["SOFT", "MEDIUM", "HARD"]).all():
                continue
            old_base, old_slope = old_fit
            new_base, new_slope = new_fit
            for current in range(max(int(before.LapNumber.min()) + 5, stop_lap - 9), stop_lap - 1, 2):
                history = before[(before.LapNumber.lt(current)) & before.LapTime.between(60, 300)]
                if len(history) < 4:
                    continue
                current_row = before[before.LapNumber.eq(current)]
                if current_row.empty or current_row.TrackStatus.iloc[0] != 1 or pd.isna(current_row.TyreLife.iloc[0]):
                    continue
                age = int(current_row.TyreLife.iloc[0])
                upper = min(5, finish - current - 2)
                if upper < 2:
                    continue
                pace = float(history.LapTime.tail(4).median())
                trend = float((history.LapTime.tail(2).median() - history.LapTime.tail(4).head(2).median()) / 2)
                state = dict(
                    laps_remaining=finish - current + 1,
                    # F1 source has no fuel. This placeholder only disables the
                    # fuel constraint; no fuel conclusion may be drawn from it.
                    fuel_laps_remaining=float(finish - current + 3),
                    reserve_laps=0.0,
                    stint_laps=float(age),
                    pace_mean_s=pace,
                    pace_trend_s=trend,
                    gap_ahead_s=float(current_row.Interval_front.iloc[0]) if pd.notna(current_row.Interval_front.iloc[0]) else np.nan,
                    gap_behind_s=float(current_row.Interval_behind.iloc[0]) if pd.notna(current_row.Interval_behind.iloc[0]) else np.nan,
                    pit_loss_s=25.0,
                    window_open_offset=0,
                    window_close_offset=upper,
                )
                costs = []
                for offset in range(upper + 1):
                    pit = current + offset
                    old_ages = age + np.arange(offset + 1)
                    new_ages = np.arange(1, finish - pit + 1)
                    cost = (old_base + old_slope * old_ages).sum() + 25.0 + (new_base + new_slope * new_ages).sum()
                    costs.append(float(cost))
                best = min(costs)
                for offset, cost in enumerate(costs):
                    regret = cost - best
                    rows.append(dict(
                        **state,
                        candidate_offset=offset,
                        session_id=f"f1_{year}_{gp.replace(' ', '_')}",
                        session_start_utc=start.isoformat(),
                        session_end_utc=(start + pd.Timedelta(hours=1)).isoformat(),
                        decision_id=f"{driver}_{current}",
                        source_domain="f1",
                        profile_id=PROFILE,
                        current_lap=current,
                        candidate_lap=current + offset,
                        label_method="research_simulation",
                        label_version=VERSION,
                        provenance=SOURCE,
                        relevance=3 if regret <= 0.5 else 2 if regret <= 2 else 1 if regret <= 5 else 0,
                        outcome_cost_s=cost,
                    ))
    return pd.DataFrame(rows)


if __name__ == "__main__":
    ROOT.mkdir(exist_ok=True)
    frame = candidates()
    path = ROOT / "f1_pit_candidates.csv"
    frame.to_csv(path, index=False)
    print(f"{len(frame)} candidates, {frame.session_id.nunique()} races, {frame.decision_id.nunique()} decision IDs -> {path}")
