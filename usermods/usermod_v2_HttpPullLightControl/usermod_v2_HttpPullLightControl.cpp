#include "usermod_v2_HttpPullLightControl.h"

// add more strings here to reduce flash memory usage
const char HttpPullLightControl::_name[]    PROGMEM = "HttpPullLightControl";
const char HttpPullLightControl::_enabled[] PROGMEM = "Enable";

void HttpPullLightControl::setup() {
  //Serial.begin(115200);

  // Print version number
  DEBUG_PRINT(F("HttpPullLightControl version: "));
  DEBUG_PRINTLN(HTTP_PULL_LIGHT_CONTROL_VERSION);

  // Start a nice chase so we know its booting and searching for its first http pull.
  DEBUG_PRINTLN(F("Starting a nice chase so we now it is booting."));
  Segment& seg = strip.getMainSegment();
  seg.setMode(28); // Set to chase
  seg.speed = 200;
  seg.intensity = 255;
  seg.setPalette(128);
  seg.setColor(0, 5263440);
  seg.setColor(1, 0);
  seg.setColor(2, 4605510);

  // Go on with generating a unique ID and splitting the URL into parts
  uniqueId = generateUniqueId();  // Cache the unique ID
  parseUrl();
  DEBUG_PRINTLN(F("HttpPullLightControl successfully setup"));
}

// This is the main loop function, from here we check the URL and handle the response.
// Effects or individual lights are set as a result from this.
void HttpPullLightControl::loop() {
  if (!enabled || offMode) return; // Do nothing when not enabled or powered off
  if (millis() - lastCheck >= checkInterval * 1000) {
    DEBUG_PRINTLN(F("Calling checkUrl function"));
    checkUrl();
    lastCheck = millis();
  }

}

// Generate a unique ID based on the MAC address and a SALT
String HttpPullLightControl::generateUniqueId() {
  // We use an easy to implement Fowler–Noll–Vo hash function so we dont need any Sha1.h or Crypto.h dependencies
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  String input = String(macStr) + salt;
  unsigned long hashValue = FNV_offset_basis;
  for (char c : input) {
    hashValue *= FNV_prime;
    hashValue ^= c;
  }
  DEBUG_PRINT(F("Unique ID generated: "));
  DEBUG_PRINTLN(hashValue);
  return String(hashValue);
}

// This function is called when the user updates the Sald and so we need to re-calculate the unique ID
void HttpPullLightControl::updateSalt(String newSalt) {
  DEBUG_PRINTLN(F("Salt updated"));
  this->salt = newSalt;
  uniqueId = generateUniqueId();
}

// The function is used to separate the URL in a host part and a path part
void HttpPullLightControl::parseUrl() {
  int firstSlash = url.indexOf('/', 7);  // Skip http(s)://
  host = url.substring(7, firstSlash);
  path = url.substring(firstSlash);
}

// This function is called by WLED when the USERMOD config is read
bool HttpPullLightControl::readFromConfig(JsonObject& root) {
  // Attempt to retrieve the nested object for this usermod
  JsonObject top = root[FPSTR(_name)];
  bool configComplete = !top.isNull();  // check if the object exists

  // Retrieve the values using the getJsonValue function for better error handling
  configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled, enabled);  // default value=enabled
  configComplete &= getJsonValue(top["checkInterval"], checkInterval, checkInterval);  // default value=60
  #ifndef HTTP_PULL_LIGHT_CONTROL_HIDE_URL
    configComplete &= getJsonValue(top["url"], url, url);  // default value="http://example.com"
  #endif
  #ifndef HTTP_PULL_LIGHT_CONTROL_HIDE_SALT
    configComplete &= getJsonValue(top["salt"], salt, salt);  // default value=your_salt_here
  #endif

  return configComplete;
}

// This function is called by WLED when the USERMOD config is saved in the frontend
void HttpPullLightControl::addToConfig(JsonObject& root) {
  // Create a nested object for this usermod
  JsonObject top = root.createNestedObject(FPSTR(_name));

  // Write the configuration parameters to the nested object
  top[FPSTR(_enabled)] = enabled;
  top["checkInterval"] = checkInterval;
  #ifndef HTTP_PULL_LIGHT_CONTROL_HIDE_URL
    top["url"] = url;
  #endif
  #ifndef HTTP_PULL_LIGHT_CONTROL_HIDE_SALT
    top["salt"] = salt;
    updateSalt(salt);  // Update the UniqueID
  #endif
  parseUrl();  // Re-parse the URL, maybe path and host is changed
}

