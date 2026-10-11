#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

namespace {
// Classic ESP32 DAC-capable pins are GPIO25 (DAC1) and GPIO26 (DAC2).
// This project uses DAC1. Use a current-limiting resistor; do not connect an
// LED directly to the pin. For more current, drive a transistor/MOSFET.
constexpr uint8_t LED_DAC_PIN = 25;
constexpr uint8_t LED_DAC_ON = 255;
constexpr uint8_t LED_DAC_OFF = 0;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t MIN_DELAY_MS = 50;
constexpr uint32_t MAX_DELAY_MS = 2000;

AsyncWebServer server(80);

struct LedState {
  bool enabled = false;
  bool blinking = false;
  uint32_t delayMs = 500;
  bool output = false;
  uint32_t lastToggle = 0;
} state;

uint32_t clampDelay(long value) {
  return static_cast<uint32_t>(constrain(value, static_cast<long>(MIN_DELAY_MS),
                                         static_cast<long>(MAX_DELAY_MS)));
}

String statusFragment(const char* message, bool error = false) {
  String html = "<span class=\"";
  html += error ? "error" : "success";
  html += "\">";
  html += message;
  html += "</span>";
  return html;
}

String statusFragment(const String& message, bool error = false) {
  return statusFragment(message.c_str(), error);
}

String blinkControls() {
  if (!state.blinking) {
    return F("<div id=\"blink-controls\"></div>");
  }

  String html;
  html.reserve(700);
  html += F("<div id=\"blink-controls\"><label for=\"blink-delay\">"
            "Blink delay <output id=\"blink-delay-value\">");
  html += state.delayMs;
  html += F(" ms</output></label><input id=\"blink-delay\" name=\"delay\""
            " type=\"range\" min=\"50\" max=\"2000\" step=\"10\" value=\"");
  html += state.delayMs;
  html += F("\" hx-post=\"/led/blink-delay\""
            " hx-trigger=\"change\" hx-target=\"#command-status\""
            " hx-swap=\"innerHTML\""
            " hx-on:input=\"document.getElementById('blink-delay-value').value = this.value + ' ms'\">"
            "<small>Shorter delay means faster blinking."
            "</small></div>");
  return html;
}

void applyOutput() {
  // The DAC output is intentionally used directly rather than digitalWrite.
  // 0 V represents off and the maximum 8-bit DAC code represents on.
  dacWrite(LED_DAC_PIN, state.output ? LED_DAC_ON : LED_DAC_OFF);
}

void updateLed() {
  if (!state.enabled) {
    state.output = false;
    applyOutput();
    return;
  }

  if (!state.blinking) {
    state.output = true;
    applyOutput();
    return;
  }

  const uint32_t now = millis();
  if (now - state.lastToggle >= state.delayMs) {
    state.lastToggle = now;
    state.output = !state.output;
    applyOutput();
  }
}

void setupRoutes() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/led/power", HTTP_POST, [](AsyncWebServerRequest* request) {
    state.enabled = request->hasParam("enabled", true);
    state.lastToggle = millis();
    updateLed();
    request->send(200, "text/html", statusFragment(state.enabled ? "LED on" : "LED off"));
  });

  server.on("/led/mode", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("mode", true)) {
      request->send(400, "text/html", statusFragment("Missing mode", true));
      return;
    }

    const String mode = request->getParam("mode", true)->value();
    if (mode != "solid" && mode != "blink") {
      request->send(400, "text/html", statusFragment("Invalid mode", true));
      return;
    }

    state.blinking = mode == "blink";
    state.lastToggle = millis();
    updateLed();
    request->send(200, "text/html", blinkControls());
  });

  server.on("/led/blink-delay", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("delay", true)) {
      request->send(400, "text/html", statusFragment("Missing delay", true));
      return;
    }

    state.delayMs = clampDelay(request->getParam("delay", true)->value().toInt());
    request->send(200, "text/html", statusFragment(String("Blink delay set to ") + state.delayMs + " ms"));
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });
}
} // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  // DAC pins do not need pinMode().
  applyOutput();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-LED", "esp32-led");
  Serial.print("LED controller ready at http://");
  Serial.println(WiFi.softAPIP());

  setupRoutes();
  server.begin();
}

void loop() {
  updateLed();
  delay(1);
}
