from pathlib import Path
import sys

ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import preview  # noqa: E402

index = (ROOT / "data/index.html").read_text()
css = (ROOT / "data/style.css").read_text()
cpp = (ROOT / "src/main.cpp").read_text()
htmx = ROOT / "data/htmx.min.js"

assert htmx.exists() and htmx.stat().st_size > 1000
for needle in (
    "/led/power",
    "/led/mode",
    'name="mode"',
    'value="solid"',
    'value="blink"',
    'value="pattern"',
    'value="morse"',
    "#blink-controls",
):
    assert needle in index, needle

for needle in ("@media", 'input[type="range"]', ".htmx-request", ".input-row"):
    assert needle in css, needle

for needle in (
    "/led/power",
    "/led/mode",
    "/led/blink-delay",
    "/led/pattern",
    "/led/morse",
    "STEPS_DOUBLE",
    "STEPS_HEARTBEAT",
    "STEPS_SOS",
    "encodeMorse",
):
    assert needle in cpp, needle

# Verify Morse encoding timing and visual representation
res = preview.encode_morse("sos")
assert res is not None
clean, visual, steps = res
assert clean == "SOS"
assert visual == "... --- ..."
assert steps == [1, 1, 1, 1, 1, 3, 3, 1, 3, 1, 3, 3, 1, 1, 1, 1, 1, 7]

res_words = preview.encode_morse("  hi   there! ")
assert res_words is not None
clean_w, visual_w, steps_w = res_words
assert clean_w == "HI THERE!"
assert visual_w == ".... .. / - .... . .-. . -.-.--"
assert steps_w[-1] == 7
assert len(steps_w) % 2 == 0

assert preview.encode_morse("   ") is None

# Verify dynamic controls fragments for each mode
preview.state["mode"] = "solid"
assert preview.blink_controls() == '<div id="blink-controls"></div>'

preview.state["mode"] = "blink"
assert "/led/blink-delay" in preview.blink_controls()

preview.state["mode"] = "pattern"
pattern_html = preview.blink_controls()
assert "/led/pattern" in pattern_html
assert "heartbeat" in pattern_html

preview.state["mode"] = "morse"
morse_html = preview.blink_controls()
assert "/led/morse" in morse_html
assert 'id="morse-code"' in morse_html

print("web asset checks passed")
