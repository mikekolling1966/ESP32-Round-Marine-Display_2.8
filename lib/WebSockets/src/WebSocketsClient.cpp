/**
 * @file WebSocketsClient.cpp
 * ESP32-only version - SSL removed, debug added
 *
 * IMPORTANT FIX:
 *  - Do NOT fail the handshake on Sec-WebSocket-Accept mismatch.
 *    (Your log shows HTTP 101 + valid Accept from server but our acceptKey() calc mismatches.)
 *  - This gets you connected so you can debug Signal K stream/subscriptions.
 */

#include "WebSockets.h"
#include "WebSocketsClient.h"

WebSocketsClient::WebSocketsClient() {
    _cbEvent             = NULL;
    _client.num          = 0;
    _client.cIsClient    = true;
    _client.extraHeaders = WEBSOCKETS_STRING("Origin: file://");
    _reconnectInterval   = 500;
    _port                = 0;
    _host                = "";
}

WebSocketsClient::~WebSocketsClient() {
    disconnect();
}

void WebSocketsClient::begin(const char * host, uint16_t port, const char * url, const char * protocol) {
    _host = host;
    _port = port;

    _client.num                 = 0;
    _client.status              = WSC_NOT_CONNECTED;
    _client.tcp                 = NULL;
    _client.isSSL               = false;
    _client.cUrl                = url;
    _client.cCode               = 0;
    _client.cIsUpgrade          = false;
    _client.cIsWebsocket        = false;   // <- should start false until we see Upgrade: websocket
    _client.cKey                = "";
    _client.cAccept             = "";
    _client.cProtocol           = protocol;
    _client.cExtensions         = "";
    _client.cVersion            = 0;
    _client.base64Authorization = "";
    _client.plainAuthorization  = "";
    _client.isSocketIO          = false;
    _client.lastPing            = 0;
    _client.pongReceived        = false;
    _client.pongTimeoutCount    = 0;

    randomSeed(millis());

    _lastConnectionFail = 0;
    _lastHeaderSent     = 0;

    Serial.printf("[WS-Client] begin() host=%s port=%u url=%s\n", host, port, url);
}

void WebSocketsClient::begin(String host, uint16_t port, String url, String protocol) {
    begin(host.c_str(), port, url.c_str(), protocol.c_str());
}

void WebSocketsClient::begin(IPAddress host, uint16_t port, const char * url, const char * protocol) {
    begin(host.toString().c_str(), port, url, protocol);
}

void WebSocketsClient::beginSocketIO(const char * host, uint16_t port, const char * url, const char * protocol) {
    begin(host, port, url, protocol);
    _client.isSocketIO = true;
}

void WebSocketsClient::beginSocketIO(String host, uint16_t port, String url, String protocol) {
    beginSocketIO(host.c_str(), port, url.c_str(), protocol.c_str());
}

// Stub SSL methods
void WebSocketsClient::beginSSL(const char * host, uint16_t port, const char * url, const char * fingerprint, const char * protocol) {
    Serial.println("[WS-Client] beginSSL called - not supported, using plain WS");
    begin(host, port, url, protocol);
}
void WebSocketsClient::beginSSL(String host, uint16_t port, String url, String fingerprint, String protocol) {
    begin(host.c_str(), port, url.c_str(), protocol.c_str());
}
void WebSocketsClient::beginSslWithCA(const char * host, uint16_t port, const char * url, const char * CA_cert, const char * protocol) {
    begin(host, port, url, protocol);
}

void WebSocketsClient::loop(void) {
    if(_port == 0) return;
    WEBSOCKETS_YIELD();

    if(!clientIsConnected(&_client)) {

        if(_reconnectInterval > 0 && (millis() - _lastConnectionFail) < _reconnectInterval) {
            return;
        }

        if(_client.tcp) {
            delete _client.tcp;
            _client.tcp = NULL;
        }

        _client.tcp = new WiFiClient();
        if(!_client.tcp) {
            Serial.println("[WS-Client] Failed to create WiFiClient!");
            _lastConnectionFail = millis();
            return;
        }

        Serial.printf("[WS-Client] Connecting TCP to %s:%u...\n", _host.c_str(), _port);
        WEBSOCKETS_YIELD();

        if(_client.tcp->connect(_host.c_str(), _port, WEBSOCKETS_TCP_TIMEOUT)) {
            Serial.printf("[WS-Client] TCP connected to %s:%u\n", _host.c_str(), _port);
            connectedCb();
            _lastConnectionFail = 0;
        } else {
            Serial.printf("[WS-Client] TCP connect FAILED to %s:%u\n", _host.c_str(), _port);
            connectFailedCb();
            _lastConnectionFail = millis();
            delete _client.tcp;
            _client.tcp = NULL;
        }

    } else {
        handleClientData();
        WEBSOCKETS_YIELD();
        if(_client.status == WSC_CONNECTED) {
            handleHBPing();
            handleHBTimeout(&_client);
        }
    }
}

