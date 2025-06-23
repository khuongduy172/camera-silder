// Slider v2 - Manual Control + Speed Sliders + Keyframe + MultiStepper Sync + Timed Move + WiFi Status LED + Endstop Switches

#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>
#include <MultiStepper.h>

#define DIR_SLIDER 4
#define STEP_SLIDER 3
#define DIR_PAN 2
#define STEP_PAN 1
#define STATUS_LED 8 // ESP32-C3 SuperMini built-in LED
#define ENDSTOP_MAX 5  // Updated pin for other end of slider
#define ENDSTOP_MIN 6  // Updated pin for one end of slider

AccelStepper sliderMotor(AccelStepper::DRIVER, STEP_SLIDER, DIR_SLIDER);
AccelStepper panMotor(AccelStepper::DRIVER, STEP_PAN, DIR_PAN);
MultiStepper steppers;

AsyncWebServer server(80);

int sliderDir = 0;
int panDir = 0;
int sliderSpeed = 1000;
int panSpeed = 100;

long posA_slider = 0;
long posA_pan = 0;
long posB_slider = 0;
long posB_pan = 0;

int moveDuration = 10; // in seconds

bool movingToTarget = false;
long targetPositions[2] = {0, 0};

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Camera Slider v2</title>
  <style>
    body { background: #121212; color: white; font-family: sans-serif; text-align: center; padding-top: 40px; }
    button { font-size: 1.2rem; margin: 10px; padding: 15px 30px; border: none; border-radius: 10px; background: #333; color: white; }
    button:active { background: #555; }
    .slider { width: 80%; margin: 10px auto; }
  </style>
</head>
<body>
  <h2>Manual Control</h2>

  <h3>Slider</h3>
  <p>Speed: <span id="sliderSpeedVal">1000</span> steps/sec</p>
  <input type="range" min="100" max="10000" value="1000" class="slider" id="sliderSpeed" oninput="updateSliderSpeed(this.value)">
  <br>
  <button onclick="fetch('/slider?dir=left')">Left</button>
  <button onclick="fetch('/slider?dir=right')">Right</button>
  <button onclick="fetch('/slider?dir=stop')">Stop</button>

  <h3>Pan</h3>
  <p>Speed: <span id="panSpeedVal">100</span> steps/sec</p>
  <input type="range" min="50" max="1000" value="100" class="slider" id="panSpeed" oninput="updatePanSpeed(this.value)">
  <br>
  <button onclick="fetch('/pan?dir=left')">Pan Left</button>
  <button onclick="fetch('/pan?dir=right')">Pan Right</button>
  <button onclick="fetch('/pan?dir=stop')">Stop Pan</button>

  <h3>Keyframe Control</h3>
  <button onclick="setPoint('A')">Set Point A</button>
  <button onclick="setPoint('B')">Set Point B</button>
  <p>Point A: <span id="posA">(0, 0)</span></p>
  <p>Point B: <span id="posB">(0, 0)</span></p>
  <br>
  <button onclick="fetch('/move_to?target=A')">Move to A</button>
  <button onclick="fetch('/move_to?target=B')">Move to B</button>
  <br>
  <p>Duration: <span id="durationVal">10</span> sec</p>
  <input type="range" min="1" max="30" value="10" class="slider" id="durationSlider" oninput="updateDuration(this.value)">

  <script>
    function updateSliderSpeed(val) {
      document.getElementById('sliderSpeedVal').innerText = val;
      fetch(`/set_slider_speed?value=${val}`);
    }
    function updatePanSpeed(val) {
      document.getElementById('panSpeedVal').innerText = val;
      fetch(`/set_pan_speed?value=${val}`);
    }
    function updateDuration(val) {
      document.getElementById('durationVal').innerText = val;
      fetch(`/set_duration?value=${val}`);
    }
    function updatePositions(a, b) {
      document.getElementById('posA').innerText = `(${a[0]}, ${a[1]})`;
      document.getElementById('posB').innerText = `(${b[0]}, ${b[1]})`;
    }
    function setPoint(pos) {
      fetch(`/set_point?pos=${pos}`)
        .then(() => fetch('/get_points'))
        .then(res => res.json())
        .then(data => updatePositions(data.A, data.B));
    }
    fetch('/get_points')
      .then(response => response.json())
      .then(data => updatePositions(data.A, data.B));
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(ENDSTOP_MIN, INPUT_PULLUP);
  pinMode(ENDSTOP_MAX, INPUT_PULLUP);
  digitalWrite(STATUS_LED, HIGH);

  sliderMotor.setMaxSpeed(10000);
  sliderMotor.setAcceleration(300);
  panMotor.setMaxSpeed(1000);
  panMotor.setAcceleration(300);

  steppers.addStepper(sliderMotor);
  steppers.addStepper(panMotor);

  WiFiManager wm;
  if (!wm.autoConnect("Slider-v2")) {
    ESP.restart();
  }
  digitalWrite(STATUS_LED, LOW);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send_P(200, "text/html", index_html);
  });

  server.on("/slider", HTTP_GET, [](AsyncWebServerRequest *req){
    String dir = req->getParam("dir")->value();
    sliderDir = (dir == "left") ? -1 : (dir == "right") ? 1 : 0;
    if (sliderDir != 0) movingToTarget = false;
    req->send(200, "text/plain", "OK");
  });

  server.on("/pan", HTTP_GET, [](AsyncWebServerRequest *req){
    String dir = req->getParam("dir")->value();
    panDir = (dir == "left") ? -1 : (dir == "right") ? 1 : 0;
    if (panDir != 0) movingToTarget = false;
    req->send(200, "text/plain", "OK");
  });

  server.on("/set_slider_speed", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("value")) {
      sliderSpeed = req->getParam("value")->value().toInt();
      sliderMotor.setMaxSpeed(sliderSpeed);
    }
    req->send(200, "text/plain", "Slider speed updated");
  });

  server.on("/set_pan_speed", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("value")) {
      panSpeed = req->getParam("value")->value().toInt();
      panMotor.setMaxSpeed(panSpeed);
    }
    req->send(200, "text/plain", "Pan speed updated");
  });

  server.on("/set_duration", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("value")) {
      moveDuration = req->getParam("value")->value().toInt();
    }
    req->send(200, "text/plain", "Duration updated");
  });

  server.on("/set_point", HTTP_GET, [](AsyncWebServerRequest *req){
    String pos = req->getParam("pos")->value();
    if (pos == "A") {
      posA_slider = sliderMotor.currentPosition();
      posA_pan = panMotor.currentPosition();
    } else if (pos == "B") {
      posB_slider = sliderMotor.currentPosition();
      posB_pan = panMotor.currentPosition();
    }
    req->send(200, "text/plain", "Point " + pos + " set");
  });

  server.on("/get_points", HTTP_GET, [](AsyncWebServerRequest *req){
    String json = "{";
    json += "\"A\": [" + String(posA_slider) + ", " + String(posA_pan) + "], ";
    json += "\"B\": [" + String(posB_slider) + ", " + String(posB_pan) + "]";
    json += "}";
    req->send(200, "application/json", json);
  });

  server.on("/move_to", HTTP_GET, [](AsyncWebServerRequest *req){
    String target = req->getParam("target")->value();
    long target_slider = (target == "A") ? posA_slider : posB_slider;
    long target_pan = (target == "A") ? posA_pan : posB_pan;

    long delta_slider = abs(target_slider - sliderMotor.currentPosition());
    long delta_pan = abs(target_pan - panMotor.currentPosition());

    if ((target_slider > sliderMotor.currentPosition() && digitalRead(ENDSTOP_MAX) == LOW) ||
        (target_slider < sliderMotor.currentPosition() && digitalRead(ENDSTOP_MIN) == LOW)) {
      req->send(200, "text/plain", "Endstop hit, cannot move");
      return;
    }

    float speed_slider = (float)delta_slider / moveDuration;
    float speed_pan = (float)delta_pan / moveDuration;

    sliderMotor.setMaxSpeed(speed_slider);
    panMotor.setMaxSpeed(speed_pan);

    targetPositions[0] = target_slider;
    targetPositions[1] = target_pan;
    steppers.moveTo(targetPositions);
    movingToTarget = true;

    sliderDir = 0;
    panDir = 0;
    req->send(200, "text/plain", "Moving to " + target);
  });

  server.begin();
}

void loop() {
  if (movingToTarget) {
    if (!steppers.run()) {
      movingToTarget = false;
    }
  } else {
    if (sliderDir != 0) {
      if ((sliderDir == -1 && digitalRead(ENDSTOP_MIN) == LOW) || (sliderDir == 1 && digitalRead(ENDSTOP_MAX) == LOW)) {
        sliderMotor.setSpeed(0);
      } else {
        sliderMotor.setSpeed(sliderSpeed * sliderDir);
        sliderMotor.runSpeed();
      }
    }

    if (panDir != 0) {
      panMotor.setSpeed(panSpeed * panDir);
      panMotor.runSpeed();
    }
  }
}
