"""Convert create2biquads.py stdout into loudness.c table bodies."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FILES = [
    ("tmp_lut_precise_44100.txt", "LOUDNESS_Q61_ONE", "gen_precise_44100.c"),
    ("tmp_lut_precise_48000.txt", "LOUDNESS_Q61_ONE", "gen_precise_48000.c"),
    ("tmp_lut_fast_44100.txt", "LOUDNESS_Q28_ONE", "gen_fast_44100.c"),
    ("tmp_lut_fast_48000.txt", "LOUDNESS_Q28_ONE", "gen_fast_48000.c"),
]


def parse_lut(path: Path):
    text = path.read_text(encoding="utf-8-sig")
    sections = []
    header_pat = r"Biquad coefficients \(a0 = 1\), fs = [^\n]+, phons = (\d+) phon\n"
    for m in re.finditer(header_pat, text):
        phon = int(m.group(1))
        tail = text[m.end():]
        rows = []
        for line in tail.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            if not stripped.startswith("{"):
                break
            rows.append(stripped.rstrip(","))
            if len(rows) == 2:
                break
        if len(rows) != 2:
            raise ValueError(f"{path}: phon {phon} has {len(rows)} rows")
        sections.append((phon, rows))
    if len(sections) != 13:
        raise ValueError(f"{path}: expected 13 sections, got {len(sections)}")
    return sections


def format_table(sections, neutral_suffix: str) -> str:
    out = []
    for phon, rows in sections:
        out.append(f"    /* {phon} phon */")
        out.append("    {")
        for row in rows:
            row = re.sub(r",\s*/\* a0=.*\*/", "", row)
            out.append(f"        {row},")
        out.append("    },")
    out.append("    /* 80 phon */")
    out.append("    {")
    out.append(f"        {{ 0, 0, {neutral_suffix}, 0, 0 }},")
    out.append(f"        {{ 0, 0, {neutral_suffix}, 0, 0 }},")
    out.append("    },")
    return "\n".join(out)


def main():
    for src_name, neutral, out_name in FILES:
        src = ROOT / src_name
        body = format_table(parse_lut(src), neutral)
        out = ROOT / out_name
        out.write_text(body + "\n", encoding="utf-8")
        print(out_name, "ok")


if __name__ == "__main__":
    main()