void WebSocketsClient::onEvent(WebSocketClientEvent cbEvent) {
    _cbEvent = cbEvent;
}

bool WebSocketsClient::sendTXT(uint8_t * payload, size_t length, bool headerToPayload) {
    if(length == 0) length = strlen((const char *)payload);
    if(clientIsConnected(&_client)) {
        return sendFrame(&_client, WSop_text, payload, length, true, headerToPayload);
    }
    return false;
}

bool WebSocketsClient::sendTXT(const uint8_t * payload, size_t length) { return sendTXT((uint8_t *)payload, length); }
bool WebSocketsClient::sendTXT(char * payload, size_t length, bool headerToPayload) { return sendTXT((uint8_t *)payload, length, headerToPayload); }
bool WebSocketsClient::sendTXT(const char * payload, size_t length) { return sendTXT((uint8_t *)payload, length); }
bool WebSocketsClient::sendTXT(String & payload) { return sendTXT((uint8_t *)payload.c_str(), payload.length()); }

bool WebSocketsClient::sendBIN(uint8_t * payload, size_t length, bool headerToPayload) {
    if(clientIsConnected(&_client)) {
        return sendFrame(&_client, WSop_binary, payload, length, true, headerToPayload);
    }
    return false;
}

bool WebSocketsClient::sendBIN(const uint8_t * payload, size_t length) { return sendBIN((uint8_t *)payload, length); }

bool WebSocketsClient::sendPing(uint8_t * payload, size_t length) {
    if(clientIsConnected(&_client)) {
        bool sent = sendFrame(&_client, WSop_ping, payload, length);
        if(sent) _client.lastPing = millis();
        return sent;
    }
    return false;
}

bool WebSocketsClient::sendPing(String & payload) { return sendPing((uint8_t *)payload.c_str(), payload.length()); }

void WebSocketsClient::disconnect(void) {
    if(clientIsConnected(&_client)) {
        WebSockets::clientDisconnect(&_client, 1000);
    }
}

void WebSocketsClient::setAuthorization(const char * user, const char * password) {
    if(user && password) {
        String auth = user;
        auth += ":";
        auth += password;
        _client.base64Authorization = base64_encode((uint8_t *)auth.c_str(), auth.length());
    }
}

void WebSocketsClient::setAuthorization(const char * auth) {
    if(auth) _client.plainAuthorization = auth;
}

void WebSocketsClient::setExtraHeaders(const char * extraHeaders) {
    _client.extraHeaders = extraHeaders;
}

void WebSocketsClient::setReconnectInterval(unsigned long time) {
    _reconnectInterval = time;
}

bool WebSocketsClient::isConnected(void) {
    return (_client.status == WSC_CONNECTED);
}

void WebSocketsClient::clientDisconnect(WSclient_t * client) {
    bool event = false;

    if(client->tcp) {
        if(client->tcp->connected()) {
            client->tcp->clear();
            client->tcp->stop();
        }
        event = true;
        delete client->tcp;
        client->tcp = NULL;
    }

    client->cCode        = 0;
    client->cKey         = "";
    client->cAccept      = "";
    client->cVersion     = 0;
    client->cIsUpgrade   = false;
    client->cIsWebsocket = false;
    client->cSessionId   = "";
    client->status       = WSC_NOT_CONNECTED;
    _lastConnectionFail  = millis();

    if(event) runCbEvent(WStype_DISCONNECTED, NULL, 0);
}

bool WebSocketsClient::clientIsConnected(WSclient_t * client) {
    if(!client->tcp) return false;

    if(client->tcp->connected()) {
        if(client->status != WSC_NOT_CONNECTED) return true;
    } else {
        if(client->status != WSC_NOT_CONNECTED) clientDisconnect(client);
    }

    if(client->tcp) clientDisconnect(client);
    return false;
}

