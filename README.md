# sixlift

**Use the internet over IPv6 when your ISP's IPv4 is broken.**

Some ISPs (typically via a misbehaving CGNAT) leave you with working **IPv6**
but dead **IPv4**: dual-stack sites load over IPv6, but anything IPv4-only
times out. `sixlift` routes that IPv4 traffic over your working IPv6 using
public **NAT64/DNS64** gateways, and runs a small **watchdog** that keeps it
alive across IPv6 blips and DNS resets.

It's a single, dependency-free C binary built with CMake.

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

The connectivity checks are raw `getaddrinfo` + non-blocking `connect`; the
route surgery is native **rtnetlink** (`RTM_GETROUTE` dump + `RTM_DELROUTE`).
Connection/DNS plumbing is delegated to `nmcli`/`resolvectl`/`systemctl`,
which own that state.

## Build

Requires a C11 compiler, CMake ≥ 3.16, and Linux kernel headers.

```sh
cmake -S . -B build
cmake --build build
# binary at build/sixlift
```

Then install system-wide (sets up the watchdog too):

```sh
sudo ./build/sixlift install
sudo sixlift on
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

## Scope & limitations

- **Linux only.** It speaks rtnetlink and drives systemd + NetworkManager.
  CMake makes it portable across Linux distros and CPU architectures, **not**
  across operating systems.
- **Name-based traffic only.** DNS64 fixes anything resolved by hostname.
  Connections to a hard-coded IPv4 literal (e.g. `ping 8.8.8.8`) are not
  translated.
- **It's a workaround.** It depends on your IPv6 plus a free third-party NAT64
  service. The real fix is your ISP restoring IPv4 (or giving you a public IP).

## License

MIT — see [LICENSE](LICENSE).
