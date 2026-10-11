#!/usr/bin/env python3
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
import os

ROOT = Path(__file__).resolve().parents[1] / "data"
state = {"enabled": False, "mode": "solid", "delay": 500}

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        values = parse_qs(self.rfile.read(length).decode())

        if self.path == "/led/power":
            state["enabled"] = "enabled" in values
            body = f'<span class="success">LED {"on" if state["enabled"] else "off"}</span>'
        elif self.path == "/led/mode":
            state["mode"] = values.get("mode", ["solid"])[0]
            body = blink_controls()
        elif self.path == "/led/blink-delay":
            state["delay"] = max(50, min(2000, int(values.get("delay", [500])[0])))
            body = f'<span class="success">Blink delay set to {state["delay"]} ms</span>'
        else:
            self.send_error(404)
            return

        encoded = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def blink_controls():
    if state["mode"] != "blink":
        return '<div id="blink-controls"></div>'
    delay = state["delay"]
    return f'''<div id="blink-controls">
<label for="blink-delay">Blink delay <output id="blink-delay-value">{delay} ms</output></label>
<input id="blink-delay" name="delay" type="range" min="50" max="2000" step="10" value="{delay}"
       hx-post="/led/blink-delay" hx-trigger="change" hx-target="#command-status"
       hx-swap="innerHTML"
       hx-on:input="document.getElementById('blink-delay-value').value = this.value + ' ms'">
<small>Shorter delay means faster blinking.</small>
</div>'''


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8080"))
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"Preview: http://127.0.0.1:{port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