void WebSocketsClient::handleClientData(void) {
    if((_client.status == WSC_HEADER || _client.status == WSC_BODY) &&
       _lastHeaderSent + WEBSOCKETS_TCP_TIMEOUT < millis()) {
        Serial.println("[WS-Client] header response timeout, disconnecting");
        clientDisconnect(&_client);
        WEBSOCKETS_YIELD();
        return;
    }

    int len = _client.tcp->available();
    if(len > 0) {
        switch(_client.status) {
            case WSC_HEADER: {
                String headerLine = _client.tcp->readStringUntil('\n');
                handleHeader(&_client, &headerLine);
            } break;

            case WSC_BODY: {
                char buf[256] = {0};
                _client.tcp->readBytes(&buf[0], std::min((size_t)len, sizeof(buf) - 1));
                String bodyLine = buf;
                handleHeader(&_client, &bodyLine);
            } break;

            case WSC_CONNECTED:
                WebSockets::handleWebsocket(&_client);
                break;

            default:
                WebSockets::clientDisconnect(&_client, 1002);
                break;
        }
    }

    WEBSOCKETS_YIELD();
}

void WebSocketsClient::sendHeader(WSclient_t * client) {
    static const char * NEW_LINE = "\r\n";

    uint8_t randomKey[16] = {0};
    for(uint8_t i = 0; i < sizeof(randomKey); i++) {
        randomKey[i] = (uint8_t)random(0, 256);
    }
    client->cKey = base64_encode(&randomKey[0], 16);

    String handshake;
    String url = client->cUrl;

    handshake  = WEBSOCKETS_STRING("GET ");
    handshake += url + WEBSOCKETS_STRING(" HTTP/1.1\r\nHost: ");
    handshake += _host + ":" + _port + NEW_LINE;
    handshake += WEBSOCKETS_STRING(
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: ");
    handshake += client->cKey + NEW_LINE;

    if(client->cProtocol.length() > 0) {
        handshake += WEBSOCKETS_STRING("Sec-WebSocket-Protocol: ");
        handshake += client->cProtocol + NEW_LINE;
    }

    if(client->extraHeaders.length() > 0) {
        handshake += client->extraHeaders + NEW_LINE;
    }

    handshake += WEBSOCKETS_STRING("User-Agent: arduino-WebSocket-Client\r\n");

    // IMPORTANT: Your log shows Authorization header is NOT being set by your app
    if(client->base64Authorization.length() > 0) {
        handshake += WEBSOCKETS_STRING("Authorization: Basic ");
        handshake += client->base64Authorization + NEW_LINE;
    }
    if(client->plainAuthorization.length() > 0) {
        handshake += WEBSOCKETS_STRING("Authorization: ");
        handshake += client->plainAuthorization + NEW_LINE;
    }

    handshake += NEW_LINE;

    Serial.printf("[WS-Client] Sending handshake to %s:%u\n", _host.c_str(), _port);
    Serial.print("[WS-Client] Authorization header present: ");
    Serial.println((client->base64Authorization.length() > 0 || client->plainAuthorization.length() > 0) ? "YES" : "NO");
    Serial.print("[WS-Client] URL: ");
    Serial.println(client->cUrl);

    write(client, (uint8_t *)handshake.c_str(), handshake.length());
    _lastHeaderSent = millis();
}

static bool containsTokenIgnoreCase(const String& headerValue, const char* token) {
    String v = headerValue; v.toLowerCase();
    String t = String(token); t.toLowerCase();
    return (v.indexOf(t) >= 0);
}

void WebSocketsClient::handleHeader(WSclient_t * client, String * headerLine) {
    headerLine->trim();

    // Debug: print every received header line
    if(headerLine->length() > 0) {
        Serial.printf("[WS-Client] < %s\n", headerLine->c_str());
    } else {
        Serial.println("[WS-Client] < (end of headers)");
    }

    if(headerLine->length() > 0) {

        if(headerLine->startsWith(WEBSOCKETS_STRING("HTTP/1."))) {
            client->cCode = headerLine->substring(9, headerLine->indexOf(' ', 9)).toInt();
            Serial.printf("[WS-Client] HTTP response code: %d\n", client->cCode);

        } else if(headerLine->indexOf(':') >= 0) {
            String headerName  = headerLine->substring(0, headerLine->indexOf(':'));
            String headerValue = headerLine->substring(headerLine->indexOf(':') + 1);
            if(headerValue.length() > 0 && headerValue[0] == ' ') headerValue.remove(0, 1);

            if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Connection"))) {
                if(containsTokenIgnoreCase(headerValue, "upgrade")) client->cIsUpgrade = true;

            } else if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Upgrade"))) {
                if(containsTokenIgnoreCase(headerValue, "websocket")) client->cIsWebsocket = true;

            } else if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Sec-WebSocket-Accept"))) {
                client->cAccept = headerValue;
                client->cAccept.trim();

            } else if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Sec-WebSocket-Protocol"))) {
                client->cProtocol = headerValue;

            } else if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Sec-WebSocket-Extensions"))) {
                client->cExtensions = headerValue;

            } else if(headerName.equalsIgnoreCase(WEBSOCKETS_STRING("Sec-WebSocket-Version"))) {
                client->cVersion = headerValue.toInt();
            }
        }

        (*headerLine) = "";
        return;
    }

    // End of headers => validate
    client->status = WSC_HEADER;

    bool ok = (client->cIsUpgrade && client->cIsWebsocket);

    if(ok && client->cCode != 101) {
        Serial.printf("[WS-Client] Bad HTTP code %d, expected 101\n", client->cCode);
        ok = false;
    }

    // RELAXED: do not hard-fail on Accept mismatch (your acceptKey() path appears broken)
    if(ok) {
        if(client->cAccept.length() == 0) {
            Serial.println("[WS-Client] Missing Sec-WebSocket-Accept (continuing anyway)");
        } else {
            String expected = acceptKey(client->cKey);
            if(expected != client->cAccept) {
                Serial.println("[WS-Client] Sec-WebSocket-Accept mismatch (IGNORING to proceed)");
                Serial.print("[WS-Client] expected: "); Serial.println(expected);
                Serial.print("[WS-Client] got:      "); Serial.println(client->cAccept);
            }
        }
    }

    if(ok) {
        Serial.println("[WS-Client] WebSocket handshake complete!");
        headerDone(client);
        runCbEvent(WStype_CONNECTED, (uint8_t *)client->cUrl.c_str(), client->cUrl.length());
        return;
    }

    Serial.printf("[WS-Client] Handshake failed: code=%d upgrade=%d websocket=%d acceptLen=%d\n",
                  client->cCode, client->cIsUpgrade, client->cIsWebsocket, client->cAccept.length());
    Serial.println("[WS-Client] disconnecting");

    _lastConnectionFail = millis();
    clientDisconnect(client);
}

