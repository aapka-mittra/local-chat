#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>

#include <string.h>

#include "chat_page.h"

// Change these values before uploading the firmware.
const char WIFI_SSID[] = "LocalChat";
const char WIFI_PASSWORD[] = "ChangeMe1234";

constexpr uint8_t HTTP_PORT = 80;
constexpr uint8_t WEBSOCKET_PORT = 81;
constexpr uint8_t MAX_USERS = 2;
constexpr uint8_t MAX_WIFI_CLIENTS = 8;
constexpr size_t CHAT_HISTORY_SIZE = 50;
constexpr size_t MAX_NAME_LENGTH = 24;
constexpr size_t MAX_MESSAGE_LENGTH = 160;
constexpr size_t JSON_BUFFER_SIZE =
    (MAX_MESSAGE_LENGTH * 2) + (MAX_NAME_LENGTH * 2) + 128;
constexpr size_t USER_LIST_BUFFER_SIZE =
    (MAX_USERS * (MAX_NAME_LENGTH * 2 + 48)) + 64;
constexpr uint32_t INACTIVITY_TIMEOUT_MS = 45000;

struct User {
  bool active;
  uint8_t socketId;
  uint32_t lastSeen;
  char name[MAX_NAME_LENGTH + 1];
};

struct ChatMessage {
  uint32_t id;
  char name[MAX_NAME_LENGTH + 1];
  char text[MAX_MESSAGE_LENGTH + 1];
};

ESP8266WebServer httpServer(HTTP_PORT);
WebSocketsServer webSocket(WEBSOCKET_PORT);
User users[MAX_USERS] = {};
ChatMessage chatHistory[CHAT_HISTORY_SIZE] = {};
size_t historyNext = 0;
size_t historyCount = 0;
uint32_t nextMessageId = 1;

void onWebSocketEvent(uint8_t socketId, WStype_t type, uint8_t *payload,
                      size_t length);

int16_t findUserBySocket(uint8_t socketId) {
  for (uint8_t i = 0; i < MAX_USERS; ++i) {
    if (users[i].active && users[i].socketId == socketId) {
      return static_cast<int16_t>(i);
    }
  }
  return -1;
}

void sendJson(uint8_t socketId, JsonDocument &document) {
  char output[JSON_BUFFER_SIZE];
  const size_t length = serializeJson(document, output, sizeof(output));
  if (length > 0) {
    webSocket.sendTXT(socketId, output, length);
  }
}

void sendError(uint8_t socketId, const char *code, const char *message) {
  StaticJsonDocument<256> document;
  document["type"] = "error";
  document["code"] = code;
  document["message"] = message;
  sendJson(socketId, document);
}

void broadcastUserList() {
  StaticJsonDocument<USER_LIST_BUFFER_SIZE> document;
  document["type"] = "users";
  document["maxUsers"] = MAX_USERS;
  JsonArray onlineUsers = document.createNestedArray("users");

  for (uint8_t i = 0; i < MAX_USERS; ++i) {
    if (!users[i].active) {
      continue;
    }
    JsonObject user = onlineUsers.createNestedObject();
    user["id"] = users[i].socketId;
    user["name"] = users[i].name;
  }

  char output[USER_LIST_BUFFER_SIZE];
  const size_t length = serializeJson(document, output, sizeof(output));
  if (length > 0) {
    webSocket.broadcastTXT(reinterpret_cast<uint8_t *>(output), length);
  }
}

void sendChatMessage(uint8_t socketId, const ChatMessage &message,
                     bool fromHistory) {
  StaticJsonDocument<JSON_BUFFER_SIZE> document;
  document["type"] = "message";
  document["id"] = message.id;
  document["name"] = message.name;
  document["text"] = message.text;
  if (fromHistory) {
    document["history"] = true;
  }
  sendJson(socketId, document);
}

void sendHistory(uint8_t socketId) {
  const size_t oldest =
      (historyNext + CHAT_HISTORY_SIZE - historyCount) % CHAT_HISTORY_SIZE;
  for (size_t i = 0; i < historyCount; ++i) {
    const size_t index = (oldest + i) % CHAT_HISTORY_SIZE;
    sendChatMessage(socketId, chatHistory[index], true);
  }
}

void broadcastChatMessage(const ChatMessage &message) {
  for (uint8_t i = 0; i < MAX_USERS; ++i) {
    if (users[i].active) {
      sendChatMessage(users[i].socketId, message, false);
    }
  }
}

void storeChatMessage(const char *name, const char *text) {
  ChatMessage &message = chatHistory[historyNext];
  message.id = nextMessageId++;
  snprintf(message.name, sizeof(message.name), "%s", name);
  snprintf(message.text, sizeof(message.text), "%s", text);

  historyNext = (historyNext + 1) % CHAT_HISTORY_SIZE;
  if (historyCount < CHAT_HISTORY_SIZE) {
    ++historyCount;
  }
  broadcastChatMessage(message);
}

bool hasVisibleCharacters(const char *value) {
  bool visible = false;
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(value);
       *cursor != '\0'; ++cursor) {
    if (*cursor < ' ' || *cursor == 0x7f) {
      return false;
    }
    if (*cursor > ' ') {
      visible = true;
    }
  }
  return visible;
}

