# LocalChat

LocalChat is a small, temporary messaging system hosted by an ESP8266 NodeMCU. The board creates its own password-protected Wi-Fi network and serves a lightweight chat page directly to connected phones and computers. It does not provide internet access and does not depend on mobile data, SMS, cloud services, or external messaging servers.

The first version is an MVP for two active participants. It keeps recent messages and online-user state in RAM only; restarting or powering off the board clears the session. The project is designed to keep chat traffic on the local network, but this is not an absolute guarantee against every form of network leakage.

## Features

- Password-protected Wi-Fi access point hosted by the ESP8266.
- Chat page served locally at `http://192.168.4.1/`; HTML, CSS, and JavaScript are embedded in the firmware.
- Temporary nicknames and online status for up to two active participants.
- Local WebSocket message relay with a configurable recent-history ring buffer (50 messages by default).
- Heartbeats and automatic cleanup of inactive participants.
- No database, SD card, EEPROM, flash-based chat history, cloud storage, or browser persistent message storage.
- Named compile-time limits make it possible to increase capacity later, subject to hardware and WebSocket library limits.

## Hardware

- ESP8266 NodeMCU development board.
- USB data cable and a suitable USB power source.
- Wi-Fi-enabled phones or computers.

No Arduino Uno/Nano, GSM module, SIM card, display, SD card, or separate server is required.

## Software and libraries

- Arduino IDE 2.x (or another ESP8266-compatible Arduino build environment).
- ESP8266 Arduino core for the board support and its built-in `ESP8266WiFi` and `ESP8266WebServer` libraries.
- **WebSockets** by Markus Sattler (Links2004), installed through Arduino IDE Library Manager.
- **ArduinoJson 6.x**, installed through Arduino IDE Library Manager.

## Installation and configuration

1. **Install the ESP8266 board support.** In Arduino IDE, open **File → Preferences** and add `https://arduino.esp8266.com/stable/package_esp8266com_index.json` to *Additional boards manager URLs*. Open **Tools → Board → Boards Manager**, search for `esp8266`, and install the ESP8266 platform.
2. **Install the libraries.** Open **Sketch → Include Library → Manage Libraries**. Search for and install **WebSockets** by Markus Sattler and **ArduinoJson** (version 6.x).
3. **Open the sketch.** Open `LocalChat/LocalChat.ino` from this repository in Arduino IDE. Keep `chat_page.h` in the same `LocalChat` sketch folder.
4. **Set the Wi-Fi name and password.** Near the top of `LocalChat.ino`, edit `WIFI_SSID` and `WIFI_PASSWORD`. The password must be at least 8 characters. Replace the example password before using the project. Anyone who knows the password can connect to this local network.
5. **Choose the board.** Connect the NodeMCU by USB and select **Tools → Board → ESP8266 Boards → NodeMCU 1.0 (ESP-12E Module)** (or the matching board entry for your board). Select its serial port. The default upload speed is suitable for most boards.
6. **Upload the firmware.** Click **Upload**. Open **Tools → Serial Monitor** at `115200` baud after upload. The monitor reports the configured Wi-Fi name and local chat address.
7. **Join the local network.** On each phone/computer, connect to the configured LocalChat Wi-Fi network and enter its password. The network has no internet access; a device may show a “no internet” warning. Stay connected to the network.
8. **Open the chat page.** Open a browser and navigate to `http://192.168.4.1/`. Enter a nickname and choose **Join**. A second person can connect and join the same way. A third participant can load the page but will be told the chat is full until a participant leaves or expires.

## Configuration options

The firmware settings are near the top of `LocalChat/LocalChat.ino`:

| Setting | Default | Purpose |
| --- | ---: | --- |
| `WIFI_SSID` | `LocalChat` | Access point name. |
| `WIFI_PASSWORD` | `ChangeMe1234` | Access point password; replace before use. |
| `MAX_USERS` | `2` | Maximum active chat participants. |
| `MAX_WIFI_CLIENTS` | `8` | Wi-Fi clients allowed to connect, so a visitor can see the full-chat notice. |
| `CHAT_HISTORY_SIZE` | `50` | Number of most recent messages retained in RAM. |
| `MAX_NAME_LENGTH` | `24` | Maximum nickname size in bytes. |
| `MAX_MESSAGE_LENGTH` | `160` | Maximum message size in bytes. |
| `INACTIVITY_TIMEOUT_MS` | `45000` | Time without a heartbeat before removing an online participant. |

The browser sends a heartbeat every 12 seconds. The Links2004 WebSockets library defaults to five total simultaneous WebSocket connections; keep `MAX_USERS` within that limit and raise `WEBSOCKETS_SERVER_CLIENT_MAX` in the library configuration if a larger group is required. Increasing `MAX_USERS` also increases RAM use; test carefully on the target board. `MAX_NAME_LENGTH` and `MAX_MESSAGE_LENGTH` are sent to the page at join time so the browser input limits stay aligned with the firmware. The ESP8266 has limited RAM, so increasing message sizes/history or participant counts should be measured rather than assumed safe.

## Usage and manual test steps

1. Power on or reset the NodeMCU and wait for the access point to appear.
2. Connect one phone to the LocalChat Wi-Fi, browse to `http://192.168.4.1/`, choose a nickname, and join.
3. Connect a second phone, join with a different nickname, and verify that both names appear in **Online**.
4. Send messages from each phone. Verify they appear promptly on both devices, including the sender.
5. Connect a third device to the Wi-Fi and open the page. Attempt to join and verify it receives the chat-full notice. Close one active participant's page, then retry joining from the third device.
6. To test inactivity cleanup, leave a joined browser without network access or close it abruptly. Verify it disappears from the online list after about 45 seconds. Normal browser closure should usually remove it sooner through the WebSocket disconnect event.
7. Send more than 50 messages, then reload/rejoin a participant and verify only the newest 50 messages are replayed.
8. Restart or power off the NodeMCU, reconnect after it starts, and verify the online list and chat history are empty.

## Privacy and storage

LocalChat is designed as a local and temporary communication bubble: the firmware hosts the access point and server, and the webpage connects to that local address. Chat messages are stored in a fixed-size RAM buffer and nicknames/online state are held temporarily in RAM. The webpage keeps rendered messages only in the current page session and does not use `localStorage`, IndexedDB, or another persistent browser store.

The firmware does not write messages to a database, SD card, EEPROM, flash, or an external server. A reboot or power loss clears runtime chat state. The project contains no analytics, ads, external fonts, CDNs, or internet-hosted page resources.

This local-network design is not an absolute zero-leakage guarantee. The first version does not provide end-to-end encryption, HTTPS, or WebSocket TLS; users with access to the local Wi-Fi may be able to observe unencrypted local traffic. A connected phone may also have its own cellular connection or operating-system networking behavior outside the project's control. Do not use this MVP for sensitive communications.

## Limitations

- Requires an ESP8266 access point and Wi-Fi range; it does not extend internet access.
- Exactly two chat participants are enabled by default. Other connected visitors can see a full-chat notice.
- The access point password is configured in firmware source and should be changed before upload.
- Browsers generally must be directed manually to `192.168.4.1`; captive-portal behavior varies by device.
- Messages and nicknames are unencrypted on the local network in this version.
- Firmware upload, radio behavior, and multi-device tests require physical ESP8266 hardware.

## Future improvements

- Add efficient end-to-end encryption so the NodeMCU only relays ciphertext.
- Improve reconnect and delivery acknowledgements while keeping memory bounded.
- Increase participant capacity after measuring ESP8266 memory and WebSocket client limits.
- Port to ESP32 if a larger user count or richer protocol exceeds ESP8266 resources.