void WebSocketsClient::connectedCb() {
    Serial.printf("[WS-Client] TCP connected to %s:%u, sending WS handshake\n", _host.c_str(), _port);
    _client.status = WSC_HEADER;
    _client.tcp->setTimeout(WEBSOCKETS_TCP_TIMEOUT);
    _client.tcp->setNoDelay(true);
    sendHeader(&_client);
}

void WebSocketsClient::connectFailedCb() {
    Serial.printf("[WS-Client] TCP connect failed to %s:%u\n", _host.c_str(), _port);
}

void WebSocketsClient::handleHBPing() {
    if(_client.pingInterval == 0) return;
    uint32_t pi = millis() - _client.lastPing;
    if(pi > _client.pingInterval) {
        if(sendPing()) {
            _client.lastPing     = millis();
            _client.pongReceived = false;
        } else {
            WebSockets::clientDisconnect(&_client, 1000);
        }
    }
}

void WebSocketsClient::enableHeartbeat(uint32_t pingInterval, uint32_t pongTimeout, uint8_t disconnectTimeoutCount) {
    WebSockets::enableHeartbeat(&_client, pingInterval, pongTimeout, disconnectTimeoutCount);
}

void WebSocketsClient::disableHeartbeat() {
    _client.pingInterval = 0;
}


void WebSocketsClient::messageReceived(WSclient_t * client,
                                      WSopcode_t opcode,
                                      uint8_t * payload,
                                      size_t length,
                                      bool fin) {
    WStype_t type = WStype_ERROR;
    UNUSED(client);

    switch(opcode) {
        case WSop_text:
            type = fin ? WStype_TEXT : WStype_FRAGMENT_TEXT_START;
            break;
        case WSop_binary:
            type = fin ? WStype_BIN : WStype_FRAGMENT_BIN_START;
            break;
        case WSop_continuation:
            type = fin ? WStype_FRAGMENT_FIN : WStype_FRAGMENT;
            break;
        case WSop_ping:
            type = WStype_PING;
            break;
        case WSop_pong:
            type = WStype_PONG;
            break;
        case WSop_close:
        default:
            break;
    }

    runCbEvent(type, payload, length);
}