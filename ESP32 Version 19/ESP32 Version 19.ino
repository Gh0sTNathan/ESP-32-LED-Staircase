/*
 * ESP32 NeoPixel Motion Controller
 * 290 WS2812B LEDs with dual PIR motion detection
 * Web interface with CORS support and real-time console
 * Multiple animation modes and effects
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

// Hardware Configuration
#define LED_PIN 13
#define NUM_LEDS 290
#define PIR_BOTTOM 27
#define PIR_TOP 14
#define NUM_SEGMENTS 11
#define NUM_CHUNKS NUM_SEGMENTS

#define FASTLED_RMT_MAX_CHANNELS 1
#define FASTLED_RMT_BUILTIN_DRIVER
#include <FastLED.h>


// Wi-Fi Configuration - UPDATE THESE!
const char* ssid = "******";
const char* password = "******";
const char* hostname = "ESP32-LEDController";

// LED Array and Web Server
CRGB leds[NUM_LEDS];
WebServer server(80);

// Web Console Buffer
String consoleBuffer = "";
const int maxConsoleLines = 50;

// Segment structure for motion animations
struct SegmentInfo {
  int start;
  int end;
  int middle;
};

// Stair segments with actual LED positions
SegmentInfo segments[11] = {
  {0, 25, 12},     // Segment 0: middle at LED 12
  {26, 50, 38},    // Segment 1: middle at LED 38
  {51, 76, 63},    // Segment 2: middle at LED 63
  {77, 102, 89},   // Segment 3: middle at LED 89
  {103, 128, 115}, // Segment 4: middle at LED 115
  {129, 153, 141}, // Segment 5: middle at LED 141
  {154, 181, 167}, // Segment 6: middle at LED 167
  {182, 207, 194}, // Segment 7: middle at LED 194
  {208, 234, 221}, // Segment 8: middle at LED 221
  {235, 260, 247}, // Segment 9: middle at LED 247
  {261, 289, 275}  // Segment 10: middle at LED 275
};

// Motion sequence states
enum MotionState {
  STATE_IDLE,
  STATE_MOTION_TURN_ON,
  STATE_MOTION_DELAY,
  STATE_MOTION_TURN_OFF,
  STATE_MOTION_FINAL_DELAY
};

bool motionSequenceRunning = false;
MotionState currentState = STATE_IDLE;
bool motionFromBottom = false;
int motionTurnOnStep = 0;
int motionTurnOffStep = 0;
unsigned long motionSequenceStart = 0;
unsigned long lastMotionStepUpdate = 0;


// System State Variables - UPDATED DEFAULTS
bool systemOn = true;        // Changed to default ON
bool motionEnabled = true;
bool autoOffEnabled = true;
bool motionOnlyMode = false;
uint8_t brightness = 255;    // Changed to 100% (255)
uint16_t kelvinTemp = 3000;
CRGB customColor = CRGB::White;
bool useKelvin = true;
bool forceAnimationUpdate = false;

// Animation States
enum AnimationMode {
  SOLID,
  FLICKER,
  PULSE,
  RAINBOW,
  CHASE,
  DNA_SPIRAL
};

AnimationMode currentMode = CHASE;
AnimationMode backgroundMode = CHASE;

// Motion Detection Variables
bool motionDetected = false;
bool bottomMotion = false;
bool topMotion = false;
unsigned long motionStartTime = 0;
unsigned long lastMotionTime = 0;
const unsigned long motionDelay = 200;
const unsigned long autoOffDelay = 10000; // 10 seconds

// Animation Variables
unsigned long lastAnimationUpdate = 0;
uint16_t animationStep = 0;
uint8_t ledFlickerPhase[NUM_LEDS];  // Renamed to avoid conflict
float pulsePhase = 0;
uint8_t rainbowHue = 0;
int chasePosition = 0;
float dnaPhase = 0;
float chunkFlickerPhase[NUM_CHUNKS];  // Renamed to avoid conflict

// Flickering configuration
struct FlickerConfig {
  float FLICKER_AMPLITUDE = 0.64;
  float FLICKER_BASE = 0.7;
  float TIME_STEP = 0.1;
} flickering;

// Timing Configuration
struct TimingConfig {
  uint16_t flickerSpeed = 50;
  uint16_t pulseSpeed = 30;
  uint16_t rainbowSpeed = 20;
  uint16_t chaseSpeed = 60;
  uint16_t dnaSpeed = 40;
  uint16_t motionFadeSpeed = 20;
  uint16_t delayVal = 30;
  uint16_t stepTime = 150;
  uint16_t waitTime = 6000;
} timing;

// Segment Animation Variables
int currentSegment = NUM_SEGMENTS / 2; // Start from middle
bool expandingUp = true;
bool expandingDown = true;
bool segmentAnimation = false;
unsigned long segmentTimer = 0;

// Web Console Functions
void webPrintln(String message) {
  String timestamp = "[" + String(millis()) + "] ";
  consoleBuffer += timestamp + message + "\n";
  
  // Limit buffer size
  int lineCount = 0;
  for (int i = 0; i < consoleBuffer.length(); i++) {
    if (consoleBuffer.charAt(i) == '\n') lineCount++;
  }
  
  while (lineCount > maxConsoleLines) {
    int firstNewline = consoleBuffer.indexOf('\n');
    if (firstNewline != -1) {
      consoleBuffer = consoleBuffer.substring(firstNewline + 1);
      lineCount--;
    } else {
      break;
    }
  }
  
  Serial.println(message); // Still output to serial for debugging
}

void webPrint(String message) {
  consoleBuffer += message;
  Serial.print(message);
}

void setup() {
  Serial.begin(115200);
  
  webPrintln("ESP32 LED Controller starting...");
  
  // Initialize LEDs
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();
  webPrintln("LEDs initialized");
  
  // Initialize PIR sensors
  pinMode(PIR_BOTTOM, INPUT);
  pinMode(PIR_TOP, INPUT);
  webPrintln("PIR sensors initialized");

  // Initialize flicker phases
  for(int i = 0; i < NUM_LEDS; i++) {
    ledFlickerPhase[i] = random(255);
  }
  
  // Initialize chunk flicker phases
  for(int i = 0; i < NUM_CHUNKS; i++) {
    chunkFlickerPhase[i] = random(100) / 100.0 * 2 * PI;
  }
  webPrintln("Animation phases initialized");

  // Configure static IP
  IPAddress local_IP(192, 168, 0, 69);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);

if (!WiFi.config(local_IP, gateway, subnet)) 
  {
    Serial.println("STA Failed to configure");
  }
  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();

  
  
  webPrint("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    webPrint(".");
  }
  webPrintln("");
  webPrintln("Connected! IP: " + WiFi.localIP().toString());
  
// Setup OTA
ArduinoOTA.setHostname(hostname);

// When OTA starts
ArduinoOTA.onStart([]() {
  String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
  webPrintln("Start updating " + type);
  FastLED.clear();
  FastLED.show();
});

// When OTA ends
ArduinoOTA.onEnd([]() {
  webPrintln("OTA Update End");
  FastLED.clear();
  FastLED.show();
});

// When OTA is in progress
ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  // Create a blue pulse/chase effect based on progress
  int progressLeds = map(progress, 0, total, 0, NUM_LEDS);
  FastLED.clear();

  // Blue chase pattern (moves along as progress increases)
  for (int i = 0; i < progressLeds; i++) {
    leds[i] = CRGB::Blue;
  }

  // Add a trailing fade effect
  int tailLength = 10; // adjust for smoother fade
  for (int i = 0; i < tailLength; i++) {
    int idx = progressLeds - i;
    if (idx >= 0 && idx < NUM_LEDS) {
      leds[idx].fadeToBlackBy(i * 25); // gradually fade tail
    }
  }

  FastLED.show();
});

// When OTA fails
ArduinoOTA.onError([](ota_error_t error) {
  webPrintln("OTA Error[" + String(error) + "]");

  // Red chase animation (runs for a few seconds)
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < NUM_LEDS; i++) {
      FastLED.clear();
      leds[i] = CRGB::Red;
      FastLED.show();
      delay(30); // adjust for chase speed
    }
  }

  // Leave all LEDs red after animation
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();
});

ArduinoOTA.begin();
webPrintln("OTA initialized");

  // Setup web server
  setupWebServer();
  server.begin();
  webPrintln("Web server started");
  
  // Startup animation
  startupAnimation();
  
  webPrintln("System initialized - Power: ON, Brightness: 100%");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  
  // Check motion sensors
  checkMotion();
  
  // Handle motion logic
  handleMotionLogic();
  
  // Handle motion sequence states
  handleMotionSequence();
  
  // Update animations
  updateAnimations();
  
  // Update LEDs
  FastLED.setBrightness(brightness);
  FastLED.show();
  
  //delay(10);
}

void handleMotionSequence() {
  switch (currentState) {
    case STATE_MOTION_TURN_ON:
      processMotionTurnOn();
      break;
    case STATE_MOTION_DELAY:
      processMotionDelay();
      break;
    case STATE_MOTION_TURN_OFF:
      processMotionTurnOff();
      break;
    case STATE_MOTION_FINAL_DELAY:
      if (millis() - motionSequenceStart >= 1000) {
        currentState = STATE_IDLE;
        motionSequenceRunning = false;
        webPrintln("Motion sequence complete - returning to idle");
        if (!motionOnlyMode) {
          systemOn = true;
          currentMode = backgroundMode;
          webPrintln("Switched to background mode: " + String(backgroundMode));
        }
      }
      break;
    case STATE_IDLE:
      

    default:
      break;
  }
}

void setupWebServer() {
  // Enable CORS for all routes
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(200);
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
  
  // Console endpoint
  server.on("/api/console", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", consoleBuffer);
  });
  
  // Status endpoint
  server.on("/api/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    DynamicJsonDocument doc(1024);
    doc["on"] = systemOn;
    doc["motionEnabled"] = motionEnabled;
    doc["autoOffEnabled"] = autoOffEnabled;
    doc["motionOnlyMode"] = motionOnlyMode;
    doc["brightness"] = brightness;
    doc["kelvinTemp"] = kelvinTemp;
    doc["useKelvin"] = useKelvin;
    doc["customColor"]["r"] = customColor.r;
    doc["customColor"]["g"] = customColor.g;
    doc["customColor"]["b"] = customColor.b;
    doc["animationMode"] = currentMode;
    doc["backgroundMode"] = backgroundMode;
    doc["motionDetected"] = motionDetected;
    doc["currentState"] = currentState;
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"] = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // Control endpoints
  server.on("/api/power", HTTP_POST, handlePowerControl);
  server.on("/api/motion", HTTP_POST, handleMotionControl);
  server.on("/api/brightness", HTTP_POST, handleBrightnessControl);
  server.on("/api/color", HTTP_POST, handleColorControl);
  server.on("/api/animation", HTTP_POST, handleAnimationControl);
  server.on("/api/timing", HTTP_POST, handleTimingControl);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  
  // Web interface
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", getWebInterface());
  });
}

void handlePowerControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  if (server.hasArg("on")) {
    bool newState = server.arg("on") == "true";
    systemOn = newState;
    webPrintln("System power " + String(newState ? "ON" : "OFF"));
    if (!systemOn) {
      FastLED.clear();
      segmentAnimation = false;
      webPrintln("LEDs cleared");
    }
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleMotionControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  bool updated = false;
  if (server.hasArg("enabled")) {
    motionEnabled = server.arg("enabled") == "true";
    webPrintln("Motion detection " + String(motionEnabled ? "ENABLED" : "DISABLED"));
    updated = true;
  }
  if (server.hasArg("autoOff")) {
    autoOffEnabled = server.arg("autoOff") == "true";
    webPrintln("Auto-off " + String(autoOffEnabled ? "ENABLED" : "DISABLED"));
    updated = true;
  }
  if (server.hasArg("motionOnly")) {
    motionOnlyMode = server.arg("motionOnly") == "true";
    webPrintln("Motion-only mode " + String(motionOnlyMode ? "ENABLED" : "DISABLED"));
    updated = true;
  }
  
  if (updated) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleBrightnessControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  if (server.hasArg("value")) {
    brightness = constrain(server.arg("value").toInt(), 0, 255);
    webPrintln("Brightness set to " + String(brightness) + " (" + String(map(brightness, 0, 255, 0, 100)) + "%)");
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleColorControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  if (server.hasArg("kelvin")) {
    kelvinTemp = constrain(server.arg("kelvin").toInt(), 1000, 10000);
    useKelvin = true;
    webPrintln("Color temperature set to " + String(kelvinTemp) + "K");
    server.send(200, "application/json", "{\"success\":true}");
  } else if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    customColor = CRGB(server.arg("r").toInt(), server.arg("g").toInt(), server.arg("b").toInt());
    useKelvin = false;
    webPrintln("Custom color set to RGB(" + String(customColor.r) + "," + String(customColor.g) + "," + String(customColor.b) + ")");
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleAnimationControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  String modes[] = {"Solid", "Flicker", "Pulse", "Rainbow", "Chase", "DNA Spiral"};
  
  if (server.hasArg("mode")) {
    int mode = server.arg("mode").toInt();
    if (mode >= 0 && mode <= 5) {
      currentMode = (AnimationMode)mode;
      webPrintln("Animation mode set to " + modes[mode]);
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
    }
  } else if (server.hasArg("background")) {
    int mode = server.arg("background").toInt();
    if (mode >= 0 && mode <= 5) {
      backgroundMode = (AnimationMode)mode;
      webPrintln("Background mode set to " + modes[mode]);
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleTimingControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  bool updated = false;
  if (server.hasArg("flicker")) {
    timing.flickerSpeed = constrain(server.arg("flicker").toInt(), 15, 50);
    webPrintln("Flicker speed set to " + String(timing.flickerSpeed));
    updated = true;
  }
  if (server.hasArg("pulse")) {
    timing.pulseSpeed = constrain(server.arg("pulse").toInt(), 10, 200);
    webPrintln("Pulse speed set to " + String(timing.pulseSpeed));
    updated = true;
  }
  
  if (server.hasArg("rainbow")) {
    timing.rainbowSpeed = constrain(server.arg("rainbow").toInt(), 10, 200);
    webPrintln("Rainbow speed set to " + String(timing.rainbowSpeed));
    updated = true;
  }
  if (server.hasArg("chase")) {
    timing.chaseSpeed = constrain(server.arg("chase").toInt(), 50, 500);
    webPrintln("Chase speed set to " + String(timing.chaseSpeed));
    updated = true;
  }
  if (server.hasArg("dna")) {
    timing.dnaSpeed = constrain(server.arg("dna").toInt(), 10, 200);
    webPrintln("DNA speed set to " + String(timing.dnaSpeed));
    updated = true;
  }
  
  if (updated) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameter\"}");
  }
}

void handleReboot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
  webPrintln("System reboot requested");
  delay(1000);
  ESP.restart();
}

void checkMotion() {
  bool currentBottomMotion = digitalRead(PIR_BOTTOM);
  bool currentTopMotion = digitalRead(PIR_TOP);
  
  if (!motionEnabled) return;
  
  // Debug output for PIR sensor readings
  static unsigned long lastDebugTime = 0;
  static bool lastBottomState = false;
  static bool lastTopState = false;
  
  // Only log when state changes
  if (currentBottomMotion != lastBottomState || currentTopMotion != lastTopState) {
    webPrintln("PIR State Change - Bottom: " + String(currentBottomMotion) + ", Top: " + String(currentTopMotion));
    lastBottomState = currentBottomMotion;
    lastTopState = currentTopMotion;
  }
  
    if ((currentBottomMotion || currentTopMotion) && !motionDetected && currentState == STATE_IDLE)
    {
      motionDetected = true;
      motionStartTime = millis();
      lastMotionTime = millis();
      
      bottomMotion = currentBottomMotion;
      topMotion = currentTopMotion;
      
      webPrintln("MOTION DETECTED: " + String(bottomMotion ? "Bottom " : "") + String(topMotion ? "Top " : "") + "PIR triggered");
    } 
    else if (currentBottomMotion || currentTopMotion) 
    {
      lastMotionTime = millis();
    }
  }

void handleMotionLogic() {
  if (!motionEnabled) return;
  
  unsigned long currentTime = millis();
  
  // Motion-triggered turn on
  if (motionDetected && currentTime - motionStartTime > motionDelay) {
    webPrintln("STARTING MOTION SEQUENCE - Bottom: " + String(bottomMotion) + ", Top: " + String(topMotion));
    startMotionSequence(bottomMotion);
    motionDetected = false; // Reset detection flag
  }
  
  // Auto-off after motion stops
  if (autoOffEnabled && systemOn && currentTime - lastMotionTime > autoOffDelay) {
    if (motionOnlyMode) {
      systemOn = false;
      FastLED.clear();
      segmentAnimation = false;
      webPrintln("Auto-off triggered - system turned OFF");
    } else {
      // Switch to background animation
      currentMode = backgroundMode;
      webPrintln("Auto-off triggered - switched to background animation");
    }
  }
}

// Start motion sequence
void startMotionSequence(bool fromBottom) {
  motionSequenceRunning = true;
  webPrintln("Motion sequence initialized - Direction: " + String(fromBottom ? "Bottom->Top" : "Top->Bottom"));
  
  // Clear background animation and turn off all LEDs
  systemOn = false; // Stop background animations
  FastLED.clear();
  FastLED.show();

  // Initialize motion sequence
  currentState = STATE_MOTION_TURN_ON;
  motionFromBottom = fromBottom;
  motionTurnOnStep = 0;
  motionSequenceStart = millis();
  lastMotionStepUpdate = millis();
  
  webPrintln("State: MOTION_TURN_ON");
}

// Process motion turn-on animation step by step
void processMotionTurnOn() {
  unsigned long now = millis();

  if (now - lastMotionStepUpdate < timing.stepTime) return;
  
  if (motionTurnOnStep > 10) {
    // Turn-on complete, move to delay state
    currentState = STATE_MOTION_DELAY;
    motionSequenceStart = millis();
    webPrintln("Turn-on complete, State: MOTION_DELAY");
    return;
  }

  int r, g, b;
  getCurrentColor(r, g, b);

  // Determine which segment to animate based on direction and step
  int segmentIndex;
  if (motionFromBottom) {
    // Bottom PIR triggered: animate from step 0 to 10 (bottom to top)
    segmentIndex = motionTurnOnStep;
  } else {
    // Top PIR triggered: animate from step 10 to 0 (top to bottom)
    segmentIndex = 10 - motionTurnOnStep;
  }

  webPrintln("Lighting segment " + String(segmentIndex) + " (step " + String(motionTurnOnStep) + ")");
  lightSegmentFromMiddle(segmentIndex, r, g, b);
  
  motionTurnOnStep++;
  lastMotionStepUpdate = now;
}

// Process motion delay (wait between turn-on and turn-off)
void processMotionDelay() {
  unsigned long now = millis();
  if (now - motionSequenceStart >= timing.waitTime) {
    // Delay complete, start turn-off
    currentState = STATE_MOTION_TURN_OFF;
    motionTurnOffStep = 0;
    lastMotionStepUpdate = millis();
    webPrintln("Delay complete, State: MOTION_TURN_OFF");
  }
}

// Process motion turn-off animation step by step
void processMotionTurnOff() {
  unsigned long now = millis();

  if (now - lastMotionStepUpdate < timing.stepTime) return;
  
  if (motionTurnOffStep > 10) {
    currentState = STATE_MOTION_FINAL_DELAY;
    motionSequenceStart = millis();
    webPrintln("Turn-off complete, State: FINAL_DELAY");
    return;
  }

  // Turn off in reverse order from whichever direction was triggered
  int segmentIndex;
  if (motionFromBottom) {
    // Bottom PIR triggered: clear from step 10 to 0 (top to bottom)
    segmentIndex = 10 - motionTurnOffStep;
  } else {
    // Top PIR triggered: clear from step 0 to 10 (bottom to top)
    segmentIndex = motionTurnOffStep;
  }
  
  webPrintln("Clearing segment " + String(segmentIndex) + " (step " + String(motionTurnOffStep) + ")");
  clearSegmentFromMiddle(segmentIndex);
  
  motionTurnOffStep++;
  lastMotionStepUpdate = now;
}

void lightSegmentFromMiddle(int segmentIndex, int r, int g, int b) {
  SegmentInfo seg = segments[segmentIndex];
  int middle = seg.middle;
  int start = seg.start;
  int end = seg.end;

  // Light up middle LED first
  leds[middle] = CRGB(r, g, b);
  FastLED.show();
  delay(timing.delayVal);

  // Then expand outward from middle
  int maxDistance = max(middle - start, end - middle);

  for (int distance = 1; distance <= maxDistance; distance++) {
    // Light up LED to the left of middle (if within segment bounds)
    if (middle - distance >= start) {
      leds[middle - distance] = CRGB(r, g, b);
    }

    // Light up LED to the right of middle (if within segment bounds)
    if (middle + distance <= end) {
      leds[middle + distance] = CRGB(r, g, b);
    }

    FastLED.show();
    delay(timing.delayVal);
  }
}

void clearSegmentFromMiddle(int segmentIndex) {
  SegmentInfo seg = segments[segmentIndex];
  int middle = seg.middle;
  int start = seg.start;
  int end = seg.end;

  // Clear middle LED first
  leds[middle] = CRGB::Black;
  FastLED.show();
  delay(timing.delayVal);

  // Then clear outward from middle
  int maxDistance = max(middle - start, end - middle);

  for (int distance = 1; distance <= maxDistance; distance++) {
    // Clear LED to the left of middle (if within segment bounds)
    if (middle - distance >= start) {
      leds[middle - distance] = CRGB::Black;
    }

    // Clear LED to the right of middle (if within segment bounds)
    if (middle + distance <= end) {
      leds[middle + distance] = CRGB::Black;
    }

    FastLED.show();
    delay(timing.delayVal);
  }
}

void setAllPixels(int r, int g, int b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(r, g, b);
  }
  FastLED.show();
}

void getCurrentColor(int &r, int &g, int &b) {
  if (useKelvin) {
    CRGB color = kelvinToRGB(kelvinTemp);
    r = color.r;
    g = color.g;
    b = color.b;
  } else {
    r = customColor.r;
    g = customColor.g;
    b = customColor.b;
  }
}

void updateAnimations() {
  // Don't run background animations during motion sequence
  if (currentState != STATE_IDLE) return;
  
  if (!systemOn && !motionOnlyMode) return;
  
  unsigned long currentTime = millis();
  
  // Handle segment-based motion animation
  if (segmentAnimation) {
    updateSegmentAnimation(currentTime);
    return;
  }
  
  // Regular animations
  switch (currentMode) {
    case SOLID:
      updateSolidAnimation();
      break;
    case FLICKER:
      if (currentTime - lastAnimationUpdate > timing.flickerSpeed) {
        flickerAnimation();
        lastAnimationUpdate = currentTime;
      }
      break;
    case PULSE:
      if (currentTime - lastAnimationUpdate > timing.pulseSpeed) {
        updatePulseAnimation();
        lastAnimationUpdate = currentTime;
      }
      break;
    case RAINBOW:
      if (currentTime - lastAnimationUpdate > timing.rainbowSpeed) {
        updateRainbowAnimation();
        lastAnimationUpdate = currentTime;
      }
      break;
    case CHASE:
      if (currentTime - lastAnimationUpdate > timing.chaseSpeed) {
        updateChaseAnimation();
        lastAnimationUpdate = currentTime;
      }
      break;
    case DNA_SPIRAL:
      if (currentTime - lastAnimationUpdate > timing.dnaSpeed) {
        updateDNASpiralAnimation();
        lastAnimationUpdate = currentTime;
      }
      break;
  }
}

void flickerAnimation() {
  unsigned long now = millis();
  
  // Check timing or force update
  if (!forceAnimationUpdate && (now - lastAnimationUpdate < timing.delayVal)) {
    return;
  }

  int r, g, b;
  getCurrentColor(r, g, b);
  
  float brightnessScale = constrain(brightness, 0, 100) / 100.0;

  static float time = 0.0;
 time += flickering.TIME_STEP / 1.5;


  for (int chunk = 0; chunk < NUM_CHUNKS; chunk++) {
    float flicker = flickering.FLICKER_AMPLITUDE * sin(time + chunkFlickerPhase[chunk]) + flickering.FLICKER_BASE;
    flicker = constrain(flicker, 0.0, 1.0); // Ensure flicker stays in valid range

    uint8_t flickerR = (uint8_t)(r * flicker);
    uint8_t flickerG = (uint8_t)(g * flicker);
    uint8_t flickerB = (uint8_t)(b * flicker);

    // Use the actual segment boundaries instead of calculated chunks
    SegmentInfo seg = segments[chunk];
    for (int i = seg.start; i <= seg.end && i < NUM_LEDS; i++) {
      leds[i] = CRGB(flickerR, flickerG, flickerB);               
    }
  }

  FastLED.show();
  lastAnimationUpdate = now;
  forceAnimationUpdate = false;
}

void updateSegmentAnimation(unsigned long currentTime) {
  if (currentTime - segmentTimer < timing.motionFadeSpeed) return;
  
  segmentTimer = currentTime;
  CRGB color = useKelvin ? kelvinToRGB(kelvinTemp) : customColor;
  
  // Light up current segment
  SegmentInfo seg = segments[currentSegment];
  int startLED = seg.start;
  int endLED = seg.end + 1;
  
  for (int i = startLED; i < endLED && i < NUM_LEDS; i++) {
    leds[i] = color;
  }
  
  // Progress to next segments
  bool finished = true;
  
  if (expandingUp && currentSegment < NUM_SEGMENTS - 1) {
    currentSegment++;
    finished = false;
  }
  if (expandingDown && currentSegment > 0) {
    currentSegment--;
    finished = false;
  }
  
  if (finished) {
    segmentAnimation = false;
    // Switch to regular animation after motion sequence
    if (!motionOnlyMode) {
      currentMode = backgroundMode;
    }
  }
}

void updateSolidAnimation() {
  CRGB color = useKelvin ? kelvinToRGB(kelvinTemp) : customColor;
  fill_solid(leds, NUM_LEDS, color);
}

void updatePulseAnimation() {
  pulsePhase += 0.1;
  if (pulsePhase > 2 * PI) pulsePhase = 0;
  
  uint8_t intensity = (sin(pulsePhase) + 1) * 127.5;
  CRGB color = useKelvin ? kelvinToRGB(kelvinTemp) : customColor;
  
  fill_solid(leds, NUM_LEDS, color);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(intensity);
  }
}

void updateRainbowAnimation() {
  rainbowHue += 2;
  fill_rainbow(leds, NUM_LEDS, rainbowHue, 7);
}

void updateChaseAnimation() {
  FastLED.clear();
  
  CRGB color = useKelvin ? kelvinToRGB(kelvinTemp) : customColor;
  int tailLength = 20;
  
  for (int i = 0; i < tailLength; i++) {
    int pos = (chasePosition - i + NUM_LEDS) % NUM_LEDS;
    uint8_t intensity = map(i, 0, tailLength - 1, 255, 0);
    leds[pos] = color;
    leds[pos].nscale8(intensity);
  }
  
  chasePosition = (chasePosition + 1) % NUM_LEDS;
}

void updateDNASpiralAnimation() {
  dnaPhase += 0.05;
  if (dnaPhase > 2 * PI) dnaPhase = 0;
  
  FastLED.clear();
  
  for (int i = 0; i < NUM_LEDS; i++) {
    float position = (float)i / NUM_LEDS;
    float wave1 = sin(position * 4 * PI + dnaPhase);
    float wave2 = sin(position * 4 * PI + dnaPhase + PI);
    
    uint8_t brightness1 = (wave1 + 1) * 127.5;
    uint8_t brightness2 = (wave2 + 1) * 127.5;
    
    leds[i] = CRGB(brightness1, 0, brightness2);
  }
}

CRGB kelvinToRGB(uint16_t kelvin) {
  float temp = kelvin / 100.0;
  
  uint8_t red, green, blue;
  
  if (temp <= 66) {
    red = 255;
  } else {
    red = temp - 60;
    red = 329.698727446 * pow(red, -0.1332047592);
    red = constrain(red, 0, 255);
  }
  
  if (temp <= 66) {
    green = temp;
    green = 99.4708025861 * log(green) - 161.1195681661;
    green = constrain(green, 0, 255);
  } else {
    green = temp - 60;
    green = 288.1221695283 * pow(green, -0.0755148492);
    green = constrain(green, 0, 255);
  }
  
  if (temp >= 66) {
    blue = 255;
  } else if (temp <= 19) {
    blue = 0;
  } else {
    blue = temp - 10;
    blue = 138.5177312231 * log(blue) - 305.0447927307;
    blue = constrain(blue, 0, 255);
  }
  
  return CRGB(red, green, blue);
}

void startupAnimation() {
  webPrintln("Running startup fade animation...");

  // Repeat the fade-in/out sequence 3 times
  for (int cycle = 0; cycle < 3; cycle++) {

    // Fade all LEDs on (0 → 255)
    for (int i = 0; i <= 255; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::White); // or choose another color
      FastLED.setBrightness(i);
      FastLED.show();
      delay(0.4);
    }
      delay(10);
    // Fade all LEDs off (255 → 0)
    for (int i = 255; i >= 0; i--) {
      FastLED.setBrightness(i);
      FastLED.show();
      
    }
    delay(50);
  }

  // Ensure all LEDs are off when finished
  FastLED.clear();
  FastLED.show();

  webPrintln("Startup fade animation complete");
}


String getWebInterface() {
  return R"====(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 LED Controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #1a1a1a, #2a2a2a); color: white; }
        .container { max-width: 1200px; margin: 0 auto; display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
        .section { background: linear-gradient(135deg, #2a2a2a, #3a3a3a); padding: 25px; border-radius: 15px; box-shadow: 0 8px 32px rgba(0,0,0,0.3); border: 1px solid #404040; }
        .control { margin: 15px 0; display: flex; align-items: center; justify-content: space-between; }
        .control label { flex: 1; font-weight: 500; }
        button { padding: 12px 24px; background: linear-gradient(135deg, #4CAF50, #45a049); color: white; border: none; border-radius: 8px; cursor: pointer; font-weight: 500; transition: all 0.3s ease; }
        button:hover { transform: translateY(-2px); box-shadow: 0 4px 16px rgba(76, 175, 80, 0.3); }
        button:active { transform: translateY(0); }
        input, select { padding: 10px; border-radius: 8px; border: 1px solid #555; background: linear-gradient(135deg, #333, #444); color: white; transition: all 0.3s ease; }
        input:focus, select:focus { border-color: #4CAF50; box-shadow: 0 0 0 2px rgba(76, 175, 80, 0.2); outline: none; }
        .status { background: linear-gradient(135deg, #333, #444); padding: 15px; border-radius: 10px; margin: 10px 0; border-left: 4px solid #4CAF50; }
        .color-picker { width: 60px; height: 40px; border-radius: 8px; }
        .slider { width: 100%; height: 8px; border-radius: 4px; background: #555; outline: none; -webkit-appearance: none; }
        .slider::-webkit-slider-thumb { appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #4CAF50; cursor: pointer; }
        .slider::-moz-range-thumb { width: 20px; height: 20px; border-radius: 50%; background: #4CAF50; cursor: pointer; border: none; }
        h1 { text-align: center; grid-column: 1 / -1; color: #4CAF50; text-shadow: 0 2px 10px rgba(76, 175, 80, 0.3); margin-bottom: 30px; }
        h2 { color: #4CAF50; margin-top: 0; }
        .console { background: #000; color: #0f0; font-family: 'Courier New', monospace; padding: 15px; border-radius: 10px; max-height: 400px; overflow-y: auto; white-space: pre-wrap; font-size: 12px; border: 1px solid #333; }
        .console-section { grid-column: 1 / -1; }
        .value-display { background: #444; padding: 5px 10px; border-radius: 5px; min-width: 50px; text-align: center; }
        @media (max-width: 768px) { .container { grid-template-columns: 1fr; } }
    </style>
</head>
<body>
    <div class="container">
        <h1> ESP32 LED Controller</h1>
        <div id="status" class="status"></div>
        
        <div class="section">
            <h2> Power & Motion</h2>
            <div class="control">
                <label>System Power:</label>
                <button onclick="togglePower()" id="powerBtn">OFF</button>
            </div>
            <div class="control">
                <label>Motion Detection:</label>
                <button onclick="toggleMotion()" id="motionBtn">Enabled</button>
            </div>
            <div class="control">
                <label>Auto Off:</label>
                <button onclick="toggleAutoOff()" id="autoOffBtn">Enabled</button>
            </div>
            <div class="control">
                <label>Motion Only Mode:</label>
                <button onclick="toggleMotionOnly()" id="motionOnlyBtn">Disabled</button>
            </div>
        </div>
        
        <div class="section">
            <h2> Brightness & Color</h2>
            <div class="control">
                <label>Brightness:</label>
                <input type="range" id="brightness" class="slider" min="0" max="255" value="255" oninput="setBrightness()">
                <div class="value-display" id="brightnessValue">100%</div>
            </div>
            <div class="control">
                <label>Color Temperature:</label>
                <input type="range" id="kelvin" class="slider" min="1000" max="10000" step="100" value="3000" oninput="setKelvin()">
                <div class="value-display" id="kelvinValue">3000K</div>
            </div>
            <div class="control">
                <label>Custom Color:</label>
                <input type="color" id="colorPicker" class="color-picker" onchange="setCustomColor()">
            </div>
        </div>
        
        <div class="section">
            <h2> Animation</h2>
            <div class="control">
                <label>Current Mode:</label>
                <select id="animationMode" onchange="setAnimation()">
                    <option value="0">Solid</option>
                    <option value="1">Flicker</option>
                    <option value="2">Pulse</option>
                    <option value="3">Rainbow</option>
                    <option value="4">Chase</option>
                    <option value="5">DNA Spiral</option>
                </select>
            </div>
            <div class="control">
                <label>Background Mode:</label>
                <select id="backgroundMode" onchange="setBackgroundAnimation()">
                    <option value="0">Solid</option>
                    <option value="1">Flicker</option>
                    <option value="2">Pulse</option>
                    <option value="3">Rainbow</option>
                    <option value="4">Chase</option>
                    <option value="5" selected>DNA Spiral</option>
                </select>
            </div>
        </div>
        
        <div class="section">
            <h2>Timing</h2>
            <div class="control">
                <label>Flicker Speed:</label>
                <input type="range" id="flickerSpeed" class="slider" min="15" max="50" value="50" oninput="setTiming()">
                <div class="value-display" id="flickerValue">50</div>
            </div>
            <div class="control">
                <label>Pulse Speed:</label>
                <input type="range" id="pulseSpeed" class="slider" min="10" max="200" value="30" oninput="setTiming()">
                <div class="value-display" id="pulseValue">30</div>
            </div>
            <div class="control">
                <label>Rainbow Speed:</label>
                <input type="range" id="rainbowSpeed" class="slider" min="10" max="200" value="20" oninput="setTiming()">
                <div class="value-display" id="rainbowValue">20</div>
            </div>
            <div class="control">
                <label>Chase Speed:</label>
                <input type="range" id="chaseSpeed" class="slider" min="50" max="500" value="100" oninput="setTiming()">
                <div class="value-display" id="chaseValue">100</div>
            </div>
            <div class="control">
                <label>DNA Speed:</label>
                <input type="range" id="dnaSpeed" class="slider" min="10" max="200" value="40" oninput="setTiming()">
                <div class="value-display" id="dnaValue">40</div>
            </div>
        </div>
        
        <div class="section console-section">
            <h2>📟 System Console</h2>
            <div id="console" class="console">Loading console...</div>
            <button onclick="clearConsole()" style="margin-top: 10px; background: #666;">Clear Console</button>
            <button onclick="reboot()" style="background: #f44336; margin-left: 10px;">Reboot Device</button>
        </div>
    </div>

    <script>
        let status = {};
        
        function updateStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    status = data;
                    const states = {
                        0: 'IDLE',
                        1: 'TURN_ON',
                        2: 'DELAY',
                        3: 'TURN_OFF',
                        4: 'FINAL_DELAY'
                    };
                    
                    document.getElementById('status').innerHTML = `
                        Status: <strong>${data.on ? 'ON' : 'OFF'}</strong> | 
                        Motion: <strong>${data.motionDetected ? 'DETECTED' : 'None'}</strong> | 
                        State: <strong>${states[data.currentState] || 'UNKNOWN'}</strong> | 
                        WiFi: <strong>${data.wifi.ssid}</strong> (${data.wifi.rssi}dBm)
                    `;
                    
                    // Update UI elements
                    document.getElementById('powerBtn').textContent = data.on ? 'ON' : 'OFF';
                    document.getElementById('motionBtn').textContent = data.motionEnabled ? 'Enabled' : 'Disabled';
                    document.getElementById('autoOffBtn').textContent = data.autoOffEnabled ? 'Enabled' : 'Disabled';
                    document.getElementById('motionOnlyBtn').textContent = data.motionOnlyMode ? 'Enabled' : 'Disabled';
                    
                    if (document.getElementById('brightness').value != data.brightness) {
                        document.getElementById('brightness').value = data.brightness;
                    }
                    document.getElementById('brightnessValue').textContent = Math.round((data.brightness / 255) * 100) + '%';
                    
                    if (document.getElementById('kelvin').value != data.kelvinTemp) {
                        document.getElementById('kelvin').value = data.kelvinTemp;
                    }
                    document.getElementById('kelvinValue').textContent = data.kelvinTemp + 'K';
                    
                    document.getElementById('animationMode').value = data.animationMode;
                    document.getElementById('backgroundMode').value = data.backgroundMode;
                })
                .catch(error => console.error('Status update failed:', error));
        }
        
        function updateConsole() {
            fetch('/api/console')
                .then(response => response.text())
                .then(data => {
                    const console = document.getElementById('console');
                    if (console.textContent !== data) {
                        console.textContent = data;
                        console.scrollTop = console.scrollHeight;
                    }
                })
                .catch(error => console.error('Console update failed:', error));
        }
        
        function clearConsole() {
            // This would require server endpoint to clear buffer
            document.getElementById('console').textContent = 'Console cleared (restart device to clear server buffer)';
        }
        
        function togglePower() {
            const newState = !status.on;
            fetch('/api/power', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'on=' + newState
            });
        }
        
        function toggleMotion() {
            const newState = !status.motionEnabled;
            fetch('/api/motion', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'enabled=' + newState
            });
        }
        
        function toggleAutoOff() {
            const newState = !status.autoOffEnabled;
            fetch('/api/motion', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'autoOff=' + newState
            });
        }
        
        function toggleMotionOnly() {
            const newState = !status.motionOnlyMode;
            fetch('/api/motion', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'motionOnly=' + newState
            });
        }
        
        function setBrightness() {
            const value = document.getElementById('brightness').value;
            const percent = Math.round((value / 255) * 100);
            document.getElementById('brightnessValue').textContent = percent + '%';
            fetch('/api/brightness', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'value=' + value
            });
        }
        
        function setKelvin() {
            const value = document.getElementById('kelvin').value;
            document.getElementById('kelvinValue').textContent = value + 'K';
            fetch('/api/color', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'kelvin=' + value
            });
        }
        
        function setCustomColor() {
            const color = document.getElementById('colorPicker').value;
            const r = parseInt(color.substr(1, 2), 16);
            const g = parseInt(color.substr(3, 2), 16);
            const b = parseInt(color.substr(5, 2), 16);
            fetch('/api/color', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `r=${r}&g=${g}&b=${b}`
            });
        }
        
        function setAnimation() {
            const mode = document.getElementById('animationMode').value;
            fetch('/api/animation', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'mode=' + mode
            });
        }
        
        function setBackgroundAnimation() {
            const mode = document.getElementById('backgroundMode').value;
            fetch('/api/animation', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'background=' + mode
            });
        }
        
        function setTiming() {
            const flicker = document.getElementById('flickerSpeed').value;
            const pulse = document.getElementById('pulseSpeed').value;
            const rainbow = document.getElementById('rainbowSpeed').value;
            const chase = document.getElementById('chaseSpeed').value;
            const dna = document.getElementById('dnaSpeed').value;
            
            // Update display values
            document.getElementById('flickerValue').textContent = flicker;
            document.getElementById('pulseValue').textContent = pulse;
            document.getElementById('rainbowValue').textContent = rainbow;
            document.getElementById('chaseValue').textContent = chase;
            document.getElementById('dnaValue').textContent = dna;
            
            fetch('/api/timing', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `flicker=${flicker}&pulse=${pulse}&rainbow=${rainbow}&chase=${chase}&dna=${dna}`
            });
        }
        
        function reboot() {
            if (confirm('Are you sure you want to reboot the device?')) {
                fetch('/api/reboot', {method: 'POST'});
                alert('Device is rebooting...');
            }
        }
        
        // Update status every 1 second
        setInterval(updateStatus, 1000);
        // Update console every 2 seconds
        setInterval(updateConsole, 2000);
        
        // Initial load
        updateStatus();
        updateConsole();
    </script>
</body>
</html>
)====";
}