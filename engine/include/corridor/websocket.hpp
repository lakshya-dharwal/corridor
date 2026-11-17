// Server side of the WebSocket protocol (RFC 6455): the opening handshake and
// frame encoding/decoding. No third-party dependencies.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "corridor/http.hpp"

namespace corridor {

// Raw 20-byte SHA-1 digest. The WebSocket handshake requires it; it is not used
// for anything security-sensitive.
std::string sha1(std::string_view input);
std::string base64_encode(std::string_view input);

// Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key.
std::string websocket_accept_key(const std::string& client_key);

// The "101 Switching Protocols" response for a valid upgrade request, or
// nullopt with a reason in `error`.
std::optional<std::string> websocket_handshake_response(const Request& request,
                                                        std::string& error);

enum class WsOpcode { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

struct WsFrame {
    WsOpcode opcode = WsOpcode::Text;
    bool fin = true;
    std::string payload;
};

// Frames sent by a server are never masked.
std::string encode_ws_frame(WsOpcode opcode, std::string_view payload);

enum class WsDecode { Complete, Incomplete, Error };

// Largest client payload accepted. Clients only send control frames and short
// messages here, so anything bigger is treated as abuse.
inline constexpr std::size_t kMaxWsPayload = 64 * 1024;

// Decodes one client frame from the front of `buffer` and removes it. Client
// frames must be masked; an unmasked, oversized or malformed frame is an Error.
WsDecode decode_ws_frame(std::string& buffer, WsFrame& out);

}  // namespace corridor
