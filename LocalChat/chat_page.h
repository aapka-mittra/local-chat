#ifndef LOCALCHAT_CHAT_PAGE_H
#define LOCALCHAT_CHAT_PAGE_H

#include <pgmspace.h>

const char CHAT_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#173b36">
  <title>LocalChat</title>
  <style>
    :root { color-scheme: light; font-family: system-ui, sans-serif; background: #f3f6f4; color: #172521; }
    * { box-sizing: border-box; }
    body { margin: 0; min-height: 100vh; display: grid; place-items: center; padding: 16px; }
    main { width: min(100%, 560px); height: min(760px, calc(100vh - 32px)); min-height: 420px; display: flex; flex-direction: column; background: #fff; border: 1px solid #d7e0dc; border-radius: 14px; overflow: hidden; box-shadow: 0 8px 28px #173b3615; }
    header { padding: 16px 18px; background: #173b36; color: #fff; }
    h1 { margin: 0; font-size: 1.2rem; }
    #connection { margin-top: 4px; color: #d6e7df; font-size: .86rem; }
    .online { padding: 12px 18px; border-bottom: 1px solid #e4eae7; }
    .online h2 { display: inline; margin: 0 8px 0 0; font-size: .9rem; }
    #users { display: inline; margin: 0; padding: 0; list-style: none; font-size: .9rem; }
    #users li { display: inline; }
    #users li + li::before { content: ", "; }
    #join-panel { padding: 18px; background: #f7faf8; border-bottom: 1px solid #e4eae7; }
    #join-panel p { margin: 0 0 12px; font-size: .9rem; }
    form { display: flex; gap: 8px; }
    input, button { min-height: 44px; border-radius: 8px; font: inherit; }
    input { min-width: 0; flex: 1; border: 1px solid #b9c9c2; padding: 0 12px; }
    button { border: 0; padding: 0 16px; background: #176b54; color: white; font-weight: 650; cursor: pointer; }
    button:disabled { background: #9baba5; cursor: not-allowed; }
    #notice { min-height: 1.25em; margin-top: 10px !important; color: #8a301d; }
    #messages { flex: 1; overflow-y: auto; padding: 16px 18px; margin: 0; list-style: none; }
    #messages li { margin: 0 0 12px; overflow-wrap: anywhere; }
    #messages strong { color: #176b54; }
    #empty { color: #687770; text-align: center; padding-top: 32px; }
    #message-form { padding: 12px; border-top: 1px solid #e4eae7; }
    @media (max-width: 420px) { body { padding: 0; } main { height: 100vh; min-height: 0; border: 0; border-radius: 0; } }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>LocalChat</h1>
      <div id="connection" role="status">Connect to the LocalChat Wi-Fi to begin.</div>
    </header>
    <section class="online" aria-label="Online users">
      <h2>Online</h2>
      <ul id="users"><li>None yet</li></ul>
    </section>
    <section id="join-panel">
      <p>Choose a nickname to join this temporary local chat.</p>
      <form id="join-form">
        <input id="nickname" autocomplete="nickname" placeholder="Your nickname" required>
        <button type="submit">Join</button>
      </form>
      <p id="notice" aria-live="polite"></p>
    </section>
    <ol id="messages" aria-live="polite"><li id="empty">Recent messages will appear here.</li></ol>
    <form id="message-form">
      <input id="message-input" autocomplete="off" placeholder="Write a message" aria-label="Message" disabled required>
      <button id="send-button" type="submit" disabled>Send</button>
    </form>
  </main>
  <script>
    (() => {
      "use strict";
      const MAX_VISIBLE_MESSAGES = 50;
      const MAX_NAME_LENGTH = 24;
      const MAX_MESSAGE_LENGTH = 160;
      const connectionLabel = document.getElementById("connection");
      const joinPanel = document.getElementById("join-panel");
      const joinForm = document.getElementById("join-form");
      const nicknameInput = document.getElementById("nickname");
      const notice = document.getElementById("notice");
      const usersList = document.getElementById("users");
      const messages = document.getElementById("messages");
      const messageForm = document.getElementById("message-form");
      const messageInput = document.getElementById("message-input");
      const sendButton = document.getElementById("send-button");
      let socket = null;
      let joined = false;

      function renderMessage(data) {
        const empty = document.getElementById("empty");
        if (empty) empty.remove();
        const item = document.createElement("li");
        const author = document.createElement("strong");
        author.textContent = `${data.name}: `;
        item.append(author, document.createTextNode(data.text));
        messages.append(item);
        while (messages.children.length > MAX_VISIBLE_MESSAGES) {
          messages.firstElementChild.remove();
        }
        messages.scrollTop = messages.scrollHeight;
      }

      function renderUsers(users) {
        usersList.replaceChildren();
        if (!users.length) {
          const item = document.createElement("li");
          item.textContent = "None yet";
          usersList.append(item);
          return;
        }
        for (const user of users) {
          const item = document.createElement("li");
          item.textContent = user.name;
          usersList.append(item);
        }
      }

      function handleServerMessage(data) {
        if (data.type === "joined") {
          joined = true;
          joinPanel.hidden = true;
          nicknameInput.maxLength = data.maxNameLength;
          messageInput.maxLength = data.maxMessageLength;
          messageInput.disabled = false;
          sendButton.disabled = false;
          connectionLabel.textContent = `Connected as ${data.name}`;
          messageInput.focus();
        } else if (data.type === "users") {
          renderUsers(data.users);
        } else if (data.type === "message") {
          renderMessage(data);
        } else if (data.type === "error") {
          notice.textContent = data.message;
          if (data.code === "full" || data.code === "invalid_name") {
            joined = false;
            joinPanel.hidden = false;
            messageInput.disabled = true;
            sendButton.disabled = true;
            connectionLabel.textContent = data.code === "full"
              ? "The chat is full. Try again when someone leaves."
              : "Choose a valid nickname to continue.";
          }
        }
      }

      function sendJoin() {
        const name = nicknameInput.value.trim();
        if (!name || !socket || socket.readyState !== WebSocket.OPEN) return;
        socket.send(JSON.stringify({ type: "join", name }));
        connectionLabel.textContent = "Requesting a chat slot…";
        notice.textContent = "";
      }

      joinForm.addEventListener("submit", (event) => {
        event.preventDefault();
        const name = nicknameInput.value.trim();
        if (!name || name.length > MAX_NAME_LENGTH) {
          nicknameInput.focus();
          notice.textContent = `Use a nickname of 1 to ${MAX_NAME_LENGTH} characters.`;
          return;
        }
        nicknameInput.value = name;
        if (socket && socket.readyState === WebSocket.OPEN) {
          sendJoin();
          return;
        }

        connectionLabel.textContent = "Connecting to the local chat…";
        socket = new WebSocket(`ws://${location.hostname}:81/`);
        socket.addEventListener("open", sendJoin);
        socket.addEventListener("message", (event) => {
          let data;
          try {
            data = JSON.parse(event.data);
          } catch {
            connectionLabel.textContent = "Received an invalid server response.";
            return;
          }
          handleServerMessage(data);
        });
        socket.addEventListener("close", () => {
          joined = false;
          joinPanel.hidden = false;
          messageInput.disabled = true;
          sendButton.disabled = true;
          connectionLabel.textContent = "Disconnected. Join again to reconnect.";
        });
        socket.addEventListener("error", () => {
          connectionLabel.textContent = "Could not reach the local chat server.";
        });
      });

      messageForm.addEventListener("submit", (event) => {
        event.preventDefault();
        const text = messageInput.value.trim();
        if (!joined || !text || text.length > MAX_MESSAGE_LENGTH) return;
        if (!socket || socket.readyState !== WebSocket.OPEN) return;
        socket.send(JSON.stringify({ type: "message", text }));
        messageInput.value = "";
      });

      window.setInterval(() => {
        if (joined && socket && socket.readyState === WebSocket.OPEN) {
          socket.send(JSON.stringify({ type: "heartbeat" }));
        }
      }, 12000);
    })();
  </script>
</body>
</html>
)rawliteral";

#endif
