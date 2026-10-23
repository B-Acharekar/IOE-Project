#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>

// Install: Firebase Arduino Client Library for ESP8266 and ESP32
// Fill these values before uploading.
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY "AIzaSyAra3hPq88iND1GboObujr3g8jLSpwvk4M"
#define DATABASE_URL "https://ecobot-b2ad3-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DEVICE_EMAIL "ecobot-device@ecobot-b2ad3.firebaseapp.com"
#define DEVICE_PASSWORD "CHANGE_THIS_DEVICE_PASSWORD"

#define IR_LEFT 34
#define IR_RIGHT 35
#define BIN_IR 4
#define OBSTACLE LOW
#define TRIG_FRONT 32
#define ECHO_FRONT 33
#define FRONT_OBSTACLE_DISTANCE 20.0
#define TRIG_GARBAGE 15
#define ECHO_GARBAGE 2
#define GARBAGE_DISTANCE 12.0
#define GARBAGE_RESET_DISTANCE 18.0
#define SERVO_PIN 16
#define SCOOP_HOME 20
#define SCOOP_THROW 135

#define L_AIN1 25
#define L_AIN2 26
#define L_PWMA 27
#define L_BIN1 14
#define L_BIN2 12
#define L_PWMB 13
#define R_AIN1 18
#define R_AIN2 19
#define R_PWMA 21
#define R_BIN1 22
#define R_BIN2 23
#define R_PWMB 5

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Servo scoopServo;
bool collected = false;
unsigned long lastPublish = 0;
String robotStatus = "BOOTING";

void setMotor(int in1, int in2, int pwm, int direction) {
  digitalWrite(in1, direction == 1 ? HIGH : LOW);
  digitalWrite(in2, direction == -1 ? HIGH : LOW);
  digitalWrite(pwm, direction == 0 ? LOW : HIGH);
}
void leftSide(int d) { setMotor(L_AIN1,L_AIN2,L_PWMA,d); setMotor(L_BIN1,L_BIN2,L_PWMB,d); }
void rightSide(int d) { d = -d; setMotor(R_AIN1,R_AIN2,R_PWMA,d); setMotor(R_BIN1,R_BIN2,R_PWMB,d); }
void stopRobot() { leftSide(0); rightSide(0); }
void forward() { leftSide(1); rightSide(1); }
void backward() { leftSide(-1); rightSide(-1); }
void turnLeft() { leftSide(-1); rightSide(1); }
void turnRight() { leftSide(1); rightSide(-1); }

float getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2); digitalWrite(trigPin, HIGH); delayMicroseconds(10); digitalWrite(trigPin, LOW);
  unsigned long duration = pulseIn(echoPin, HIGH, 30000);
  return duration == 0 ? 999.0 : duration * 0.0343 / 2.0;
}
void collectGarbage() { stopRobot(); scoopServo.write(SCOOP_THROW); delay(1400); scoopServo.write(SCOOP_HOME); delay(1000); robotStatus = "COLLECTION COMPLETE"; }
void avoidFrontObstacle() { stopRobot(); delay(200); backward(); delay(550); stopRobot(); delay(150); turnRight(); delay(800); stopRobot(); }
void avoidLeftObstacle() { stopRobot(); delay(150); backward(); delay(350); stopRobot(); delay(100); turnRight(); delay(600); stopRobot(); }
void avoidRightObstacle() { stopRobot(); delay(150); backward(); delay(350); stopRobot(); delay(100); turnLeft(); delay(600); stopRobot(); }

void publishTelemetry(int leftIR, int rightIR, int binSensor, float frontDistance, float garbageDistance, bool moving) {
  if (!Firebase.ready() || millis() - lastPublish < 1200) return;
  lastPublish = millis();
  FirebaseJson json;
  json.set("online", true);
  json.set("moving", moving);
  json.set("binFull", binSensor == LOW);
  json.set("binLevel", binSensor == LOW ? 100 : 0);
  json.set("battery", 100);
  json.set("frontDistance", frontDistance);
  json.set("garbageDistance", garbageDistance);
  json.set("leftObstacle", leftIR == OBSTACLE);
  json.set("rightObstacle", rightIR == OBSTACLE);
  json.set("collected", collected);
  json.set("status", robotStatus);
  json.set("lastSeen", (int) (millis() / 1000));
  if (!Firebase.RTDB.setJSON(&fbdo, "/ecobot", &json)) Serial.println(fbdo.errorReason());
}

void setup() {
  Serial.begin(115200);
  pinMode(IR_LEFT, INPUT); pinMode(IR_RIGHT, INPUT); pinMode(BIN_IR, INPUT);
  pinMode(TRIG_FRONT, OUTPUT); pinMode(ECHO_FRONT, INPUT); pinMode(TRIG_GARBAGE, OUTPUT); pinMode(ECHO_GARBAGE, INPUT);
  int motorPins[] = {L_AIN1,L_AIN2,L_PWMA,L_BIN1,L_BIN2,L_PWMB,R_AIN1,R_AIN2,R_PWMA,R_BIN1,R_BIN2,R_PWMB};
  for (int pin : motorPins) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
  scoopServo.setPeriodHertz(50); scoopServo.attach(SERVO_PIN, 500, 2400); scoopServo.write(SCOOP_HOME); stopRobot();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  config.api_key = API_KEY; config.database_url = DATABASE_URL;
  auth.user.email = DEVICE_EMAIL; auth.user.password = DEVICE_PASSWORD;
  Firebase.begin(&config, &auth); Firebase.reconnectWiFi(true);
  robotStatus = "CLEAR -> FORWARD";
}

void loop() {
  int leftIR = digitalRead(IR_LEFT), rightIR = digitalRead(IR_RIGHT), binSensor = digitalRead(BIN_IR);
  float garbageDistance = getDistance(TRIG_GARBAGE, ECHO_GARBAGE); delay(30); float frontDistance = getDistance(TRIG_FRONT, ECHO_FRONT);
  bool moving = false;
  if (binSensor == LOW) { stopRobot(); robotStatus = "BIN FULL -> ROBOT STOPPED"; }
  else if (garbageDistance <= GARBAGE_DISTANCE && garbageDistance > 2 && !collected) { robotStatus = "GARBAGE DETECTED"; collectGarbage(); collected = true; }
  else if (garbageDistance > GARBAGE_RESET_DISTANCE) { collected = false; if (leftIR == OBSTACLE && rightIR == OBSTACLE) { robotStatus = "BOTH IR BLOCKED"; avoidFrontObstacle(); } else if (frontDistance <= FRONT_OBSTACLE_DISTANCE && frontDistance > 2) { robotStatus = "FRONT ULTRASONIC BLOCKED"; avoidFrontObstacle(); } else if (leftIR == OBSTACLE) { robotStatus = "LEFT IR BLOCKED"; avoidLeftObstacle(); } else if (rightIR == OBSTACLE) { robotStatus = "RIGHT IR BLOCKED"; avoidRightObstacle(); } else { robotStatus = "CLEAR -> FORWARD"; forward(); moving = true; } }
  publishTelemetry(leftIR, rightIR, binSensor, frontDistance, garbageDistance, moving);
  delay(50);
}
