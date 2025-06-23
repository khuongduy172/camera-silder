#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>

#define STATUS_LED 8
#define DIR1 4
#define STEP1 3
#define DIR2 2
#define STEP2 1
#define ENDSTOP_A 5
#define ENDSTOP_B 6

AccelStepper sliderMotor(AccelStepper::DRIVER, STEP1, DIR1);
AccelStepper panMotor(AccelStepper::DRIVER, STEP2, DIR2);

AsyncWebServer server(80);

int sliderSpeed = 1600;
int sliderDir = 0;

int panDir = 0;
int panSpeed = 100;

// Keyframe variables
long posA_slider = 0, posA_pan = 0;
long posB_slider = 0, posB_pan = 0;
int moveDuration = 10;
bool isRunning = false;
bool isMoveAB = false;
unsigned long moveStartTime;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Camera Slider</title>
  <style>
    body { background-color: #121212; color: white; font-family: sans-serif; text-align: center; margin-top: 40px; }
    button { font-size: 1rem; padding: 1rem; margin: 0.5rem; background: #333; color: white; border: none; border-radius: 10px; }
    button:active { background: #555; }
    .speed-slider { width: 70%; height: 15px; background: #333; border-radius: 5px; outline: none; }
  </style>
</head>
<body>
  <h2>Slider Control</h2>
  <p>Speed: <span id="speedVal">1600</span> steps/sec</p>
  <input class="speed-slider" type="range" min="20" max="10000" value="1600" id="speedSlider" oninput="updateSpeed(this.value)" />
  <br><br>
  <button onclick="startMove('left')">Move Left</button>
  <button onclick="startMove('right')">Move Right</button>
  <button onclick="stopMove()">Stop</button>

  <h2>Pan Control</h2>
  <h3>Pan Speed: <span id="panSpeedVal">100</span> steps/sec</h3>
  <input class="speed-slider" type="range" min="20" max="1000" value="100" id="panSpeedSlider" oninput="updatePanSpeed(this.value)" />
  <br><br>
  <button onclick="startPan('left')">Pan Left</button>
  <button onclick="startPan('right')">Pan Right</button>
  <button onclick="stopPan()">Stop Pan</button>

  <h2>Keyframe Control</h2>
  <p id="pointAPos">A: Not set</p>
  <p id="pointBPos">B: Not set</p>
  <button onclick="setPoint('A')">Set A</button>
  <button onclick="setPoint('B')">Set B</button>
  <button onclick="fetch('/move_to?pt=A')">Move to A</button>
  <button onclick="fetch('/move_to?pt=B')">Move to B</button>
  <button onclick="fetch('/start_move')">Start</button>
  <button onclick="fetch('/stop_all')">Stop</button>
  <br>
  <p>Duration: <span id="durVal">10</span> sec</p>
  <input class="speed-slider" type="range" min="1" max="60" value="10" id="durSlider" oninput="setDuration(this.value)" />

  <script>
    function updateSpeed(val) {
      document.getElementById('speedVal').innerText = val;
      fetch(`/set_speed?value=${val}`);
    }
    function startMove(dir) { fetch(`/move_slider?dir=${dir}&action=start`); }
    function stopMove() { fetch(`/move_slider?action=stop`); }
    function updatePanSpeed(val) {
      document.getElementById('panSpeedVal').innerText = val;
      fetch(`/set_pan_speed?value=${val}`);
    }
    function startPan(dir) { fetch(`/move_pan?dir=${dir}&action=start`); }
    function stopPan() { fetch(`/move_pan?action=stop`); }
    function setDuration(val) {
      document.getElementById('durVal').innerText = val;
      fetch(`/set_duration?value=${val}`);
    }
    function setPoint(which) {
      fetch(`/set_point?point=${which}`)
        .then(res => res.text())
        .then(data => {
          document.getElementById(`point${which}Pos`).innerText = `${which}: ${data}`;
        });
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(ENDSTOP_A, INPUT_PULLUP);
  pinMode(ENDSTOP_B, INPUT_PULLUP);
  sliderMotor.setMaxSpeed(10000);
  sliderMotor.setAcceleration(300);
  panMotor.setMaxSpeed(1000);
  panMotor.setAcceleration(300/8);

  WiFiManager wm;
  digitalWrite(STATUS_LED, HIGH);
  while (!wm.autoConnect("CameraSlider-Setup")) { digitalWrite(STATUS_LED, HIGH); }
  digitalWrite(STATUS_LED, LOW);
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/move_slider", HTTP_GET, [](AsyncWebServerRequest *request) {
    String action = request->getParam("action")->value();
    if (action == "start" && request->hasParam("dir")) {
      sliderDir = (request->getParam("dir")->value() == "left") ? -1 : 1;
    } else if (action == "stop") {
      sliderDir = 0;
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/set_speed", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      sliderSpeed = request->getParam("value")->value().toInt();
      sliderMotor.setMaxSpeed(sliderSpeed);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/move_pan", HTTP_GET, [](AsyncWebServerRequest *request) {
    String action = request->getParam("action")->value();
    if (action == "start" && request->hasParam("dir")) {
      panDir = (request->getParam("dir")->value() == "left") ? -1 : 1;
    } else if (action == "stop") {
      panDir = 0;
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/set_pan_speed", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      panSpeed = request->getParam("value")->value().toInt();
      panMotor.setMaxSpeed(panSpeed);
    }
    request->send(200, "text/plain", "Pan speed updated");
  });

  // Handle setting A/B points
  server.on("/set_point", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("point")) {
      String p = request->getParam("point")->value();
      if (p == "A") {
        posA_slider = sliderMotor.currentPosition();
        posA_pan = panMotor.currentPosition();
        request->send(200, "text/plain", "Slider=" + String(posA_slider) + ", Pan=" + String(posA_pan));
      } else if (p == "B") {
        posB_slider = sliderMotor.currentPosition();
        posB_pan = panMotor.currentPosition();
        request->send(200, "text/plain", "Slider=" + String(posB_slider) + ", Pan=" + String(posB_pan));
      } else {
        request->send(400, "text/plain", "Invalid point");
      }
    } else {
      request->send(400, "text/plain", "Missing point");
    }
  });

  server.on("/move_to", HTTP_GET, [](AsyncWebServerRequest *request) {
    String pt = request->getParam("pt")->value();
    if (pt == "A") {
      sliderMotor.moveTo(posA_slider);
      panMotor.moveTo(posA_pan);
    } else if (pt == "B") {
      sliderMotor.moveTo(posB_slider);
      panMotor.moveTo(posB_pan);
    }
    isMoveAB = true;
    request->send(200, "text/plain", "Moving to point");
  });

  server.on("/set_duration", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("value")) {
      moveDuration = request->getParam("value")->value().toInt();
    }
    request->send(200, "text/plain", "Duration set");
  });

  server.on("/start_move", HTTP_GET, [](AsyncWebServerRequest *request) {
    long deltaSlider = posB_slider - posA_slider;
    long deltaPan = posB_pan - posA_pan;
    float stepsPerSec_slider = deltaSlider / (float)moveDuration;
    float stepsPerSec_pan = deltaPan / (float)moveDuration;
    sliderMotor.setSpeed(stepsPerSec_slider);
    panMotor.setSpeed(stepsPerSec_pan);
    isRunning = true;
    moveStartTime = millis();
    request->send(200, "text/plain", "Start moving");
  });

  server.on("/stop_all", HTTP_GET, [](AsyncWebServerRequest *request) {
    sliderMotor.stop();
    sliderMotor.setSpeed(0);
    panMotor.stop();
    panMotor.setSpeed(0);
    isRunning = false;
    isMoveAB = false;
    request->send(200, "text/plain", "Stopped");
  });

  server.begin();
}

void loop() {
  if ((digitalRead(ENDSTOP_A) == LOW && sliderDir == 1) ||
      (digitalRead(ENDSTOP_B) == LOW && sliderDir == -1)) {
    sliderDir = 0;
  }
  
  if (!isRunning && !isMoveAB) {
    if (sliderDir == 0) {
      sliderMotor.setSpeed(0);
    } else {
      sliderMotor.setSpeed(sliderSpeed * sliderDir);
    }
    sliderMotor.runSpeed();

    if (panDir == 0) {
      panMotor.setSpeed(0);
    } else {
      panMotor.setSpeed(panSpeed * panDir);
    }
    panMotor.runSpeed();
  }

  if (isRunning) {
    sliderMotor.runSpeed();
    panMotor.runSpeed();
    if ((millis() - moveStartTime) > (moveDuration * 1000)) {
      sliderMotor.setSpeed(0);
      panMotor.setSpeed(0);
      isRunning = false;
    }
  }

  if (isMoveAB) {
    sliderMotor.run();
    panMotor.run();
    if (!sliderMotor.isRunning() && !panMotor.isRunning()) {
      isMoveAB = false;
    }
  }
}
