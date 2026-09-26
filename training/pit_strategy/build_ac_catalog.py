"""Build an exact-ID AC vehicle catalog from an Assetto Corsa cars directory."""

import json
import re
import sys
from pathlib import Path


# Exact metadata names keep these overrides from matching replaced mod folders.
# Verified against official Ferrari, Lotus, Maserati, McLaren, Alpine and Porsche sources.
F1_MODELS = {
    "ferrari_312t": "Ferrari 312T",
    "ks_ferrari_312_67": "Ferrari 312/67",
    "ks_ferrari_f138": "Ferrari F138",
    "ks_ferrari_f2004": "Ferrari F2004",
    "ks_ferrari_sf15t": "Ferrari SF15-T",
    "ks_ferrari_sf70h": "Ferrari SF70H",
    "ks_maserati_250f_12cyl": "Maserati 250F 12 cylinder",
    "ks_maserati_250f_6cyl": "Maserati 250F 6 cylinder",
    "ks_lotus_25": "Lotus Type 25",
    "lotus_49": "Lotus Type 49",
    "ks_lotus_72d": "Lotus 72D",
    "lotus_98t": "Lotus 98T",
    "vrc_1988_mclaren_mp4-4_r02": "McLaren-Honda MP4/4 R02",
    "vrc_1988_mclaren_mp4-4_r04": "McLaren-Honda MP4/4 R04",
    "vrc_1988_mclaren_mp4-4_r09": "McLaren-Honda MP4/4 R09",
    "vrc_1988_mclaren_mp4-4_r10": "McLaren-Honda MP4/4 R10",
    "rss_alpine_a525": "Alpine A525",
}
SPORTS_PROTOTYPES = {
    "ks_porsche_908_lh": "Porsche 908 LH",
    "ks_porsche_917_k": "Porsche 917 K",
}

# AC metadata often says only "race". Match the exact in-game name before
# applying a real-world class, and retain the primary source in the catalog.
VERIFIED_MODELS = {
    "ferrari_599xxevo": ("Ferrari 599XX EVO", "track_only", "xx_programme", "https://www.ferrari.com/en-EN/corse-clienti/articles/ferrari-racing-days-f1-clienti-and-xx-cars-light-up-sochi"),
    "ks_abarth500_assetto_corse": ("Abarth 500 Assetto Corse", "cup", "abarth_trophy", "https://www.media.stellantis.com/it-it/abarth/press/consegnate-le-prime-abarth-500-assetto-corse"),
    "ks_audi_sport_quattro_rally": ("Audi Sport quattro S1 E2", "rally", "group_b", "https://www.audi.com/en/sport/motorsport/motorsport-history/05-quattro-rallying/"),
    "ks_audi_tt_cup": ("Audi TT Cup", "cup", "audi_tt_cup", "https://www.audi.com/en/press-releases/audi-to-launch-racing-series-for-the-new-tt-1223"),
    "ks_ferrari_330_p4": ("Ferrari 330 P4", "prototype", "sports_prototype", "https://www.ferrari.com/en-US/magazine/articles/1965-Le-Mans-winner-returns-to-Fiorano"),
    "ks_ferrari_fxx_k": ("Ferrari FXX K", "track_only", "xx_programme", "https://www.ferrari.com/en-EN/corse-clienti/articles/ferrari-racing-days-f1-clienti-and-xx-cars-light-up-sochi"),
    "ks_ford_gt40": ("Ford GT40", "prototype", "sports_prototype", "https://media.ford.com/content/fordmedia/feu/gb/en/news/2021/08/12/2022-ford-gt-heritage-edition.html"),
    "ks_lotus_3_eleven": ("Lotus 3-Eleven", "track_only", "race_version", "https://www.lotuscars.com/en-NO/lotus-story/3-eleven"),
    "ks_mazda_mx5_cup": ("Mazda MX5 Cup", "cup", "mx5_cup", "https://www.mazdamotorsports.com/race-mazdas-2/pro-racing/mx-5-cup/"),
    "ks_porsche_917_30": ("Porsche 917/30 Spyder", "prototype", "can_am", "https://newsroom.porsche.com/en_US/2023/company/capturing-the-spirit-of-Porsche-33474.html"),
    "ks_porsche_935_78_moby_dick": ("Porsche 935/78 'Moby Dick'", "gt", "group_5", "https://newsroom.porsche.com/en/press-kits/50-years-porsche-turbo/Porsche-and-turbo-technology-in-motorsport--from-pioneer-to-world-champion.html"),
    "ks_toyota_celica_st185": ("Toyota Celica ST185 4WD Turbo", "rally", "group_a", "https://toyotagazooracing.com/jp/wrc/special/2018/the-wrc-chronicle-01/"),
    "ks_toyota_supra_mkiv_drift": ("Toyota Supra MKIV Drift", "drift", "drift", ""),
    "ks_toyota_supra_mkiv_tuned": ("Toyota Supra MKIV Time Attack", "time_attack", "time_attack", ""),
    "pagani_zonda_r": ("Pagani Zonda R", "track_only", "track_car", "https://www.pagani.com/press/the-pagani-huayra-r/"),
    "shelby_cobra_427sc": ("Shelby Cobra 427 S/C", "road", "competition_road", "https://shelby.com/Multimedia/Team-Shelby-Media/Cobras-at-PB"),
}

