#!/usr/bin/env python3
"""
generate-icon-names.py — Regenerate lambda/include/Lambda/UI/IconName.hpp from the
Material Symbols Rounded variable font.

Requirements:
    pip install fonttools brotli

Usage:
    python3 lambda/tools/generate-icon-names.py                        # from repo root
    python3 lambda/tools/generate-icon-names.py --font path/to/font    # custom font path
    python3 lambda/tools/generate-icon-names.py --output path/to/out   # custom output path
"""

import argparse
import os
import sys
from pathlib import Path

try:
    from fontTools.ttLib import TTFont
except ImportError:
    print("Error: fonttools not installed.  Run: pip install fonttools brotli", file=sys.stderr)
    sys.exit(1)


# ── C++ keywords (C++23) ────────────────────────────────────────────────────

CPP_KEYWORDS = frozenset({
    'alignas', 'alignof', 'and', 'and_eq', 'asm', 'auto', 'bitand',
    'bitor', 'bool', 'break', 'case', 'catch', 'char', 'char8_t',
    'char16_t', 'char32_t', 'class', 'compl', 'concept', 'const',
    'consteval', 'constexpr', 'constinit', 'const_cast', 'continue',
    'co_await', 'co_return', 'co_yield', 'decltype', 'default',
    'delete', 'do', 'double', 'dynamic_cast', 'else', 'enum',
    'explicit', 'export', 'extern', 'false', 'float', 'for', 'friend',
    'goto', 'if', 'inline', 'int', 'long', 'mutable', 'namespace',
    'new', 'noexcept', 'not', 'not_eq', 'nullptr', 'operator', 'or',
    'or_eq', 'private', 'protected', 'public', 'register',
    'reinterpret_cast', 'requires', 'return', 'short', 'signed',
    'sizeof', 'static', 'static_assert', 'static_cast', 'struct',
    'switch', 'template', 'this', 'thread_local', 'throw', 'true',
    'try', 'typedef', 'typeid', 'typename', 'union', 'unsigned',
    'using', 'virtual', 'void', 'volatile', 'wchar_t', 'while',
    'xor', 'xor_eq',
})


def to_pascal(snake: str) -> str:
    """Convert snake_case glyph name to PascalCase C++ identifier."""
    parts = snake.split('_')
    result = ''.join(p.capitalize() for p in parts if p)
    if result and result[0].isdigit():
        result = '_' + result
    if result.lower() in CPP_KEYWORDS:
        result += '_'
    return result


def extract_icons(font_path: str) -> list[tuple[str, int, str]]:
    """
    Extract (PascalName, codepoint, original_name) from a Material Symbols font.

    For glyphs with multiple codepoints (aliases), picks the lowest BMP codepoint.
    All aliases render identically — the choice is cosmetic.
    """
    font = TTFont(font_path)
    cmap = font.getBestCmap()

    # glyph_name → [codepoints]
    reverse: dict[str, list[int]] = {}
    for cp, name in cmap.items():
        if cp >= 0xE000 and not name.endswith('.fill'):
            reverse.setdefault(name, []).append(cp)

    # Pick one codepoint per glyph
    icons: dict[int, str] = {}
    for name, cps in reverse.items():
        bmp = [c for c in cps if c <= 0xFFFF]
        icons[min(bmp) if bmp else min(cps)] = name

    font.close()

    # Build sorted entries
    entries = [(to_pascal(name), cp, name) for cp, name in icons.items()]
    entries.sort(key=lambda e: e[0].lower())
    return entries


def generate_header(entries: list[tuple[str, int, str]]) -> str:
    """Generate the IconName.hpp content."""
    lines = [
        '#pragma once',
        '',
        '#include <cstdint>',
        '',
        'namespace lambdaui {',
        '',
        '/// Material Symbols Rounded — complete icon set.',
        '///',
        '/// Generated from the MaterialSymbolsRounded variable font.',
        "/// Each enumerator's value is a Unicode codepoint in the Private Use Area.",
        '/// Some icons have multiple codepoints (aliases) in the font; this enum',
        '/// uses the lowest BMP codepoint per glyph.  All aliases render identically.',
        '///',
        '/// Source: https://github.com/google/material-design-icons',
        '/// License: Apache 2.0',
        '///',
        f'/// Total icons: {len(entries)}',
        '///',
        "/// C++ keyword collisions are suffixed with '_' (Delete_, Switch_, Public_).",
        "/// Names starting with a digit are prefixed with '_' (_10k, _123).",
        'enum class IconName : char32_t {',
    ]

    align = 40
    for pascal, cp, _ in entries:
        lines.append(f'  {pascal.ljust(align)}= 0x{cp:04X},')

    lines += [
        '};',
        '',
        '/// Convert an IconName to a single-character UTF-32 value for text shaping.',
        'constexpr char32_t iconCodepoint(IconName name) noexcept {',
        '  return static_cast<char32_t>(name);',
        '}',
        '',
        '} // namespace lambdaui',
        '',
    ]

    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description='Generate IconName.hpp from Material Symbols font')
    parser.add_argument('--font', default=None,
                        help='Path to MaterialSymbolsRounded .ttf or .woff2')
    parser.add_argument('--output', default=None,
                        help='Output path (default: lambda/include/Lambda/UI/IconName.hpp)')
    args = parser.parse_args()

    # Resolve defaults relative to the framework root.
    lambda_root = Path(__file__).resolve().parent.parent
    font_path = args.font or str(lambda_root / 'resources' / 'fonts' / 'MaterialSymbolsRounded.ttf')
    output_path = args.output or str(lambda_root / 'include' / 'Lambda' / 'UI' / 'IconName.hpp')

    if not os.path.isfile(font_path):
        print(f'Error: font not found at {font_path}', file=sys.stderr)
        print(f'Place the MaterialSymbolsRounded TTF/WOFF2 there, or use --font.', file=sys.stderr)
        sys.exit(1)

    entries = extract_icons(font_path)
    header = generate_header(entries)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(header)

    print(f'Generated {len(entries)} icons → {output_path}')


if __name__ == '__main__':
    main()
