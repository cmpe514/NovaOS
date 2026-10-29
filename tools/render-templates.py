#!/usr/bin/env python3
"""Replace %%TOKEN%% placeholders in every text file under the given directories.

    render-templates.py TOKENS.json DIR [DIR...]

Fails if a placeholder is left unresolved, so a typo can never reach the image."""
import json, os, re, sys

tokens = json.load(open(sys.argv[1]))
pattern = re.compile(r"%%([A-Z][A-Z0-9_]*)%%")
problems = []
for top in sys.argv[2:]:
    for dirpath, _, files in os.walk(top):
        for name in files:
            path = os.path.join(dirpath, name)
            if os.path.islink(path):
                continue
            with open(path, "rb") as f:
                raw = f.read()
            if b"\0" in raw[:4096]:
                continue  # binary (images)
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                continue
            def sub(m):
                key = m.group(1)
                if key not in tokens:
                    problems.append(f"{path}: unknown placeholder %%{key}%%")
                    return m.group(0)
                return str(tokens[key])
            new = pattern.sub(sub, text)
            if new != text:
                mode = os.stat(path).st_mode
                with open(path, "w", encoding="utf-8") as f:
                    f.write(new)
                os.chmod(path, mode)
if problems:
    sys.exit("render-templates: " + "\n".join(problems))