# These four folders are empty on this install. The class is inferred from the
# exact model ID, but the catalog records that no usable local car was found.
EMPTY_FOLDER_MODELS = {
    "ks_ferrari_488_challenge_evo": ("cup", "ferrari_challenge", "https://www.ferrari.com/en-CA/corse-clienti/articles/the-488-challenge-evo-unveiled-in-daytona"),
    "ks_ferrari_488_gt3_2020": ("gt", "gt3", "https://www.ferrari.com/en-EN/competizioni-gt/articles/the-ferrari-488-gt3-evo-2020-the-continuing-evolution-of-a-legend"),
    "ks_lamborghini_countach_s1": ("road", "street", "https://www.lamborghini.com/en-en/history/milestones"),
    "ks_lamborghini_gallardo_sl_s3": ("road", "street", "https://www.lamborghini.com/en-en/node/1052"),
}


def read_ui(path):
    text = path.read_text(encoding="utf-8-sig")
    # AC UI files contain literal line breaks in description strings.
    output = []
    quoted = escaped = False
    for char in text:
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            elif ord(char) < 32:
                char = " "
        elif char == '"':
            quoted = True
        output.append(char)
    return json.loads("".join(output))


def classify(car_id, metadata, source):
    vehicle_class = str(metadata.get("class", "")).strip()
    name = str(metadata.get("name", ""))
    evidence = f"{source} class={vehicle_class or '(missing)'}"

    def result(category, subclass, proof=""):
        return {
            "car_model": car_id,
            "category": category,
            "subclass": subclass,
            "evidence": f"{evidence}; {proof}" if proof else evidence,
        }

    if car_id in F1_MODELS and F1_MODELS[car_id].casefold() == name.casefold():
        return result("f1", "f1", f"manufacturer-verified exact-ID F1 override: {name}")
    if car_id in SPORTS_PROTOTYPES and SPORTS_PROTOTYPES[car_id].casefold() == name.casefold():
        return result("prototype", "sports_prototype", f"manufacturer-verified exact-ID prototype override: {name}")

    verified = VERIFIED_MODELS.get(car_id)
    if verified and verified[0].casefold() == name.casefold():
        _, category, subclass, url = verified
        row = result(category, subclass, f"exact ID and UI name match: {name}")
        if url:
            row["source_url"] = url
        return row

    if vehicle_class.casefold() == "street":
        return result("road", "street")

    raw_tags = metadata.get("tags")
    if not isinstance(raw_tags, list):
        return result("unknown", "unknown", "tags missing or not a list")
    tags = [str(tag).strip().lstrip("#") for tag in raw_tags]
    tag_set = {tag.casefold() for tag in tags}

    if vehicle_class.casefold() == "f1" or "f1" in tag_set:
        return result("f1", "f1", "explicit F1 class/tag")

    prototype_subclass = next(
        (marker for marker in ("lmdh", "lmh", "lmp1", "lmp2", "lmp3", "gtp") if marker in tag_set),
        "prototype_c" if tag_set & {"proto c", "prototype c"} else "unknown",
    )
    if vehicle_class.casefold() == "lmh" or prototype_subclass != "unknown" or tag_set & {"prototype", "prototype c", "proto c"}:
        subclass = "lmh" if vehicle_class.casefold() == "lmh" else prototype_subclass
        return result("prototype", subclass, f"prototype marker={subclass}")

    formula = (
        "formula" in tag_set
        or vehicle_class.casefold() == "formula"
        or re.search(r"\bformula\b", name, re.IGNORECASE)
        or bool(tag_set & {"singleseater", "single seater", "open wheeler"})
    )
    if formula:
        combined = " ".join((car_id, name, *tags))
        subclass = next(
            (code for code in ("f2", "f3", "f4")
             if re.search(rf"(?<![a-z0-9]){code}(?![a-z0-9])", combined, re.IGNORECASE)),
            "unknown",
        )
        return result("formula", subclass, "formula/single-seater metadata")

    touring = any(
        "touring" in tag
        or tag in {"dtm", "tcr", "tcx", "group a", "gra"}
        for tag in tag_set
    )
    if touring:
        subclass = next((tag for tag in ("dtm", "tcr", "tcx") if tag in tag_set), None)
        if not subclass and ("group a" in tag_set or "gra" in tag_set):
            subclass = "group_a"
        return result("touring", subclass or "unknown", "touring metadata tag")

    combined = " ".join((car_id, name))
    subclass_codes = ("gt1", "gt2", "gt3", "gt4", "gtlm", "lmgt3", "gte", "gtdpro", "gtd")
    gt_subclass = next(
        (code for code in subclass_codes
         if re.search(rf"(?<![a-z0-9]){code}(?![a-z0-9])", combined, re.IGNORECASE)),
        None,
    ) or next((code for code in subclass_codes if code in tag_set), None)
    if gt_subclass:
        subclass = "cup" if re.search(r"\bcup\b", combined, re.IGNORECASE) else gt_subclass
        return result("gt", subclass, f"GT marker={subclass}")

    # GTE-GT3 is broad metadata; by itself it does not prove a GT3 subclass.
    if vehicle_class.casefold() in {"race", "racing"} and tag_set & {"gt", "vintage gt", "gte-gt3"}:
        if "hypercars r" not in tag_set:
            subclass = "vintage_gt" if "vintage gt" in tag_set else "unknown"
            return result("gt", subclass, "race class with broad GT metadata tag")

    tag_summary = ",".join(tags) or "(none)"
    return result("unknown", "unknown", f"tags={tag_summary}; no unambiguous class marker")


