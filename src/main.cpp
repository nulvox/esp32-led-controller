#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ctype.h>
#include <string.h>

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
constexpr size_t MAX_MORSE_TEXT_LEN = 64;
constexpr size_t MAX_STEPS = 768;

AsyncWebServer server(80);

enum class LedMode : uint8_t {
  Solid,
  Blink,
  Pattern,
  Morse,
};

struct PatternSpec {
  const char* id;
  const char* label;
  const uint8_t* steps;
  size_t stepCount;
};

// Even indices are ON durations, odd indices are OFF durations (in delayMs units).
constexpr uint8_t STEPS_DOUBLE[] = {1, 1, 1, 4};
constexpr uint8_t STEPS_TRIPLE[] = {1, 1, 1, 1, 1, 4};
constexpr uint8_t STEPS_HEARTBEAT[] = {1, 1, 2, 5};
constexpr uint8_t STEPS_BEACON[] = {4, 1, 1, 4};
constexpr uint8_t STEPS_STROBE[] = {1, 1, 1, 1, 1, 1, 1, 5};
constexpr uint8_t STEPS_SOS[] = {1, 1, 1, 1, 1, 3, 3, 1, 3, 1, 3, 3, 1, 1, 1, 1, 1, 7};

constexpr PatternSpec PATTERNS[] = {
  {"double", "Double blink", STEPS_DOUBLE, sizeof(STEPS_DOUBLE)},
  {"triple", "Triple blink", STEPS_TRIPLE, sizeof(STEPS_TRIPLE)},
  {"heartbeat", "Heartbeat", STEPS_HEARTBEAT, sizeof(STEPS_HEARTBEAT)},
  {"beacon", "Beacon", STEPS_BEACON, sizeof(STEPS_BEACON)},
  {"strobe", "Strobe burst", STEPS_STROBE, sizeof(STEPS_STROBE)},
  {"sos", "SOS (... --- ...)", STEPS_SOS, sizeof(STEPS_SOS)},
};
constexpr size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

struct MorseEntry {
  char ch;
  const char* code;
};

constexpr MorseEntry MORSE_TABLE[] = {
  {'A', ".-"},      {'B', "-..."},    {'C', "-.-."},   {'D', "-.."},
  {'E', "."},       {'F', "..-."},    {'G', "--."},    {'H', "...."},
  {'I', ".."},      {'J', ".---"},    {'K', "-.-"},    {'L', ".-.."},
  {'M', "--"},      {'N', "-."},      {'O', "---"},    {'P', ".--."},
  {'Q', "--.-"},    {'R', ".-."},     {'S', "..."},    {'T', "-"},
  {'U', "..-"},     {'V', "...-"},    {'W', ".--"},    {'X', "-..-"},
  {'Y', "-.--"},    {'Z', "--.."},    {'0', "-----"},  {'1', ".----"},
  {'2', "..---"},   {'3', "...--"},   {'4', "....-"},  {'5', "....."},
  {'6', "-...."},   {'7', "--..."},   {'8', "---.."},  {'9', "----."},
  {'.', ".-.-.-"},  {',', "--..--"},  {'?', "..--.."}, {'!', "-.-.--"},
  {'/', "-..-."},   {'-', "-....-"},  {'@', ".--.-."}, {':', "---..."},
  {'\'', ".----."}, {'(', "-.--."},   {')', "-.--.-"}, {'=', "-...-"},
  {'+', ".-.-."},
};

struct LedState {
  bool enabled = false;
  LedMode mode = LedMode::Solid;
  uint32_t delayMs = 200;
  size_t patternIndex = 0;
  String morseText = "SOS";
  String morseVisual = "... --- ...";
  uint8_t steps[MAX_STEPS] = {1, 1};
  size_t stepCount = 2;
  size_t stepIndex = 0;
  bool output = false;
  uint32_t lastToggle = 0;
} state;

uint32_t clampDelay(long value) {
  return static_cast<uint32_t>(constrain(value, static_cast<long>(MIN_DELAY_MS),
                                         static_cast<long>(MAX_DELAY_MS)));
}

