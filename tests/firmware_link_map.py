"""Allocated-section provenance from GNU ld, LLVM lld and Arm linker maps."""
from __future__ import annotations

from dataclasses import dataclass, field
import re


@dataclass(frozen=True)
class Section:
    address: int
    size: int
    origin: str


@dataclass
class LinkMap:
    sections: list[Section] = field(default_factory=list)
    symbols: dict[str, set[int]] = field(default_factory=dict)

    def symbol(self, name: str, address: int) -> None:
        # Arm maps may show the Thumb ISA bit; nm reports the code address.
        self.symbols.setdefault(name, set()).add(address & ~1)

    def verify(self, symbol: str, address: int, archive: str, member: str) -> None:
        address &= ~1
        if self.symbols.get(symbol) != {address}:
            raise ValueError(f"linker map does not match ELF address for {symbol}")
        owners = {
            section.origin for section in self.sections
            if section.address <= address < section.address + section.size
        }
        # Require one defining input section, not an archive LOAD/extraction line
        # or a discarded section. Public CMake targets have deterministic names.
        expected = re.compile(
            r"(?:.*[/\\])?" + re.escape(archive)
            + r"\((?:.*[/\\])?" + re.escape(member) + r"\.(?:o|obj)\)"
        )
        if len(owners) != 1 or not expected.fullmatch(next(iter(owners), "")):
            raise ValueError(
                f"wrong defining object for {symbol}: {sorted(owners)}; "
                f"expected {archive}({member}.o)"
            )


def parse_map(text: str) -> LinkMap:
    lines = text.splitlines()
    if "Linker script and memory map" in lines:
        return _gnu(lines[lines.index("Linker script and memory map") + 1:])
    if any(re.fullmatch(r"\s*VMA\s+LMA\s+Size\s+Align\s+Out\s+In\s+Symbol\s*", line) for line in lines):
        return _lld(lines)
    if "Memory Map of the image" in lines:
        return _arm(lines)
    raise ValueError("unsupported linker map: require GNU ld, LLVM lld, or Arm --map --symbols output")


def _is_code(name: str) -> bool:
    return name == ".text" or name.startswith((".text.", ".gnu.linkonce.t."))


def _gnu(lines: list[str]) -> LinkMap:
    result = LinkMap()
    # Input sections may wrap their name onto the preceding line. Output
    # sections have no leading whitespace and are deliberately excluded.
    section = re.compile(r"^\s+(?:\.\S+\s+)?(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+\.(?:o|obj)\)?)\s*$")
    symbol = re.compile(r"^\s+(0x[0-9a-fA-F]+)\s+([A-Za-z_][\w.$]*)\s*$")
    section_name = ""
    for line in lines:
        name = re.match(r"^ (\.\S+)(?:\s|$)", line)
        if name:
            section_name = name[1]
        elif line and not line[0].isspace():
            section_name = ""
        if line.startswith("Cross Reference Table"):
            break
        match = section.fullmatch(line)
        if match and _is_code(section_name):
            result.sections.append(Section(int(match[1], 16), int(match[2], 16), match[3]))
        match = symbol.fullmatch(line)
        if match:
            result.symbol(match[2], int(match[1], 16))
    return result


def _lld(lines: list[str]) -> LinkMap:
    result = LinkMap()
    row = re.compile(r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+\d+\s+(.+?)\s*$")
    for line in lines:
        match = row.fullmatch(line)
        if not match:
            continue
        address, size, value = int(match[1], 16), int(match[2], 16), match[3]
        if ":(" in value:
            origin, section_name = value.rsplit(":(", 1)
            if _is_code(section_name.rstrip(")")):
                result.sections.append(Section(address, size, origin))
        elif re.fullmatch(r"[A-Za-z_][\w.$]*", value):
            result.symbol(value, address)
    return result


def _arm(lines: list[str]) -> LinkMap:
    result = LinkMap()
    # Symbol table gives name/address; memory map gives the archive/member.
    # The table's bare member name alone is not sufficient provenance.
    symbol = re.compile(r"^\s*([A-Za-z_][\w.$]*)\s+(0x[0-9a-fA-F]+)\s+(?:Thumb|ARM) Code\s+\d+\s+.+$")
    section = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+Code\s+RO\s+\d+\s+(?:\*\s+)?\S+\s+(.+?)\s*$")
    in_memory_map = False
    for line in lines:
        if line == "Memory Map of the image":
            in_memory_map = True
        if not in_memory_map:
            match = symbol.fullmatch(line)
            if match:
                result.symbol(match[1], int(match[2], 16))
        else:
            match = section.fullmatch(line)
            if match:
                result.sections.append(Section(int(match[1], 16), int(match[2], 16), match[3]))
    return result