def build_catalog(cars_dir):
    root = Path(cars_dir).expanduser().resolve()
    cars = []
    for directory in sorted((path for path in root.iterdir() if path.is_dir()), key=lambda path: path.name.casefold()):
        metadata_path = next(
            (path for path in (directory / "ui" / "ui_car.json", directory / "ui" / "dlc_ui_car.json") if path.is_file()),
            None,
        )
        if metadata_path is None:
            inferred = EMPTY_FOLDER_MODELS.get(directory.name) if not any(directory.iterdir()) else None
            if inferred:
                category, subclass, url = inferred
                cars.append({
                    "car_model": directory.name,
                    "category": category,
                    "subclass": subclass,
                    "evidence": "exact ID inference only; local car directory is empty",
                    "source_url": url,
                })
                continue
            cars.append({
                "car_model": directory.name,
                "category": "unknown",
                "subclass": "unknown",
                "evidence": "no ui/ui_car.json or ui/dlc_ui_car.json",
            })
            continue
        relative_source = metadata_path.relative_to(directory).as_posix()
        try:
            metadata = read_ui(metadata_path)
            if not isinstance(metadata, dict):
                raise ValueError("UI metadata is not a JSON object")
            cars.append(classify(directory.name, metadata, relative_source))
        except (OSError, ValueError) as error:
            cars.append({
                "car_model": directory.name,
                "category": "unknown",
                "subclass": "unknown",
                "evidence": f"{relative_source} unreadable: {error}",
            })

    return {
        "schema_version": 1,
        "simulator": "sim_ac",
        "source": [str(root)],
        "cars": cars,
    }


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_ac_catalog.py <cars-dir> <output-json>")
    catalog = build_catalog(sys.argv[1])
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    counts = {}
    for car in catalog["cars"]:
        counts[car["category"]] = counts.get(car["category"], 0) + 1
    print(f"Wrote {len(catalog['cars'])} cars to {output} ({counts})")


if __name__ == "__main__":
    main()