void handleJoin(uint8_t socketId, JsonDocument &document) {
  const char *name = document["name"].as<const char *>();
  if (name == nullptr) {
    sendError(socketId, "invalid_name", "Choose a valid nickname.");
    return;
  }
  const size_t nameLength = strlen(name);
  if (nameLength == 0 || nameLength > MAX_NAME_LENGTH ||
      !hasVisibleCharacters(name)) {
    sendError(socketId, "invalid_name", "Choose a valid nickname.");
    return;
  }

  if (findUserBySocket(socketId) >= 0) {
    return;
  }

  int16_t freeSlot = -1;
  for (uint8_t i = 0; i < MAX_USERS; ++i) {
    if (!users[i].active) {
      freeSlot = static_cast<int16_t>(i);
      break;
    }
  }
  if (freeSlot < 0) {
    sendError(socketId, "full", "Chat is full. Wait for someone to leave.");
    return;
  }

  User &user = users[freeSlot];
  user.active = true;
  user.socketId = socketId;
  user.lastSeen = millis();
  snprintf(user.name, sizeof(user.name), "%s", name);

  StaticJsonDocument<128> joined;
  joined["type"] = "joined";
  joined["name"] = user.name;
  joined["maxNameLength"] = MAX_NAME_LENGTH;
  joined["maxMessageLength"] = MAX_MESSAGE_LENGTH;
  sendJson(socketId, joined);
  broadcastUserList();
  sendHistory(socketId);
}

void handleTextMessage(uint8_t socketId, JsonDocument &document) {
  const int16_t userIndex = findUserBySocket(socketId);
  if (userIndex < 0) {
    sendError(socketId, "not_joined", "Choose a nickname before chatting.");
    return;
  }

  const char *text = document["text"].as<const char *>();
  if (text == nullptr) {
    sendError(socketId, "invalid_message",
              "Messages must be non-empty and within the configured limit.");
    return;
  }
  const size_t textLength = strlen(text);
  if (textLength == 0 || textLength > MAX_MESSAGE_LENGTH ||
      !hasVisibleCharacters(text)) {
    sendError(socketId, "invalid_message",
              "Messages must be non-empty and within the configured limit.");
    return;
  }

  User &user = users[userIndex];
  user.lastSeen = millis();
  storeChatMessage(user.name, text);
}

void removeUserBySocket(uint8_t socketId) {
  const int16_t userIndex = findUserBySocket(socketId);
  if (userIndex < 0) {
    return;
  }
  users[userIndex] = {};
  broadcastUserList();
}

void expireInactiveUsers() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_USERS; ++i) {
    if (users[i].active &&
        static_cast<uint32_t>(now - users[i].lastSeen) >=
            INACTIVITY_TIMEOUT_MS) {
      const uint8_t socketId = users[i].socketId;
      users[i] = {};
      webSocket.disconnect(socketId);
      broadcastUserList();
    }
  }
}

void onWebSocketEvent(uint8_t socketId, WStype_t type, uint8_t *payload,
                      size_t length) {
  if (type == WStype_DISCONNECTED) {
    removeUserBySocket(socketId);
    return;
  }
  if (type != WStype_TEXT) {
    return;
  }
  if (length == 0 || length > JSON_BUFFER_SIZE) {
    sendError(socketId, "invalid_request", "Request is too large.");
    return;
  }

  StaticJsonDocument<JSON_BUFFER_SIZE> document;
  const DeserializationError error =
      deserializeJson(document, payload, length);
  if (error) {
    sendError(socketId, "invalid_request", "Request must be valid JSON.");
    return;
  }

  const char *requestType = document["type"].as<const char *>();
  if (requestType == nullptr) {
    sendError(socketId, "invalid_request", "Request type must be a string.");
    return;
  }
  if (strcmp(requestType, "join") == 0) {
    handleJoin(socketId, document);
    return;
  }

  const int16_t userIndex = findUserBySocket(socketId);
  if (userIndex < 0) {
    sendError(socketId, "not_joined", "Choose a nickname before chatting.");
    return;
  }
  users[userIndex].lastSeen = millis();

  if (strcmp(requestType, "heartbeat") == 0) {
    return;
  }
  if (strcmp(requestType, "message") == 0) {
    handleTextMessage(socketId, document);
    return;
  }
  sendError(socketId, "unknown_request", "Unsupported request.");
}

void handleRoot() {
  httpServer.sendHeader("Cache-Control", "no-store, max-age=0");
  httpServer.send_P(200, "text/html; charset=utf-8", CHAT_PAGE);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Starting LocalChat access point..."));

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 1, false, MAX_WIFI_CLIENTS)) {
    Serial.println(F("Could not start the access point."));
    while (true) {
      delay(1000);
    }
  }

  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.onNotFound([]() {
    httpServer.send(404, "text/plain; charset=utf-8", "Not found");
  });
  httpServer.begin();

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  Serial.print(F("Wi-Fi name: "));
  Serial.println(WIFI_SSID);
  Serial.print(F("Chat page: http://"));
  Serial.println(WiFi.softAPIP());
  Serial.println(F("Chat state is temporary and stored in RAM only."));
}

void loop() {
  httpServer.handleClient();
  webSocket.loop();
  expireInactiveUsers();
}
