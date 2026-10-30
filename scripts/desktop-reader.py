#!/usr/bin/env python3
"""Assert actual Orca speech and AT-SPI text in the private OS desktop profile."""
import os
from pathlib import Path
import subprocess
import time

import pyatspi
from gi.repository import Atspi

speech = Path(os.environ["NEBULA_ORCA_LOG"])


def wait(check, message, seconds=6):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError(message)


def keys(*names):
    subprocess.run(["xdotool", "key", "--clearmodifiers", "--delay", "70", *names],
                   check=True, timeout=10)


def find(name):
    def visit(node):
        if node.name == name:
            return node
        for child in node:
            result = visit(child)
            if result:
                return result
        return None
    for app in pyatspi.Registry.getDesktop(0):
        if app.name == "desktop_tests":
            app.set_cache_mask(Atspi.Cache.NONE)
            app.clearCache()
            return visit(app)
    return None


def spoken(text, start=0):
    wait(lambda: any(text in line for line in speech.read_text()[start:].splitlines()
                     if "SPEECH OUTPUT:" in line), "Orca did not speak: " + text, seconds=15)
    print("Orca spoke:", text, flush=True)


def details(expected, spoken_text=None):
    start = len(speech.read_text())
    keys("F8")
    subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Block details$",
                    "windowactivate", "--sync"], check=True, timeout=10)
    field = wait(lambda: find("Block details"), "Block details not exposed")
    # The dialog and its text share a name. Find the focused text through AT-SPI.
    def focused_text(node):
        if node.getState().contains(pyatspi.STATE_FOCUSED):
            try:
                return node.queryText()
            except NotImplementedError:
                pass
        for child in node:
            result = focused_text(child)
            if result:
                return result
        return None
    try:
        text = wait(lambda: focused_text(field), "Block details text not focused")
        contents = text.getText(0, -1)
        print("AT-SPI details:", contents, flush=True)
        assert expected in contents, contents
        # Select the relevant line using native keys. Orca treats Ctrl+A as
        # "entire document selected", without speaking the selected contents.
        time.sleep(.3)
        target = spoken_text or expected
        line = next(i for i, value in enumerate(contents.splitlines()) if target in value)
        keys("ctrl+Home", *(["Down"] * line), "Home", "shift+End")
        wait(lambda: text.getNSelections() == 1, "Details selection missing")
        a, b = text.getSelection(0)
        assert target in text.getText(a, b), text.getText(a, b)
        spoken(target, start)
    finally:
        close_start = len(speech.read_text())
        keys("Escape")
        subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Nebula$",
                        "windowactivate", "--sync"], check=True, timeout=10)
        editor.queryComponent().grabFocus()
        spoken("Page block editor", close_start)
    wait(lambda: editor.getState().contains(pyatspi.STATE_FOCUSED), "Editor focus not restored")


def go_to(snippet):
    target = text.getText(0, -1).index(snippet)
    keys("ctrl+Home")
    for _ in range(50):
        caret = text.caretOffset
        if target <= caret <= target + len(snippet):
            print("Native caret:", caret, snippet, flush=True)
            return
        keys("Down")
        time.sleep(.05)
    raise AssertionError(("Native navigation failed", snippet, text.caretOffset))


editor = wait(lambda: find("Page block editor"), "Editor missing from AT-SPI")
subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Nebula$",
                "windowactivate", "--sync"], check=True, timeout=10)
editor.queryComponent().grabFocus()
wait(lambda: editor.getState().contains(pyatspi.STATE_FOCUSED), "Initial editor focus missing")
assert editor.getState().contains(pyatspi.STATE_EDITABLE)
text = editor.queryText()
assert "A day in Nebula" in text.getText(0, -1)
go_to("A day in Nebula")
details("Heading level 1")
go_to("Nested blocks move")
details("Nesting level 1", "Nested blocks move with their parent.")
go_to("Try selecting text")
details("Task unchecked")
go_to("A stable reference")
details("Local block reference", "A stable reference to")
details("Local block reference", "Local block reference")
go_to("\ufffc")
details("Image: Sample image representation", "Image: Sample image representation")
keys("Down")
details("Attachment: journal-notes.txt", "Attachment: journal-notes.txt")
start = len(speech.read_text())
keys("ctrl+Home", "shift+Down")
wait(lambda: text.getNSelections() == 1, "Selection not exported")
a, b = text.getSelection(0)
assert text.getText(a, b) == "A day in Nebula\n", (a, b, text.getText(a, b))
spoken("selected", start)
print("AT-SPI: native focus, all block semantics, card labels and selection verified", flush=True)
