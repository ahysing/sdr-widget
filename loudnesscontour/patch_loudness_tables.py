"""Patch loudness_fast.c coefficient tables from gen_*.c fragments."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOUDNESS_FAST_C = ROOT / "src" / "loudness_fast.c"

TABLES = [
    ("lowshelf_and_volume_44100hz", "gen_and_volume_44100.c", "biquad_quotients_fast_t"),
    ("lowshelf_and_volume_48000hz", "gen_and_volume_48000.c", "biquad_quotients_fast_t"),
    ("lowshelf_no_volume_44100hz", "gen_no_volume_44100.c", "biquad_quotients_fast_t"),
    ("lowshelf_no_volume_48000hz", "gen_no_volume_48000.c", "biquad_quotients_fast_t"),
]


def replace_table(text: str, array_name: str, body: str, type_name: str) -> str:
    marker = (
        f"static const {type_name}\n"
        f"{array_name}[LOUDNESS_NUM_EQUALIZER_STEPS] = {{"
    )
    alt_marker = (
        f"static const {type_name}\n"
        f"{array_name}[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {{"
    )
    idx = text.find(marker)
    if idx == -1:
        idx = text.find(alt_marker)
        if idx == -1:
            raise ValueError(f"Array {array_name} ({type_name}) not found")
        marker = alt_marker
    open_brace = text.find("{", idx + len(marker) - 1)
    close_brace = text.find("\n};", open_brace)
    if close_brace == -1:
        raise ValueError(f"Could not locate end for {array_name}")
    return text[: open_brace + 1] + "\n" + body + text[close_brace:]


def main():
    text = LOUDNESS_FAST_C.read_text(encoding="utf-8")
    for array_name, gen_name, type_name in TABLES:
        gen_path = ROOT / gen_name
        if not gen_path.exists():
            print(f"Skip {array_name}: {gen_name} not found")
            continue
        body = gen_path.read_text(encoding="utf-8").rstrip() + "\n"
        text = replace_table(text, array_name, body, type_name)
        print(f"Patched {type_name} {array_name} from {gen_name}")
    LOUDNESS_FAST_C.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
