#!/usr/bin/env python3
"""Check localization/zh-TW/strings.txt against the English string table.

Every line is "IDS_NAME<TAB>translation". The check fails when a name is not
in res/strings/strings.res.h, has no English text in strings.res.cpp, is
translated twice (by name or number), or when its printf placeholders differ
from the English text (the game would format garbage or crash).
Runs in ctest (zh_tw_translations); build_zh_tw_patch.py uses the result.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PRINTF_TOKEN = re.compile(r"%%|%[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn]")


def load_translations(root=ROOT):
    """Return {string number: translation}; raise ValueError listing every problem."""
    symbols = dict(re.findall(
        r"^\s*(IDS_\w+)\s*=\s*(\d+)\s*,",
        (root / "res/strings/strings.res.h").read_text(),
        re.MULTILINE,
    ))
    originals = dict(re.findall(
        r'\{\s*(IDS_\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*\}',
        (root / "res/strings/strings.res.cpp").read_text(encoding="cp1252"),
    ))
    path = root / "localization/zh-TW/strings.txt"
    entries, names, errors = {}, set(), []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        where = f"{path.name}:{lineno}"
        if "\t" not in line:
            errors.append(f"{where}: expected IDS_NAME<TAB>text")
            continue
        symbol, translation = line.split("\t", 1)
        if symbol not in symbols:
            errors.append(f"{where}: {symbol} is not in strings.res.h")
            continue
        if symbol not in originals:
            errors.append(f"{where}: {symbol} has no English text in strings.res.cpp")
            continue
        number = int(symbols[symbol])
        if symbol in names or number in entries:
            errors.append(f"{where}: {symbol} ({number}) is translated twice")
            continue
        if not translation.strip():
            errors.append(f"{where}: {symbol} has an empty translation")
            continue
        if PRINTF_TOKEN.findall(originals[symbol]) != PRINTF_TOKEN.findall(translation):
            errors.append(f"{where}: {symbol} placeholders {PRINTF_TOKEN.findall(translation)} "
                          f"differ from the English {PRINTF_TOKEN.findall(originals[symbol])}")
            continue
        names.add(symbol)
        entries[number] = translation
    if errors:
        raise ValueError("\n".join(errors))
    return entries


def main():
    try:
        entries = load_translations()
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"{len(entries)} zh-TW translations OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
