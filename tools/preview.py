#!/usr/bin/env python3
from html import escape
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
import os

ROOT = Path(__file__).resolve().parents[1] / "data"
MAX_MORSE_TEXT_LEN = 64

PATTERNS = [
    ("double", "Double blink", [1, 1, 1, 4]),
    ("triple", "Triple blink", [1, 1, 1, 1, 1, 4]),
    ("heartbeat", "Heartbeat", [1, 1, 2, 5]),
    ("beacon", "Beacon", [4, 1, 1, 4]),
    ("strobe", "Strobe burst", [1, 1, 1, 1, 1, 1, 1, 5]),
    ("sos", "SOS (... --- ...)", [1, 1, 1, 1, 1, 3, 3, 1, 3, 1, 3, 3, 1, 1, 1, 1, 1, 7]),
]
PATTERN_LABELS = {pid: label for pid, label, _ in PATTERNS}

MORSE_TABLE = {
    "A": ".-",      "B": "-...",    "C": "-.-.",    "D": "-..",
    "E": ".",       "F": "..-.",    "G": "--.",     "H": "....",
    "I": "..",      "J": ".---",    "K": "-.-",     "L": ".-..",
    "M": "--",      "N": "-.",      "O": "---",     "P": ".--.",
    "Q": "--.-",    "R": ".-.",     "S": "...",     "T": "-",
    "U": "..-",     "V": "...-",    "W": ".--",     "X": "-..-",
    "Y": "-.--",    "Z": "--..",    "0": "-----",   "1": ".----",
    "2": "..---",   "3": "...--",   "4": "....-",   "5": ".....",
    "6": "-....",   "7": "--...",   "8": "---..",   "9": "----.",
    ".": ".-.-.-",  ",": "--..--",  "?": "..--..",  "!": "-.-.--",
    "/": "-..-.",   "-": "-....-",  "@": ".--.-.",  ":": "---...",
    "'": ".----.",  "(": "-.--.",   ")": "-.--.-",  "=": "-...-",
    "+": ".-.-.",
}

state = {
    "enabled": False,
    "mode": "solid",
    "delay": 200,
    "pattern": "double",
    "morse_text": "SOS",
    "morse_visual": "... --- ...",
}


def encode_morse(raw_text: str):
    steps = []
    clean_chars = []
    visual_parts = []
    pending_gap = 0
    has_char = False

    for ch in raw_text:
        if ch.isspace():
            if has_char:
                pending_gap = 7
            continue
        upper = ch.upper()
        code = MORSE_TABLE.get(upper)
        if not code:
            continue
        if has_char:
            steps.append(pending_gap)
            if pending_gap == 7:
                clean_chars.append(" ")
                visual_parts.append(" / ")
            else:
                visual_parts.append(" ")
        clean_chars.append(upper)
        visual_parts.append(code)
        for idx, sym in enumerate(code):
            if idx > 0:
                steps.append(1)
            steps.append(3 if sym == "-" else 1)
        has_char = True
        pending_gap = 3

    if not has_char:
        return None
    steps.append(7)
    return "".join(clean_chars), "".join(visual_parts), steps


def status_fragment(message: str, error: bool = False) -> str:
    cls = "error" if error else "success"
    return f'<span class="{cls}">{message}</span>'


def delay_slider(label: str, hint: str) -> str:
    delay = state["delay"]
    return f'''<label for="blink-delay">{label} <output id="blink-delay-value">{delay} ms</output></label>
<input id="blink-delay" name="delay" type="range" min="50" max="2000" step="10" value="{delay}"
       hx-post="/led/blink-delay" hx-trigger="change" hx-target="#command-status"
       hx-swap="innerHTML"
       hx-on:input="document.getElementById('blink-delay-value').value = this.value + ' ms'">
<small>{hint}</small>'''


