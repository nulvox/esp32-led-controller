#!/usr/bin/env python3
"""Small dependency-free checks for the files shipped to the device."""
from html.parser import HTMLParser
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"


class PageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.controls = 0
        self.errors = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            if attrs["id"] in self.ids:
                self.errors.append(f"duplicate id: {attrs['id']}")
            self.ids.add(attrs["id"])
        if tag in {"input", "button", "select", "textarea"}:
            self.controls += 1
        for attr in attrs:
            if attr.startswith("hx-") and not attrs[attr]:
                self.errors.append(f"empty htmx attribute: {attr}")


def fail(message):
    raise SystemExit(f"lint error: {message}")


html_path = DATA / "index.html"
css_path = DATA / "style.css"
htmx_path = DATA / "htmx.min.js"

for path in (html_path, css_path, htmx_path):
    if not path.is_file():
        fail(f"missing web asset: {path.relative_to(ROOT)}")

html = html_path.read_text()
css = css_path.read_text()

if "cdn." in html or "unpkg.com" in html or "jsdelivr" in html:
    fail("web UI must not depend on a CDN")
if "<script src=\"/htmx.min.js\"" not in html:
    fail("htmx must be served locally")
if 'rel="stylesheet" href="/style.css"' not in html:
    fail("stylesheet must be served locally")
if "input[type=\"range\"]" not in css:
    fail("range control has no styling")
if "@media" not in css:
    fail("responsive media query is missing")

parser = PageParser()
parser.feed(html)
if parser.errors:
    fail("; ".join(parser.errors))
if parser.controls < 3:
    fail("expected at least three interactive controls")

for endpoint in ("/led/power", "/led/mode", "/led/blink-delay"):
    if endpoint not in html and endpoint not in (ROOT / "src/main.cpp").read_text():
        fail(f"endpoint is not wired: {endpoint}")

print("content lint passed")
