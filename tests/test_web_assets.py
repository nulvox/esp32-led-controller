from pathlib import Path

ROOT = Path(__file__).parents[1]
index = (ROOT / "data/index.html").read_text()
css = (ROOT / "data/style.css").read_text()
htmx = ROOT / "data/htmx.min.js"

assert htmx.exists() and htmx.stat().st_size > 1000
for needle in ("/led/power", "/led/mode", "name=\"mode\"", "#blink-controls"):
    assert needle in index, needle
for needle in ("@media", "input[type=\"range\"]", ".htmx-request"):
    assert needle in css, needle
print("web asset checks passed")
