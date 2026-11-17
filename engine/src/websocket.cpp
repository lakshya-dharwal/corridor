#include "corridor/websocket.hpp"

#include <cstdint>

namespace corridor {

namespace {

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::uint32_t rotl(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::uint8_t byte_at(const std::string& text, std::size_t index) {
    return static_cast<std::uint8_t>(text[index]);
}

}  // namespace

std::string sha1(std::string_view input) {
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    // Pad: a 1 bit, zeros to 56 mod 64, then the bit length as 64-bit big-endian.
    std::string message(input);
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8;
    message += '\x80';
    while (message.size() % 64 != 56) message += '\0';
    for (int shift = 56; shift >= 0; shift -= 8) {
        message += static_cast<char>((bit_length >> shift) & 0xFF);
    }

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const std::size_t at = offset + static_cast<std::size_t>(i) * 4;
            w[i] = (std::uint32_t{byte_at(message, at)} << 24) |
                   (std::uint32_t{byte_at(message, at + 1)} << 16) |
                   (std::uint32_t{byte_at(message, at + 2)} << 8) |
                   std::uint32_t{byte_at(message, at + 3)};
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t next = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = next;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::string digest(20, '\0');
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[static_cast<std::size_t>(i * 4 + j)] =
                static_cast<char>((h[i] >> (24 - 8 * j)) & 0xFF);
        }
    }
    return digest;
}

std::string base64_encode(std::string_view input) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < input.size(); i += 3) {
        const std::size_t remaining = input.size() - i;
        const std::uint32_t b0 = static_cast<std::uint8_t>(input[i]);
        const std::uint32_t b1 = remaining > 1 ? static_cast<std::uint8_t>(input[i + 1]) : 0;
        const std::uint32_t b2 = remaining > 2 ? static_cast<std::uint8_t>(input[i + 2]) : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(triple >> 18) & 0x3F];
        out += kAlphabet[(triple >> 12) & 0x3F];
        out += remaining > 1 ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out += remaining > 2 ? kAlphabet[triple & 0x3F] : '=';
    }
    return out;
}

std::string websocket_accept_key(const std::string& client_key) {
    return base64_encode(sha1(client_key + kWebSocketGuid));
}

std::optional<std::string> websocket_handshake_response(const Request& request,
                                                        std::string& error) {
    if (request.method != "GET" || !wants_websocket(request)) {
        error = "not a websocket upgrade request";
        return std::nullopt;
    }
    const auto version = request.headers.find("sec-websocket-version");
    if (version == request.headers.end() || version->second != "13") {
        error = "unsupported websocket version";
        return std::nullopt;
    }
    const auto key = request.headers.find("sec-websocket-key");
    if (key == request.headers.end() || key->second.size() != 24) {  // base64 of 16 bytes
        error = "missing or malformed Sec-WebSocket-Key";
        return std::nullopt;
    }
    return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " +
           websocket_accept_key(key->second) + "\r\n\r\n";
}

std::string encode_ws_frame(WsOpcode opcode, std::string_view payload) {
    std::string frame;
    frame += static_cast<char>(0x80 | static_cast<int>(opcode));  // FIN set, no fragmentation
    const std::uint64_t length = payload.size();
    if (length < 126) {
        frame += static_cast<char>(length);
    } else if (length <= 0xFFFF) {
        frame += static_cast<char>(126);
        frame += static_cast<char>((length >> 8) & 0xFF);
        frame += static_cast<char>(length & 0xFF);
    } else {
        frame += static_cast<char>(127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame += static_cast<char>((length >> shift) & 0xFF);
        }
    }
    frame.append(payload);
    return frame;
}

WsDecode decode_ws_frame(std::string& buffer, WsFrame& out) {
    if (buffer.size() < 2) return WsDecode::Incomplete;
    const std::uint8_t b0 = byte_at(buffer, 0);
    const std::uint8_t b1 = byte_at(buffer, 1);
    if (b0 & 0x70) return WsDecode::Error;        // reserved bits: no extensions negotiated
    if (!(b1 & 0x80)) return WsDecode::Error;     // client frames must be masked

    const int opcode = b0 & 0x0F;
    const bool fin = (b0 & 0x80) != 0;
    const bool known = opcode <= 0x2 || (opcode >= 0x8 && opcode <= 0xA);
    if (!known) return WsDecode::Error;

    std::uint64_t length = b1 & 0x7F;
    std::size_t header = 2;
    if (length == 126) {
        if (buffer.size() < 4) return WsDecode::Incomplete;
        length = (std::uint64_t{byte_at(buffer, 2)} << 8) | byte_at(buffer, 3);
        header = 4;
    } else if (length == 127) {
        if (buffer.size() < 10) return WsDecode::Incomplete;
        length = 0;
        for (std::size_t i = 2; i < 10; ++i) length = (length << 8) | byte_at(buffer, i);
        header = 10;
    }
    if (length > kMaxWsPayload) return WsDecode::Error;
    const bool control = opcode >= 0x8;
    if (control && (length > 125 || !fin)) return WsDecode::Error;  // RFC 6455 section 5.5

    const std::size_t total = header + 4 + static_cast<std::size_t>(length);
    if (buffer.size() < total) return WsDecode::Incomplete;

    out.opcode = static_cast<WsOpcode>(opcode);
    out.fin = fin;
    out.payload.assign(static_cast<std::size_t>(length), '\0');
    for (std::size_t i = 0; i < out.payload.size(); ++i) {
        out.payload[i] = static_cast<char>(byte_at(buffer, header + 4 + i) ^
                                           byte_at(buffer, header + (i % 4)));
    }
    buffer.erase(0, total);
    return WsDecode::Complete;
}

}  // namespace corridor
