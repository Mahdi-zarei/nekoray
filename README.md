# NekoRay / NekoBox For PC

Qt based cross-platform GUI proxy configuration manager (backend: sing-box)

Support Windows / Linux / MacOS out of the box now.

### Using Tun mode in MacOS
To use Tun mode in MacOS, you need to open the app as root.

Open the app like this:

```shell
sudo /Applications/nekoray.app/Contents/MacOS/nekoray
```

It will open a Nekoray instance with no configurations; if you have configured your Nekoray in normal mode, close the app and do this to copy them:

**`Note:`** Replace `YOU` with your username

```shell
sudo cp -r /Users/YOU/Library/Preferences/nekoray/ /private/var/root/Library/Preferences/nekoray
```

Now, you can open it with the first command.


### GitHub Releases (Portable ZIP)

[![GitHub All Releases](https://img.shields.io/github/downloads/Mahdi-zarei/nekoray/total?label=downloads-total&logo=github&style=flat-square)](https://github.com/Mahdi-zarei/nekoray/releases)

## Proxy

- SOCKS (4/4a/5)
- HTTP(S)
- Shadowsocks
- VMess
- VLESS
- Trojan
- TUIC ( sing-box )
- NaïveProxy ( Custom Core )
- Hysteria ( Custom Core or sing-box )
- Hysteria2 ( Custom Core or sing-box )
- Custom Outbound
- Custom Config
- Custom Core

## Subscription

We support widely used link formats(like Shadowsocks, Clash and v2rayN) as well as custom
outbound and custom configs. The subscription file can contain commented lines(starting with // or #, empty lines are ignored as well),
JSON strings(can be human-readable or compact) and import links(each link should be on a separate line).

## Credits

Core:

- [SagerNet/sing-box](https://github.com/SagerNet/sing-box)
- [Matsuridayo/sing-box-extra](https://github.com/Mahdi-zarei/sing-box-extra)

Gui:

- [Qv2ray](https://github.com/Qv2ray/Qv2ray)
- [Qt](https://www.qt.io/)
- [protobuf](https://github.com/protocolbuffers/protobuf)
- [yaml-cpp](https://github.com/jbeder/yaml-cpp)
- [zxing-cpp](https://github.com/nu-book/zxing-cpp)
- [QHotkey](https://github.com/Skycoder42/QHotkey)
- [AppImageKit](https://github.com/AppImage/AppImageKit)
