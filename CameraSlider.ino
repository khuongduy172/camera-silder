#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>

// Stepper Motor Pins
#define DIR1 18
#define STEP1 19
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

// Slider position variables

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <title>Camera Slider</title>
</head>
<body>
  <h2>Camera Slider Control</h2>

  </div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);

  // Init End Stops
  pinMode(ENDSTOP_A, INPUT_PULLUP);
  pinMode(ENDSTOP_B, INPUT_PULLUP);

  // Init Motors
  sliderMotor.setMaxSpeed(sliderSpeed);
  sliderMotor.setAcceleration(300);
  panMotor.setMaxSpeed(1000);
  panMotor.setAcceleration(300);

  // Start WiFi config portal
  WiFiManager wm;
  if (!wm.autoConnect("CameraSlider-Setup")) {
    Serial.println("Failed to connect");
    ESP.restart();
  }

  Serial.println("Connected to WiFi");
  Serial.println(WiFi.localIP());

  // Serve UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {

}
