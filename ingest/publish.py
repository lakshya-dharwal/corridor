"""Publishes a snapshot the service can hot-reload, and keeps the generation.

The live file is replaced atomically. The same bytes are copied to
`<directory of the live file>/history/<feed>/<epoch>.tsv`. A caller that
fails before publish leaves both the live file and the history untouched.
The newest `keep` generations of each feed are retained.
"""

from __future__ import annotations

import os


def publish(path: str, body: str, *, feed: str, epoch: int, keep: int = 48) -> None:
    directory = os.path.dirname(path) or "."
    os.makedirs(directory, exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        handle.write(body)
    os.replace(temporary, path)

    history = os.path.join(directory, "history", feed)
    os.makedirs(history, exist_ok=True)
    archived = os.path.join(history, f"{int(epoch)}.tsv")
    temporary_copy = archived + ".tmp"
    with open(path, "rb") as source, open(temporary_copy, "wb") as handle:
        handle.write(source.read())
    os.replace(temporary_copy, archived)

    generations = []
    for name in os.listdir(history):
        if not name.endswith(".tsv"):
            continue
        stem = name[: -len(".tsv")]
        if stem.isdigit():
            generations.append((int(stem), name))
    generations.sort()
    for _, name in generations[:-keep]:
        try:
            os.remove(os.path.join(history, name))
        except OSError:
            pass