def blink_controls() -> str:
    mode = state["mode"]
    if mode == "solid":
        return '<div id="blink-controls"></div>'
    if mode == "blink":
        inner = delay_slider("Blink delay", "Shorter delay means faster blinking.")
        return f'<div id="blink-controls">{inner}</div>'
    if mode == "pattern":
        options = []
        for pid, label, _ in PATTERNS:
            selected = " selected" if pid == state["pattern"] else ""
            options.append(f'<option value="{pid}"{selected}>{label}</option>')
        options_html = "".join(options)
        slider = delay_slider(
            "Step unit delay",
            "Base duration for each pulse and pause in the pattern.",
        )
        return f'''<div id="blink-controls">
<div class="control-group"><label for="blink-pattern">Discrete pattern</label>
<select id="blink-pattern" name="pattern" hx-post="/led/pattern" hx-trigger="change"
        hx-target="#command-status" hx-swap="innerHTML">{options_html}</select></div>
{slider}
</div>'''
    if mode == "morse":
        text = escape(state["morse_text"], quote=True)
        visual = escape(state["morse_visual"], quote=True)
        slider = delay_slider(
            "Dot unit delay",
            "1 dot = 1x, dash = 3x, letter gap = 3x, word gap = 7x.",
        )
        return f'''<div id="blink-controls">
<form class="morse-form" hx-post="/led/morse" hx-target="#command-status" hx-swap="innerHTML">
<label for="morse-message">Morse message</label>
<div class="input-row">
<input id="morse-message" name="message" type="text" maxlength="64" autocomplete="off"
       placeholder="Type message (A-Z, 0-9)" value="{text}" required>
<button type="submit">Send</button>
</div>
<small>Morse preview: <code id="morse-code">{visual}</code></small>
</form>
{slider}
</div>'''
    return '<div id="blink-controls"></div>'


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def _send_html(self, status: int, body: str):
        encoded = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        values = parse_qs(self.rfile.read(length).decode(), keep_blank_values=True)

        if self.path == "/led/power":
            state["enabled"] = "enabled" in values
            self._send_html(200, status_fragment(f'LED {"on" if state["enabled"] else "off"}'))
        elif self.path == "/led/mode":
            if "mode" not in values:
                self._send_html(400, status_fragment("Missing mode", error=True))
                return
            mode = values["mode"][0]
            if mode not in {"solid", "blink", "pattern", "morse"}:
                self._send_html(400, status_fragment("Invalid mode", error=True))
                return
            state["mode"] = mode
            self._send_html(200, blink_controls())
        elif self.path == "/led/blink-delay":
            if "delay" not in values:
                self._send_html(400, status_fragment("Missing delay", error=True))
                return
            try:
                delay_val = int(values["delay"][0])
            except ValueError:
                delay_val = 200
            state["delay"] = max(50, min(2000, delay_val))
            self._send_html(200, status_fragment(f'Blink delay set to {state["delay"]} ms'))
        elif self.path == "/led/pattern":
            if "pattern" not in values:
                self._send_html(400, status_fragment("Missing pattern", error=True))
                return
            pattern = values["pattern"][0]
            if pattern not in PATTERN_LABELS:
                self._send_html(400, status_fragment("Invalid pattern", error=True))
                return
            state["pattern"] = pattern
            state["mode"] = "pattern"
            self._send_html(200, status_fragment(f'Pattern set to {PATTERN_LABELS[pattern]}'))
        elif self.path == "/led/morse":
            if "message" not in values:
                self._send_html(400, status_fragment("Missing message", error=True))
                return
            raw = values["message"][0]
            if len(raw) > MAX_MORSE_TEXT_LEN:
                self._send_html(
                    400,
                    status_fragment("Message too long (max 64 characters)", error=True),
                )
                return
            encoded = encode_morse(raw)
            if encoded is None:
                self._send_html(
                    400,
                    status_fragment("Enter a message with letters or numbers", error=True),
                )
                return
            clean_text, visual, _ = encoded
            state["morse_text"] = clean_text
            state["morse_visual"] = visual
            state["mode"] = "morse"
            esc_text = escape(clean_text, quote=True)
            esc_visual = escape(visual, quote=True)
            body = (
                status_fragment(f'Morse set to "{esc_text}" ({esc_visual})')
                + f'<code id="morse-code" hx-swap-oob="true">{esc_visual}</code>'
            )
            self._send_html(200, body)
        else:
            self.send_error(404)


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8080"))
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"Preview: http://127.0.0.1:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
