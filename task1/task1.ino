#include <WiFi.h>
#include <PubSubClient.h>

// Define the pins
#define LDR_PIN 33      // LDR 
#define SWITCH_PIN 19    // Switch
#define LED_PIN 4       // LED

// MQTT Client
WiFiClient espClient;
PubSubClient client(espClient);

bool state = false;  
unsigned long lastMsg = 0;
unsigned long attributeCheck = 0;
unsigned long attributeInterval = 60000; // 60 seconds
int thresholdLDR = 50;

// WiFi and MQTT Server Settings
const char* ssid = "Redmi102022";
const char* password = "12345678";
// const char* mqtt_server = "demo.thingsboard.io";
// const char* tb_token = "0fVubgorpIzhrBMcsbgV";
const char* mqtt_server = "thingsboard.srv524038.hstgr.cloud";
const char* tb_token = "YpxyrrfGDxQIJ6mrW8Hp";

void setup() {
  Serial.begin(115200);
  // Initialize GPIO
  pinMode(LDR_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  // Initialize WiFi
  setup_wifi();

  // Initialize MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);
}

void loop() {
  // Reconnect for client
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Read and publish sensor data every 5 seconds
  if (millis() - lastMsg > 5000) {
    lastMsg = millis();
    readAndPublishSensors();
  }

  // Read attribute every 60 seconds
  if (millis() - attributeCheck > attributeInterval) {
    attributeCheck = millis();
    client.subscribe("v1/devices/me/attributes/response/+");
    client.publish("v1/devices/me/attributes/request/1", "{\"sharedKeys\":\"Threshold\"}");
  }

  // Switch state
  state = digitalRead(SWITCH_PIN) == LOW;
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}


void setup_wifi() {
  delay(10);
  Serial.print("Connecting... ");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", tb_token, NULL)) {
      Serial.println("connected");
      client.subscribe("v1/devices/me/attributes");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  if (String(topic).endsWith("attributes/response/1")) {
    int newValue = message.toInt();
    if (newValue > 0 && newValue != thresholdLDR) {
      if (String(topic).indexOf("Threshold") != -1) {
        thresholdLDR = newValue;
      }
    }
  }
}

void readAndPublishSensors() {
  int temperature = random(10,20);
  float humidity = random(15,30);

  String payload = "{";
  payload += "\"temperature\":" + String(temperature) + ",";
  payload += "\"humidity\":" + String(humidity);
  payload += "}";

  // Serial.print("Publishing payload: ");
  Serial.println(payload);

  // Publish payload
  client.publish("v1/devices/me/telemetry", payload.c_str());

  Serial.println(payload);
}