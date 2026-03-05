#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

BluetoothSerial SerialBT;
String ssid;
String password;
String teamId;

// Struct to hold character data
struct Character {
  int id;
  String name;
  String image;
  String teamId;
};

// Function declarations
void fetchAndProcessData(void * parameter);
void fetchDetails(void * parameter);
void connectToWiFi();
void getNetworks();
bool fetchData(const String& url, DynamicJsonDocument& doc);
bool fetchDetailsData(const String& url, DynamicJsonDocument& doc);

void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_BT_Device");
  Serial.println("Bluetooth Device is Ready to Pair");
}

void loop() {
  if (SerialBT.available()) {
    String request = SerialBT.readStringUntil('\n');
    Serial.print("Received request: ");
    Serial.println(request);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, request);
    const char* action = doc["action"];
    
    if (strcmp(action, "connect") == 0) {
      Serial.println("Action: connect");
      ssid = doc["ssid"].as<String>();
      password = doc["password"].as<String>();
      connectToWiFi();
    } else if (strcmp(action, "getNetworks") == 0) {
      Serial.println("Action: getNetworks");
      teamId = doc["teamId"].as<String>();
      getNetworks();
    } else if (strcmp(action, "getData") == 0) {
      Serial.println("Action: getData");
      xTaskCreate(fetchAndProcessData, "fetchAndProcessData", 8192, NULL, 1, NULL);
    } else if (strcmp(action, "getDetails") == 0) {
      Serial.println("Action: getDetails");
      int id = doc["id"];
      Serial.print("Fetching details for ID: ");
      Serial.println(id);
      xTaskCreate(fetchDetails, "fetchDetails", 8192, (void*)id, 1, NULL);
    }
  }
  delay(100); // Small delay to avoid busy loop
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  DynamicJsonDocument wifiResponse(256);
  wifiResponse["ssid"] = ssid;
  wifiResponse["connected"] = (WiFi.status() == WL_CONNECTED);
  wifiResponse["teamId"] = teamId;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi.");
  }

  // Send WiFi connection response
  String wifiResponseStr;
  serializeJson(wifiResponse, wifiResponseStr);
  SerialBT.println(wifiResponseStr);
  Serial.print("Sent WiFi connection response: ");
  Serial.println(wifiResponseStr);
}

void fetchAndProcessData(void * parameter) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Fetching data...");
    DynamicJsonDocument doc(16384); // Increased size for larger data
    if (fetchData("http://proiectia.bogdanflorea.ro/api/marvel-api/characters", doc)) {
      JsonArray characters = doc.as<JsonArray>();

      std::vector<Character> characterList;

      // Process all character data
      for (JsonObject character : characters) {
        Character c;
        c.id = character["id"];
        c.name = character["name"].as<String>();
        c.image = character["thumbnail"].as<String>();
        c.teamId = teamId;
        characterList.push_back(c);
      }

      // Send all character data after processing
      for (Character& c : characterList) {
        // Send the required JSON response for getData
        DynamicJsonDocument getDataResponse(1024);
        getDataResponse["id"] = c.id;
        getDataResponse["name"] = c.name;
        getDataResponse["image"] = c.image;
        getDataResponse["teamId"] = c.teamId.c_str();
        
        String getDataResponseStr;
        serializeJson(getDataResponse, getDataResponseStr);
        SerialBT.println(getDataResponseStr);
        Serial.print("Sent getData response: ");
        Serial.println(getDataResponseStr);

        // Add delay between sending each character data
        delay(500); // Adjust the delay as needed
      }
    } else {
      Serial.println("Failed to fetch or parse data after retries.");
    }
  } else {
    Serial.println("WiFi not connected or lost connection.");
  }
  vTaskDelete(NULL); // Delete this task when done
}

bool fetchData(const String& url, DynamicJsonDocument& doc) {
  HTTPClient http;
  int httpCode;
  while (true) { // Infinite loop to keep retrying until successful
    http.begin(url);
    http.setTimeout(15000);
    httpCode = http.GET();
    if (httpCode > 0) {
      String response = http.getString();
      deserializeJson(doc, response);
      http.end();
      return true;
    } else {
      Serial.print("Failed with HTTP code: ");
      Serial.println(httpCode);
      http.end();
      delay(1000); // Wait 1 second before retrying
    }
  }
}

void fetchDetails(void * parameter) {
  int id = (int)parameter;
  String url = "http://proiectia.bogdanflorea.ro/api/marvel-api/character?id=" + String(id);
  DynamicJsonDocument doc(4096);
  
  if (fetchDetailsData(url, doc)) {
    int id = doc["id"];
    const char* name = doc["name"];
    const char* image = doc["thumbnail"];
    const char* description = doc["description"];
    const char* teamId_cstr = teamId.c_str();  // Convert the stored teamId to const char*

    // Send the required JSON response for getDetails
    DynamicJsonDocument getDetailsResponse(1024);
    getDetailsResponse["id"] = id;
    getDetailsResponse["name"] = name;
    getDetailsResponse["image"] = image;
    getDetailsResponse["description"] = description;
    getDetailsResponse["teamId"] = teamId_cstr;
    
    String getDetailsResponseStr;
    serializeJson(getDetailsResponse, getDetailsResponseStr);
    SerialBT.println(getDetailsResponseStr);
    Serial.print("Sent getDetails response: ");
    Serial.println(getDetailsResponseStr);
  } else {
    String error = "{\"error\": \"HTTP GET failed after retries\"}";
    Serial.println(error);
    SerialBT.println(error); // Send error details over Bluetooth in JSON format
  }
  
  vTaskDelete(NULL); // Delete this task when done
}

bool fetchDetailsData(const String& url, DynamicJsonDocument& doc) {
  HTTPClient http;
  int httpCode;
  while (true) { // Infinite loop to keep retrying until successful
    http.begin(url);
    http.setTimeout(15000);
    httpCode = http.GET();
    if (httpCode > 0) {
      String response = http.getString();
      deserializeJson(doc, response);
      http.end();
      return true;
    } else {
      Serial.print("Failed with HTTP code: ");
      Serial.println(httpCode);
      http.end();
      delay(1000); // Wait 1 second before retrying
    }
  }
}

void getNetworks() {
  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == -1) {
    Serial.println("No networks found");
    return;
  }

  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int32_t strength = WiFi.RSSI(i);
    String encryptionType;
    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN: encryptionType = "OPEN"; break;
      case WIFI_AUTH_WEP: encryptionType = "WEP"; break;
      case WIFI_AUTH_WPA_PSK: encryptionType = "WPA_PSK"; break;
      case WIFI_AUTH_WPA2_PSK: encryptionType = "WPA2_PSK"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: encryptionType = "WPA_WPA2_PSK"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: encryptionType = "WPA2_ENTERPRISE"; break;
      default: encryptionType = "UNKNOWN"; break;
    }

    // Send the required JSON response for each network
    DynamicJsonDocument networkResponse(256);
    networkResponse["ssid"] = ssid;
    networkResponse["strength"] = strength;
    networkResponse["encryption"] = encryptionType;
    networkResponse["teamId"] = teamId.c_str();

    String networkResponseStr;
    serializeJson(networkResponse, networkResponseStr);
    SerialBT.println(networkResponseStr);
    Serial.print("Sent network response: ");
    Serial.println(networkResponseStr);
  }
}
