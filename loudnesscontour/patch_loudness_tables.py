"""Patch loudness.c coefficient tables from gen_*.c fragments."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOUDNESS_C = ROOT / "src" / "loudness.c"

TABLES = [
    ("loudness_quotients_44100hz", "gen_precise_44100.c", "biquad_quotients_precise_t"),
    ("loudness_quotients_48000hz", "gen_precise_48000.c", "biquad_quotients_precise_t"),
    ("loudness_quotients_44100hz", "gen_fast_44100.c", "biquad_quotients_fast_t"),
    ("loudness_quotients_48000hz", "gen_fast_48000.c", "biquad_quotients_fast_t"),
]


def replace_table(text: str, array_name: str, body: str, type_name: str) -> str:
    marker = (
        f"static const {type_name}\n"
        f"{array_name}[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {{"
    )
    idx = text.find(marker)
    if idx == -1:
        raise ValueError(f"Array {array_name} ({type_name}) not found")
    open_brace = text.find("{", idx + len(marker) - 1)
    close_brace = text.find("\n};", open_brace)
    if close_brace == -1:
        raise ValueError(f"Could not locate end for {array_name}")
    return text[: open_brace + 1] + "\n" + body + text[close_brace:]


TABLES = [
    ("loudness_quotients_44100hz", "gen_precise_44100.c", "biquad_quotients_precise_t"),
    ("loudness_quotients_48000hz", "gen_precise_48000.c", "biquad_quotients_precise_t"),
    ("loudness_quotients_44100hz", "gen_fast_44100.c", "biquad_quotients_fast_t"),
    ("loudness_quotients_48000hz", "gen_fast_48000.c", "biquad_quotients_fast_t"),
]


def main():
    text = LOUDNESS_C.read_text(encoding="utf-8")
    for array_name, gen_name, type_name in TABLES:
        body = (ROOT / gen_name).read_text(encoding="utf-8").rstrip() + "\n"
        text = replace_table(text, array_name, body, type_name)
        print(f"Patched {type_name} {array_name} from {gen_name}")
    LOUDNESS_C.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
