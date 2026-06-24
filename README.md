# sixlift

[![build](https://github.com/brianmatute011/sixlift/actions/workflows/build.yml/badge.svg)](https://github.com/brianmatute011/sixlift/actions/workflows/build.yml)

**Use the internet over IPv6 when your ISP's IPv4 is broken.**

Some ISPs (typically via a misbehaving CGNAT) leave you with working **IPv6**
but dead **IPv4**: dual-stack sites load over IPv6, but anything IPv4-only
times out. `sixlift` routes that IPv4 traffic over your working IPv6 using
public **NAT64/DNS64** gateways, and runs a small **watchdog** that keeps it
alive across IPv6 blips and DNS resets.

Single, dependency-free C binary built with CMake. Runs on **Linux** and
**Windows** behind one platform-abstraction layer.

```
sudo sixlift install   # install the command + watchdog
sudo sixlift on        # route IPv4 over IPv6, start self-healing
sixlift status         # check it
sudo sixlift off       # revert everything
```

## How it works

1. **DNS64** — `on` points your active connection's DNS at public DNS64
   resolvers (two independent providers, [nat64.net](https://nat64.net) and
   Trex, for redundancy). They synthesize an `AAAA` for IPv4-only names.
2. **NAT64** — packets to those synthesized addresses travel over your IPv6 to
   the provider's NAT64 gateway, which translates them to IPv4 and back.
3. **Watchdog** — a `systemd` timer runs `sixlift heal` every 2 minutes. When
   the path is down it:
   - removes any IPv6 `blackhole default` route in the *local* table (a common
     VPN/privacy kill-switch leftover that silently drops all IPv6),
   - reconnects the link if base IPv6 dropped,
   - re-asserts the DNS64 servers.

The connectivity checks are raw `getaddrinfo` + non-blocking `connect`
(POSIX sockets / Winsock). The rest is OS-specific and lives behind
[`platform.h`](include/sixlift/platform.h):

| Operation | Linux backend | Windows backend |
|---|---|---|
| Blackhole route removal | native **rtnetlink** | n/a (Linux-only artifact) |
| DNS configuration | `nmcli` / `resolvectl` | `Set-DnsClientServerAddress` |
| Watchdog | `systemd` timer | Task Scheduler (`schtasks`) |
| Adapter discovery | `nmcli` | `GetAdaptersAddresses` (IP Helper) |

## Install

Prebuilt binaries for every tagged version are on the
[releases page](https://github.com/brianmatute011/sixlift/releases/latest).
No build step required.

### Linux (prebuilt)

```sh
curl -L -o sixlift https://github.com/brianmatute011/sixlift/releases/latest/download/sixlift-linux-x86_64
chmod +x sixlift
sudo ./sixlift install   # copies to /usr/local/bin and creates the watchdog
sudo sixlift on          # route IPv4 over IPv6, start self-healing
sixlift status
```

x86-64, dynamically linked against glibc — runs on mainstream distros.

### Windows (prebuilt)

1. Download **`sixlift-windows-x86_64.exe`** from the
   [latest release](https://github.com/brianmatute011/sixlift/releases/latest).
2. Open **PowerShell as Administrator**, `cd` to the download folder, then:

```powershell
Rename-Item sixlift-windows-x86_64.exe sixlift.exe   # shorter to type
.\sixlift.exe install   # copies to C:\sixlift and creates a scheduled task
.\sixlift.exe on
.\sixlift.exe status
```

The installed copy lives at `C:\sixlift\sixlift.exe`; add `C:\sixlift` to your
`PATH` to call `sixlift` from anywhere.

## Build from source

Requires CMake ≥ 3.16 and a C11 compiler.

### Linux (gcc/clang)

```sh
cmake -S . -B build
cmake --build build
sudo ./build/sixlift install
sudo sixlift on
```

### Windows (MSVC)

```powershell
cmake -S . -B build
cmake --build build --config Release
# then, in an elevated prompt:
.\build\Release\sixlift.exe install
.\build\Release\sixlift.exe on
```

### Windows .exe cross-compiled from Linux (MinGW)

```sh
sudo apt install mingw-w64
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build-win        # -> build-win/sixlift.exe
```

## Commands

| Command | Description |
|---|---|
| `sudo sixlift install`   | Install the binary to `/usr/local/bin` and create the systemd watchdog |
| `sudo sixlift on`        | Enable NAT64/DNS64 and start the watchdog |
| `sudo sixlift off`       | Disable and revert DNS + stop the watchdog |
| `sixlift status`         | Show current state and run a test |
| `sixlift test`           | Probe whether IPv4-over-IPv6 works |
| `sixlift log`            | Show the watchdog log |
| `sudo sixlift uninstall` | Revert and delete every installed file |

## Logging

Every run emits structured, leveled diagnostics with an RFC 3339 UTC
timestamp, severity, and source location, through three independent sinks:

| Sink | Default level | Notes |
|---|---|---|
| File   | `DEBUG` | `/var/log/sixlift.log` (Linux), `C:\sixlift\sixlift.log` (Windows) |
| stderr | `WARN`  | keeps normal output clean; raise with `-v` / `-vv` |
| syslog | `INFO`  | POSIX only — visible via `journalctl -t sixlift` |

```
2026-06-24T14:21:13Z [DEBUG] proc.c:31 run_impl: exec: nmcli connection up CheetahX
2026-06-24T14:21:13Z [INFO ] service.c:55 sl_test: connectivity test: nat64=ok
```

Control verbosity with `-v` (INFO to stderr), `-vv` (DEBUG to stderr),
`-q` (errors only), or set the file/syslog threshold via the
`SIXLIFT_LOG_LEVEL` environment variable (`trace|debug|info|warn|error|fatal`).

## Scope & limitations

- **Linux & Windows.** Each has its own backend; macOS is not supported.
- **Name-based traffic only.** DNS64 fixes anything resolved by hostname.
  Connections to a hard-coded IPv4 literal (e.g. `ping 8.8.8.8`) are not
  translated.
- **It's a workaround.** It depends on your IPv6 plus a free third-party NAT64
  service. The real fix is your ISP restoring IPv4 (or giving you a public IP).

## License

MIT — see [LICENSE](LICENSE).
