#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>

#define STATUS_LED 2  // Built-in LED on many ESP32 boards (adjust if needed)

// Stepper Motor Pins
#define DIR1 12
#define STEP1 14
#define DIR2 21
#define STEP2 22

// End Stop Pins for Slider Motor
#define ENDSTOP_A 32
#define ENDSTOP_B 33

// Motor setup
AccelStepper sliderMotor(AccelStepper::DRIVER, STEP1, DIR1);
AccelStepper panMotor(AccelStepper::DRIVER, STEP2, DIR2);

// Web server
AsyncWebServer server(80);

// Movement state
int sliderSpeed = 800;
int sliderDir = 0; // -1 = left, 1 = right, 0 = stop

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Camera Slider</title>
  <style>
    body {
      background-color: #121212;
      color: white;
      font-family: sans-serif;
      text-align: center;
      margin-top: 40px;
    }
    button {
      font-size: 2rem;
      padding: 1rem 2rem;
      margin: 1rem;
      background: #333;
      color: white;
      border: none;
      border-radius: 10px;
    }
    button:active {
      background: #555;
    }
  </style>
</head>
<body>
  <h2>Slider Control</h2>

  <p>
    Speed: <span id="speedVal">800</span> steps/sec
  </p>
  <input type="range" min="200" max="1000" value="800" id="speedSlider" oninput="updateSpeed(this.value)" />

  <br><br>

  <button onmousedown="startMove('left')" onmouseup="stopMove()" ontouchstart="startMove('left')" ontouchend="stopMove()">&lt;</button>
  <button onmousedown="startMove('right')" onmouseup="stopMove()" ontouchstart="startMove('right')" ontouchend="stopMove()">&gt;</button>

  <script>
    function updateSpeed(val) {
      document.getElementById('speedVal').innerText = val;
      fetch(`/set_speed?value=${val}`);
    }

    function startMove(dir) {
      fetch(`/move_slider?dir=${dir}&action=start`);
    }

    function stopMove() {
      fetch(`/move_slider?action=stop`);
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);

  // Init End Stops
  pinMode(ENDSTOP_A, INPUT_PULLUP);
  pinMode(ENDSTOP_B, INPUT_PULLUP);

  // Init Motors
  sliderMotor.setMaxSpeed(1000);
  sliderMotor.setAcceleration(300);
  panMotor.setMaxSpeed(1000);
  panMotor.setAcceleration(300);

  // WiFi config
  WiFiManager wm;

  // Blink LED while waiting for connection
  unsigned long previousMillis = 0;
  bool ledState = false;
  const long interval = 500;

  Serial.println("Starting WiFiManager...");

  while (!wm.autoConnect("CameraSlider-Setup")) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      ledState = !ledState;
      digitalWrite(STATUS_LED, ledState);
    }
  }

  // Connected: turn LED off
  digitalWrite(STATUS_LED, LOW);

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Serve UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  // Handle slider movement
  server.on("/move_slider", HTTP_GET, [](AsyncWebServerRequest *request) {
    String action = request->getParam("action")->value();
    if (action == "start") {
      if (request->hasParam("dir")) {
        String dir = request->getParam("dir")->value();
        sliderDir = (dir == "left") ? -1 : 1;
      }
    } else if (action == "stop") {
      sliderDir = 0;
    }
    request->send(200, "text/plain", "OK");
  });

  // Handle speed change
  server.on("/set_speed", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      sliderSpeed = request->getParam("value")->value().toInt();
      sliderMotor.setMaxSpeed(sliderSpeed);
    }
    request->send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  // Check end stops
  if ((digitalRead(ENDSTOP_A) == HIGH && sliderDir == -1) ||
      (digitalRead(ENDSTOP_B) == HIGH && sliderDir == 1)) {
    // sliderDir = 0; // stop if limit reached
  }

  // Move slider based on direction
  if (sliderDir == 0) {
    sliderMotor.setSpeed(0);
  } else {
    sliderMotor.setSpeed(sliderSpeed * sliderDir);
  }

  sliderMotor.runSpeed();
}