String escapeHtml(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

const char* lookupMorse(char c) {
  const char upper = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  for (const auto& entry : MORSE_TABLE) {
    if (entry.ch == upper) {
      return entry.code;
    }
  }
  return nullptr;
}

bool encodeMorse(const String& rawText, uint8_t* outSteps, size_t maxSteps,
                 size_t& outStepCount, String& outCleanText, String& outVisual) {
  outStepCount = 0;
  outCleanText = "";
  outVisual = "";

  uint8_t pendingGap = 0;
  bool hasChar = false;

  for (size_t i = 0; i < rawText.length(); ++i) {
    const char rawChar = rawText[i];
    if (isspace(static_cast<unsigned char>(rawChar))) {
      if (hasChar) {
        pendingGap = 7;
      }
      continue;
    }

    const char* code = lookupMorse(rawChar);
    if (!code) {
      continue;
    }

    const size_t codeLen = strlen(code);
    // Each symbol needs an ON step and an OFF step (plus a possible prior gap).
    if (outStepCount + (codeLen * 2) + 1 > maxSteps) {
      break;
    }

    if (hasChar) {
      outSteps[outStepCount++] = pendingGap;
      if (pendingGap == 7) {
        outCleanText += ' ';
        outVisual += F(" / ");
      } else {
        outVisual += ' ';
      }
    }

    outCleanText += static_cast<char>(toupper(static_cast<unsigned char>(rawChar)));
    outVisual += code;

    for (size_t s = 0; s < codeLen; ++s) {
      if (s > 0) {
        outSteps[outStepCount++] = 1; // intra-character gap (1 unit OFF)
      }
      outSteps[outStepCount++] = (code[s] == '-') ? 3 : 1; // ON duration
    }

    hasChar = true;
    pendingGap = 3; // default inter-character gap (3 units OFF)
  }

  if (!hasChar) {
    return false;
  }

  // Final word gap before repeating the message from the start.
  outSteps[outStepCount++] = 7;
  return true;
}

int findPatternIndex(const String& id) {
  for (size_t i = 0; i < PATTERN_COUNT; ++i) {
    if (id == PATTERNS[i].id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void applyOutput() {
  // The DAC output is intentionally used directly rather than digitalWrite.
  // 0 V represents off and the maximum 8-bit DAC code represents on.
  dacWrite(LED_DAC_PIN, state.output ? LED_DAC_ON : LED_DAC_OFF);
}

void rebuildSteps() {
  if (state.mode == LedMode::Blink) {
    state.steps[0] = 1;
    state.steps[1] = 1;
    state.stepCount = 2;
  } else if (state.mode == LedMode::Pattern) {
    const PatternSpec& spec = PATTERNS[state.patternIndex];
    memcpy(state.steps, spec.steps, spec.stepCount);
    state.stepCount = spec.stepCount;
  } else if (state.mode == LedMode::Morse) {
    String clean;
    String visual;
    size_t count = 0;
    if (encodeMorse(state.morseText, state.steps, MAX_STEPS, count, clean, visual)) {
      state.stepCount = count;
      state.morseText = clean;
      state.morseVisual = visual;
    }
  }
  state.stepIndex = 0;
  state.lastToggle = millis();
  state.output = state.enabled && (state.mode == LedMode::Solid || state.stepCount > 0);
  applyOutput();
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

void appendDelaySlider(String& html, const __FlashStringHelper* label,
                       const __FlashStringHelper* hint) {
  html += F("<label for=\"blink-delay\">");
  html += label;
  html += F(" <output id=\"blink-delay-value\">");
  html += state.delayMs;
  html += F(" ms</output></label><input id=\"blink-delay\" name=\"delay\""
            " type=\"range\" min=\"50\" max=\"2000\" step=\"10\" value=\"");
  html += state.delayMs;
  html += F("\" hx-post=\"/led/blink-delay\""
            " hx-trigger=\"change\" hx-target=\"#command-status\""
            " hx-swap=\"innerHTML\""
            " hx-on:input=\"document.getElementById('blink-delay-value').value = this.value + ' ms'\">"
            "<small>");
  html += hint;
  html += F("</small>");
}

String blinkControls() {
  if (state.mode == LedMode::Solid) {
    return F("<div id=\"blink-controls\"></div>");
  }

  String html;
  html.reserve(1200);
  html += F("<div id=\"blink-controls\">");

  if (state.mode == LedMode::Blink) {
    appendDelaySlider(html, F("Blink delay"), F("Shorter delay means faster blinking."));
  } else if (state.mode == LedMode::Pattern) {
    html += F("<div class=\"control-group\"><label for=\"blink-pattern\">Discrete pattern</label>"
              "<select id=\"blink-pattern\" name=\"pattern\""
              " hx-post=\"/led/pattern\" hx-trigger=\"change\""
              " hx-target=\"#command-status\" hx-swap=\"innerHTML\">");
    for (size_t i = 0; i < PATTERN_COUNT; ++i) {
      html += F("<option value=\"");
      html += PATTERNS[i].id;
      html += '"';
      if (i == state.patternIndex) {
        html += F(" selected");
      }
      html += '>';
      html += PATTERNS[i].label;
      html += F("</option>");
    }
    html += F("</select></div>");
    appendDelaySlider(html, F("Step unit delay"),
                      F("Base duration for each pulse and pause in the pattern."));
  } else if (state.mode == LedMode::Morse) {
    html += F("<form class=\"morse-form\" hx-post=\"/led/morse\""
              " hx-target=\"#command-status\" hx-swap=\"innerHTML\">"
              "<label for=\"morse-message\">Morse message</label>"
              "<div class=\"input-row\">"
              "<input id=\"morse-message\" name=\"message\" type=\"text\""
              " maxlength=\"64\" autocomplete=\"off\""
              " placeholder=\"Type message (A-Z, 0-9)\" value=\"");
    html += escapeHtml(state.morseText);
    html += F("\" required>"
              "<button type=\"submit\">Send</button>"
              "</div>"
              "<small>Morse preview: <code id=\"morse-code\">");
    html += escapeHtml(state.morseVisual);
    html += F("</code></small></form>");
    appendDelaySlider(html, F("Dot unit delay"),
                      F("1 dot = 1x, dash = 3x, letter gap = 3x, word gap = 7x."));
  }

  html += F("</div>");
  return html;
}

void updateLed() {
  if (!state.enabled) {
    if (state.output) {
      state.output = false;
      applyOutput();
    }
    return;
  }

  if (state.mode == LedMode::Solid) {
    if (!state.output) {
      state.output = true;
      applyOutput();
    }
    return;
  }

  if (state.stepCount == 0) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t stepDurationMs =
      static_cast<uint32_t>(state.steps[state.stepIndex]) * state.delayMs;
  if (now - state.lastToggle >= stepDurationMs) {
    state.lastToggle = now;
    state.stepIndex = (state.stepIndex + 1) % state.stepCount;
    state.output = (state.stepIndex % 2 == 0);
    applyOutput();
  }
}

void setupRoutes() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/led/power", HTTP_POST, [](AsyncWebServerRequest* request) {
    state.enabled = request->hasParam("enabled", true);
    rebuildSteps();
    request->send(200, "text/html", statusFragment(state.enabled ? "LED on" : "LED off"));
  });

  server.on("/led/mode", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("mode", true)) {
      request->send(400, "text/html", statusFragment("Missing mode", true));
      return;
    }

    const String mode = request->getParam("mode", true)->value();
    if (mode == "solid") {
      state.mode = LedMode::Solid;
    } else if (mode == "blink") {
      state.mode = LedMode::Blink;
    } else if (mode == "pattern") {
      state.mode = LedMode::Pattern;
    } else if (mode == "morse") {
      state.mode = LedMode::Morse;
    } else {
      request->send(400, "text/html", statusFragment("Invalid mode", true));
      return;
    }

    rebuildSteps();
    request->send(200, "text/html", blinkControls());
  });

  server.on("/led/blink-delay", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("delay", true)) {
      request->send(400, "text/html", statusFragment("Missing delay", true));
      return;
    }

    state.delayMs = clampDelay(request->getParam("delay", true)->value().toInt());
    request->send(200, "text/html",
                  statusFragment(String("Blink delay set to ") + state.delayMs + " ms"));
  });

  server.on("/led/pattern", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("pattern", true)) {
      request->send(400, "text/html", statusFragment("Missing pattern", true));
      return;
    }

    const String patternId = request->getParam("pattern", true)->value();
    const int idx = findPatternIndex(patternId);
    if (idx < 0) {
      request->send(400, "text/html", statusFragment("Invalid pattern", true));
      return;
    }

    state.patternIndex = static_cast<size_t>(idx);
    state.mode = LedMode::Pattern;
    rebuildSteps();
    request->send(200, "text/html",
                  statusFragment(String("Pattern set to ") + PATTERNS[state.patternIndex].label));
  });

  server.on("/led/morse", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("message", true)) {
      request->send(400, "text/html", statusFragment("Missing message", true));
      return;
    }

    const String raw = request->getParam("message", true)->value();
    if (raw.length() > MAX_MORSE_TEXT_LEN) {
      request->send(400, "text/html",
                    statusFragment("Message too long (max 64 characters)", true));
      return;
    }

    uint8_t candidateSteps[MAX_STEPS];
    size_t candidateCount = 0;
    String cleanText;
    String visualMorse;
    if (!encodeMorse(raw, candidateSteps, MAX_STEPS, candidateCount, cleanText, visualMorse)) {
      request->send(400, "text/html",
                    statusFragment("Enter a message with letters or numbers", true));
      return;
    }

    memcpy(state.steps, candidateSteps, candidateCount);
    state.stepCount = candidateCount;
    state.morseText = cleanText;
    state.morseVisual = visualMorse;
    state.mode = LedMode::Morse;
    state.stepIndex = 0;
    state.lastToggle = millis();
    state.output = state.enabled;
    applyOutput();

    String response = statusFragment(String("Morse set to \"") + escapeHtml(cleanText) +
                                     "\" (" + escapeHtml(visualMorse) + ")");
    response += F("<code id=\"morse-code\" hx-swap-oob=\"true\">");
    response += escapeHtml(visualMorse);
    response += F("</code>");
    request->send(200, "text/html", response);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });
}
} // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  // DAC pins do not need pinMode().
  rebuildSteps();

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