// Do the http request here. Note that we can not do https requests with the AsyncTCP library
// We do everything Asynchronous, so all callbacks are defined here
void HttpPullLightControl::checkUrl() {
  if (client != nullptr && client->connected()) {
      DEBUG_PRINTLN(F("We are still connected, do nothing"));
      // Do nothing, Client is still connected
      return;
  }
  if (client != nullptr) {
    // Delete previous client instance if exists, just to prevent any memory leaks
    DEBUG_PRINTLN(F("Delete previous instances"));
    delete client;
    client = nullptr;
  }

  DEBUG_PRINTLN(F("Creating new AsyncClient instance."));
  client = new AsyncClient();
  if(client) {
    client->onData([](void *arg, AsyncClient *c, void *data, size_t len) {
      DEBUG_PRINTLN(F("Data received."));
      // Cast arg back to the usermod class instance
      HttpPullLightControl *instance = (HttpPullLightControl *)arg;
      // Convertert to Safe-String
      char *strData = new char[len + 1];
      strncpy(strData, (char*)data, len);
      strData[len] = '\0';
      String responseData = String(strData);
      //String responseData = String((char *)data);
      // Make sure its zero-terminated String
      //responseData[len] = '\0';
      delete[] strData; // Do not forget to remove this one
      instance->handleResponse(responseData);
    }, this);
    client->onDisconnect([](void *arg, AsyncClient *c) {
      DEBUG_PRINTLN(F("Disconnected."));
      //Set the class-own client pointer to nullptr if its the current client
      HttpPullLightControl *instance = static_cast<HttpPullLightControl*>(arg);
      if (instance->client == c) {
        instance->client = nullptr;
      }
      // Do not remove client here, it is maintained by AsyncClient
    }, this);
    client->onTimeout([](void *arg, AsyncClient *c, uint32_t time) {
      DEBUG_PRINTLN(F("Timeout"));
      //Set the class-own client pointer to nullptr if its the current client
      HttpPullLightControl *instance = static_cast<HttpPullLightControl*>(arg);
      if (instance->client == c) {
        delete instance->client;
        instance->client = nullptr;
      }
      // Do not remove client here, it is maintained by AsyncClient
    }, this);
    client->onError([](void *arg, AsyncClient *c, int8_t error) {
      DEBUG_PRINTLN("Connection error occurred!");
      DEBUG_PRINT("Error code: ");
      DEBUG_PRINTLN(error);
      //Set the class-own client pointer to nullptr if its the current client
      HttpPullLightControl *instance = static_cast<HttpPullLightControl*>(arg);
      if (instance->client == c) {
        delete instance->client;
        instance->client = nullptr;
      }
      // Do not remove client here, it is maintained by AsyncClient
    }, this);
    client->onConnect([](void *arg, AsyncClient *c) {
      // Cast arg back to the usermod class instance
      HttpPullLightControl *instance = (HttpPullLightControl *)arg;
      instance->onClientConnect(c);  // Call a method on the instance when the client connects
    }, this);
    client->setAckTimeout(ackTimeout); // Just some safety measures because we do not want any memory fillup
    client->setRxTimeout(rxTimeout);
    DEBUG_PRINT(F("Connecting to: "));
    DEBUG_PRINT(host);
    DEBUG_PRINT(F(" via port "));
    DEBUG_PRINTLN((url.startsWith("https")) ? 443 : 80);
    //Try to connect
    if (!client->connect(host.c_str(), (url.startsWith("https")) ? 443 : 80)) {
      DEBUG_PRINTLN(F("Failed to initiate connection."));
      // Connection failed, so cleanup
      delete client;
      client = nullptr;
    } else {
      // Connection successfull, wait for callbacks to go on.
      DEBUG_PRINTLN(F("Connection initiated, awaiting response..."));
    }
  } else {
    DEBUG_PRINTLN(F("Failed to create AsyncClient instance."));
  }
}

// This function is called from the checkUrl function when the connection is establised
// We request the data here
void HttpPullLightControl::onClientConnect(AsyncClient *c) {
  DEBUG_PRINT(F("Client connected: "));
  DEBUG_PRINTLN(c->connected() ? F("Yes") : F("No"));

  if (c->connected()) {
    String request = "GET " + path + (path.indexOf('?') > 0 ? "&id=" : "?id=") + uniqueId + " HTTP/1.1\r\n"
                    "Host: " + host + "\r\n"
                    "Connection: close\r\n"
                    "Accept: application/json\r\n"
                    "Accept-Encoding: identity\r\n" // No compression
                    "User-Agent: ESP32 HTTP Client\r\n\r\n"; // Optional: User-Agent and end with a double rnrn !
    DEBUG_PRINT(request.c_str());
    auto bytesSent  = c->write(request.c_str());
    if (bytesSent  == 0) {
      // Connection could not be made
      DEBUG_PRINT(F("Failed to send HTTP request."));
    } else {
      DEBUG_PRINT(F("Request sent successfully, bytes sent: "));
      DEBUG_PRINTLN(bytesSent );
    }
  }
}


// This function is called when we receive data after connecting and doing our request
// It parses the JSON data to WLED
void HttpPullLightControl::handleResponse(String& responseStr) {
  DEBUG_PRINTLN(F("Received response for handleResponse."));

  // Get a Bufferlock, we can not use doc
  if (!requestJSONBufferLock(myLockId)) {
    DEBUG_PRINT(F("ERROR: Can not request JSON Buffer Lock, number: "));
      DEBUG_PRINTLN(myLockId);
    return;
  }

  // Search for two linebreaks between headers and content
  int bodyPos = responseStr.indexOf("\r\n\r\n");
  if (bodyPos > 0) {
    String jsonStr = responseStr.substring(bodyPos + 4); // +4 Skip the two CRLFs
    jsonStr.trim();

    DEBUG_PRINTLN("Response: ");
    DEBUG_PRINTLN(jsonStr);

    // Attempt to deserialize the JSON response
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
      // If there is an error in deserialization, exit the function
      DEBUG_PRINT(F("DeserializationError: "));
      DEBUG_PRINTLN(error.c_str());
      return;
    }
  } else {
    DEBUG_PRINTLN(F("No body found in the response"));
    return;
  }

  // Get JSON object from th doc
  JsonObject obj = doc.as<JsonObject>();

  // Parse the object throuhg deserializeState  (use CALL_MODE_NO_NOTIFY or OR CALL_MODE_DIRECT_CHANGE)
  deserializeState(obj, CALL_MODE_NO_NOTIFY);

  // Release the BufferLock again
  releaseJSONBufferLock();
}