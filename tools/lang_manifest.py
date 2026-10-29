#!/usr/bin/env python3
"""Write lang.json into every language folder: which files are text and which are voice.

    tools/lang_manifest.py <lang dir>        # the folder with gothic_base, gothic_ru_11, ...

A language folder is named gothic_<code>_<T><V>: T = the language has translated text,
V = it has voice-over. lang.json lists the files that belong to the language:

  text  : scripts and dialogue lines, fonts, localized textures, readmes
  voice : speech archives (Data/speech*.vdf) and the cutscenes (their sound track is spoken)
  subtitledVideo : cutscenes of a text-only language (subtitles burned into the picture,
          no voice); kept in the folder, not packed for the web (they would replace the
          voice language's cutscenes of the same name)

tools/pack_langs.py builds the web packs from these lists, so a build with its own
files only needs a different lang.json.
"""
import json
import os
import re
import sys

NAMES = {
    'en': 'English', 'de': 'Deutsch', 'pl': 'Polski', 'ru': 'Russian',
    'fr': 'Français', 'it': 'Italiano', 'cs': 'Čeština', 'es': 'Español',
}


def is_voice(rel):
    r = rel.lower()
    return r.startswith('data/speech') and r.endswith('.vdf')


def is_video(rel):
    return rel.lower().endswith('.bik')


def main():
    root = sys.argv[1]
    for name in sorted(os.listdir(root)):
        m = re.fullmatch(r'gothic_([a-z]{2})_([01])([01])', name)
        if not m:
            continue
        code, has_text, has_voice = m.group(1), m.group(2) == '1', m.group(3) == '1'
        folder = os.path.join(root, name)
        files = []
        for d, _, fs in os.walk(folder):
            for f in fs:
                rel = os.path.relpath(os.path.join(d, f), folder).replace(os.sep, '/')
                if rel != 'lang.json':
                    files.append(rel)
        files.sort(key=str.lower)
        voice = [f for f in files if is_voice(f) or (has_voice and is_video(f))]
        # cutscenes of a text-only language: subtitles burned into the picture, no voice
        subtitled = [f for f in files if f not in voice and is_video(f)]
        text = [f for f in files if f not in voice and f not in subtitled]
        out = {
            'id': code,
            'name': NAMES.get(code, code),
            'hasText': has_text,
            'hasVoice': has_voice,
            'files': {'text': text, 'voice': voice, 'subtitledVideo': subtitled},
        }
        with open(os.path.join(folder, 'lang.json'), 'w', encoding='utf-8') as fh:
            json.dump(out, fh, ensure_ascii=False, indent=1)
        sz = lambda l: sum(os.path.getsize(os.path.join(folder, f)) for f in l) / 1e6
        print(f'{name}: text {len(text)} files {sz(text):.1f} MB, voice {len(voice)} files {sz(voice):.1f} MB')


if __name__ == '__main__':
    main()
