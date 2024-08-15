#pragma once

namespace Preset {
    namespace SingBox {
        inline QStringList VpnImplementation = {"gvisor", "system", "mixed"};
        inline QStringList DomainStrategy = {"", "ipv4_only", "ipv6_only", "prefer_ipv4", "prefer_ipv6"};
        inline QStringList UtlsFingerPrint = {"", "chrome", "firefox", "edge", "safari", "360", "qq", "ios", "android", "random", "randomized", "chrome_psk", "chrome_psk_shuffle", "chrome_padding_psk_shuffle", "chrome_pq", "chrome_pq_psk"};
        inline QStringList ShadowsocksMethods = {"2022-blake3-aes-128-gcm", "2022-blake3-aes-256-gcm", "2022-blake3-chacha20-poly1305", "none", "aes-128-gcm", "aes-192-gcm", "aes-256-gcm", "chacha20-ietf-poly1305", "xchacha20-ietf-poly1305", "aes-128-ctr", "aes-192-ctr", "aes-256-ctr", "aes-128-cfb", "aes-192-cfb", "aes-256-cfb", "rc4-md5", "chacha20-ietf", "xchacha20"};
        inline QStringList V2RAYTransports = {"http", "grpc", "quic", "httpupgrade", "ws", "tcp"};
        inline QStringList Flows = {"xtls-rprx-vision"};
    } // namespace SingBox

} // namespace Preset
