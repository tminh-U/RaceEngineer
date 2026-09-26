"""Conservative vehicle groups for pit-strategy data ingestion."""

import re


def vehicle_category(*, car_model="", class_label="", homologation="", source_domain=""):
    """Return f1, formula, gt, prototype, touring, or unknown.

    Source class and model text must agree when both identify a group.
    An unknown car stays unknown; it must not inherit a GT or F1 model.
    """
    found = set()
    for source, value in (("homologation", homologation), ("class", class_label),
                          ("domain", source_domain), ("model", car_model)):
        name = re.sub(r"[_\-/]+", " ", str(value) if value is not None else "").casefold().strip()
        if not name:
            continue
        if re.search(r"\b(?:f1|formula\s*(?:1|one))\b", name):
            found.add("f1")
        elif source == "model" and re.search(r"\bgt3\s*rs\b", name):
            continue  # Road GT3 RS is not a verified GT3 race car.
        elif re.search(r"\b(?:gt[234]|gt300|gt500|gtdpro|gtd|gtlm|gte|lmgt3|grand\s*touring)\b", name):
            found.add("gt")
        elif re.search(r"\b(?:lmp[123]|lmh|lmdh|gtp|hypercar|prototype)\b", name):
            found.add("prototype")
        elif re.search(r"\b(?:formula|f[234]|indycar|super\s*formula)\b", name):
            found.add("formula")
        elif re.search(r"\b(?:tcr|tcx|touring)\b", name):
            found.add("touring")
    return next(iter(found)) if len(found) == 1 else "unknown"
