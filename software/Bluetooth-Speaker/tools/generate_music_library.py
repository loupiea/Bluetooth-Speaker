#!/usr/bin/env python3
"""Generate the local music JSON list and ESP-IDF C library."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import urllib.parse


ROOT = Path(__file__).resolve().parents[1]
MUSIC_DIR = ROOT / "music"
MUSIC_JSON = MUSIC_DIR / "music_list.json"
C_LIBRARY = ROOT / "ai" / "src" / "ai_music_library.c"
DEFAULT_BASE_URL = "http://lubancat.local:8081/music/"
SUPPORTED_EXTENSIONS = {".mp3", ".wav"}


def c_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def normalize_base_url(base_url: str) -> str:
    if not base_url.endswith("/"):
        base_url += "/"
    return base_url


def track_from_file(path: Path, base_url: str) -> dict[str, str]:
    stem = path.stem.strip()
    if " - " in stem:
        artist, title = stem.split(" - ", 1)
    elif "-" in stem:
        artist, title = stem.split("-", 1)
    else:
        artist, title = "本地音乐", stem

    artist = artist.strip() or "本地音乐"
    title = title.strip() or stem
    encoded_name = urllib.parse.quote(path.name)
    return {
        "name": title,
        "artist": artist,
        "format": path.suffix.lower().lstrip("."),
        "file": path.name,
        "url": f"{base_url}{encoded_name}",
    }


def scan_tracks(music_dir: Path, base_url: str) -> list[dict[str, str]]:
    files = [
        path
        for path in music_dir.iterdir()
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
    ]
    files.sort(key=lambda path: path.name.casefold())
    return [track_from_file(path, base_url) for path in files]


def write_json(tracks: list[dict[str, str]], path: Path, base_url: str) -> None:
    payload = {
        "base_url": base_url,
        "tracks": tracks,
    }
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def write_c_library(tracks: list[dict[str, str]], path: Path) -> None:
    lines: list[str] = [
        "/* 自动生成：不要手动修改。运行 tools/generate_music_library.py 更新。 */",
        '#include "ai_music_library.h"',
        "",
        "#include <stdio.h>",
        "#include <string.h>",
        "",
        "static const ai_music_track_t s_tracks[] = {",
    ]
    for track in tracks:
        lines.extend(
            [
                "    {",
                f'        .name = "{c_escape(track["name"])}",',
                f'        .artist = "{c_escape(track["artist"])}",',
                f'        .url = "{c_escape(track["url"])}",',
                "    },",
            ]
        )
    lines.extend(
        [
            "};",
            "",
            "size_t ai_music_library_count(void)",
            "{",
            "    return sizeof(s_tracks) / sizeof(s_tracks[0]);",
            "}",
            "",
            "const ai_music_track_t *ai_music_library_get(size_t index)",
            "{",
            "    if (index >= ai_music_library_count()) {",
            "        return NULL;",
            "    }",
            "    return &s_tracks[index];",
            "}",
            "",
            "const ai_music_track_t *ai_music_library_find(const char *query)",
            "{",
            "    if (query == NULL || query[0] == '\\0') {",
            "        return NULL;",
            "    }",
            "    for (size_t i = 0; i < ai_music_library_count(); ++i) {",
            "        if (strstr(s_tracks[i].name, query) != NULL ||",
            "            strstr(s_tracks[i].artist, query) != NULL ||",
            "            strstr(query, s_tracks[i].name) != NULL) {",
            "            return &s_tracks[i];",
            "        }",
            "    }",
            "    return NULL;",
            "}",
            "",
            "esp_err_t ai_music_library_format_list(char *buffer, size_t buffer_size)",
            "{",
            "    if (buffer == NULL || buffer_size == 0) {",
            "        return ESP_ERR_INVALID_ARG;",
            "    }",
            "",
            "    int written = snprintf(buffer, buffer_size, \"本地歌单：\");",
            "    if (written < 0 || (size_t)written >= buffer_size) {",
            "        return ESP_ERR_NO_MEM;",
            "    }",
            "",
            "    size_t used = (size_t)written;",
            "    for (size_t i = 0; i < ai_music_library_count(); ++i) {",
            "        written = snprintf(buffer + used,",
            "                           buffer_size - used,",
            "                           \"%s《%s》\",",
            "                           i == 0 ? \"\" : \"、\",",
            "                           s_tracks[i].name);",
            "        if (written < 0 || (size_t)written >= buffer_size - used) {",
            "            return ESP_ERR_NO_MEM;",
            "        }",
            "        used += (size_t)written;",
            "    }",
            "    return ESP_OK;",
            "}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--music-dir",
        type=Path,
        default=MUSIC_DIR,
        help="Directory containing local .mp3 and .wav files.",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="HTTP base URL that ESP32 uses to fetch music files.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    base_url = normalize_base_url(args.base_url)
    tracks = scan_tracks(args.music_dir, base_url)
    write_json(tracks, MUSIC_JSON, base_url)
    write_c_library(tracks, C_LIBRARY)
    print(f"Generated {len(tracks)} tracks")
    print(f"- {MUSIC_JSON.relative_to(ROOT)}")
    print(f"- {C_LIBRARY.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
