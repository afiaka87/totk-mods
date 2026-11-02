"""Generate build-selected native headers from reviewed bindings and owned images.

Research profiles allow offline placement work while their full native contracts
are still being reviewed. They are explicitly marked and cannot be packaged.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from native_profile_data import BINDINGS, VALIDATED_VERSIONS, VERSIONS
from native_contracts import verify as verify_contracts

GUARDS = "inline constexpr totk::engine::CodeSite kSites[]{"
VALUE = re.compile(r"\b(k\w+)\s*=\s*(-?0x[0-9a-fA-F]+|-?\d+|true|false)\b(?=\s*[,;])")


def constants(source: str) -> dict[str, int | bool]:
    result = {}
    namespace = ""
    for line in source.split(GUARDS)[0].splitlines():
        nested = re.match(r"namespace (\w+) \{", line)
        if nested:
            namespace = nested[1] + "::"
        elif line.startswith("}"):
            namespace = ""
        for match in VALUE.finditer(line.split("//")[0]):
            value = match[2]
            result[namespace + match[1]] = value == "true" if value in ("true", "false") else int(value, 0)
    return result


def render(version: str, image, *, research: bool = False) -> tuple[str, dict]:
    from totk_version_port.images import BUILD_IDS
    if version not in VERSIONS:
        raise ValueError(f"Unsupported native profile: {version}")
    if image.build_id != BUILD_IDS[version]:
        raise ValueError("Native profile version and image identity differ")
    if version not in VALIDATED_VERSIONS and not research:
        raise ValueError(f"{version} native contract is still under review; only --research-profile may compile it")
    template = Path(__file__).resolve().parents[1] / "src/engine/DescendOffsets.hpp"
    source = template.read_text(encoding="utf-8")
    baseline = constants(source)
    for key, values in BINDINGS.items():
        if baseline.get(key) != values["1.2.1"]:
            raise ValueError(f"Baseline binding drift: {key}")
    selected = baseline | {key: values[version] for key, values in BINDINGS.items()}
    witnesses = verify_contracts(image, selected)
    if version != "1.2.1":
        prefix = source.split(GUARDS)[0]
        namespace = ""
        lines = []
        for line in prefix.splitlines(keepends=True):
            nested = re.match(r"namespace (\w+) \{", line)
            if nested:
                namespace = nested[1] + "::"
            elif line.startswith("}"):
                namespace = ""

            def replace(match):
                key = namespace + match[1]
                if key not in BINDINGS:
                    return match[0]
                value = BINDINGS[key][version]
                literal = str(value).lower() if isinstance(value, bool) else hex(value)
                return f"{match[1]} = {literal}"

            code, separator, comment = line.partition("//")
            lines.append(VALUE.sub(replace, code) + separator + comment)
        source = "".join(lines)
        # Guard every mapped native function/inline entry. Remaining layout witnesses must be added before the profile leaves research status.
        entries = sorted({values[version] for key, values in BINDINGS.items()
                          if "::" not in key and not isinstance(values[version], bool)
                          and 0x10000 <= values[version] < image.text_end})
        checked = {pc+n for pc in entries for n in (0,4)}
        for witness in witnesses:
            checked.update(witness['address']+n*4 for n in range(len(witness['words'])))
        # Coalesce overlapping witnesses while retaining every checked word. Sorted neighboring pairs also compress better in the fixed RAM image.
        guard_rows = []
        while checked:
            pc = min(checked)
            checked.remove(pc)
            second = image.word(pc+4) if pc+4 in checked else 0
            checked.discard(pc+4)
            guard_rows.append((pc,image.word(pc),second))
        source += GUARDS + '\n' + ''.join(
            f'    {{{pc:#x}, {first:#010x}, {second:#010x}}},\n' for pc,first,second in guard_rows)
        source += "};\n\n} // namespace descend::engine::offsets\n"
        status = 'offline-verified native profile' if version in VALIDATED_VERSIONS else 'research profile; not packageable'
        source = source.replace("// TotK 1.2.1 (build 9B4E43650501A4D4) sites and layouts.",
                                f"// TotK {version} (build {image.build_id}) {status}.")
    actual = constants(source)
    for key, values in BINDINGS.items():
        if actual[key] != values[version]:
            raise ValueError(f"Generated binding differs: {key}")
    guards = []
    for site, first, second in re.findall(r"\{(k\w+|0x[\da-fA-F]+),\s*(0x[\da-fA-F]+|0),\s*(0x[\da-fA-F]+|0)\}", source.split(GUARDS)[1]):
        pc = actual[site] if site.startswith("k") else int(site, 0)
        words = [int(first, 0), int(second, 0)]
        if image.word(pc) != words[0] or words[1] and image.word(pc + 4) != words[1]:
            raise ValueError(f"Native guard differs at {pc:x}")
        guards.append({"address": pc, "words": words})
    if version != '1.2.1':
        guarded = {g['address']+n*4 for g in guards for n,w in enumerate(g['words']) if w}
        required = {pc+n for pc in entries for n in (0,4)} | {
            w['address']+n*4 for w in witnesses for n in range(len(w['words']))}
        if not required <= guarded:
            raise ValueError('Coalesced native guards omitted an instruction witness')
    report = {"game_version": version, "build_id": image.build_id,
              "research_only": version not in VALIDATED_VERSIONS,
              "text_end": image.text_end, "header_sha256": hashlib.sha256(source.encode()).hexdigest(),
              "bindings": actual, "guards": guards, "layout_witnesses": witnesses}
    return source, report


def write(version: str, image, include: Path, *, research: bool = False) -> dict:
    source, report = render(version, image, research=research)
    target = include / "engine/DescendOffsets.hpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source, encoding="utf-8")
    (include.parent / "native-profile.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report
