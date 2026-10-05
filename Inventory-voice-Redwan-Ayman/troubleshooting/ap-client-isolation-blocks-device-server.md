# A server on the device is unreachable, though the device reaches everything else

| | |
|---|---|
| **Domain** | `Inventory-voice-Redwan-Ayman` |
| **Date** | 2026-10-05 |
| **Task** | [firmware](../tasks/2026-10-05-esp32-p4-voice-firmware/README.md) |

## Symptom

The device's HTTP server starts and demonstrably serves (a loopback request returns the
full page), its outbound HTTPS calls succeed, and ARP resolves its MAC correctly -- but
nothing on the LAN can connect:

```
ping 192.168.68.134        -> 100% packet loss
curl http://192.168.68.134/ -> couldn't connect
arp -n 192.168.68.134      -> at 14:c1:9f:a:3b:78   (resolves fine)
```

## Cause

**AP client isolation.** The access point forwards broadcast traffic but drops unicast
station-to-station frames. ARP is broadcast, so the MAC caches and the host *looks*
routable; the sending kernel then returns `EHOSTUNREACH`.

Confirmed with a UDP probe that separates the two cases -- a listener on the device, a
sender on the laptop:

```
8 subnet broadcasts to 192.168.68.255:9999  -> all 8 received by the device
unicast to the device                        -> never left the sender
```

That test also **proved inbound delivery works** through the device's network stack, which
had been the worrying alternative explanation.

Both office SSIDs tested behaved this way.

## Fix

Run the device as its own access point as well as a station (`WIFI_MODE_APSTA`): it keeps
its uplink for API calls while broadcasting a network a phone or laptop can attach to
directly. No router change needed, and it survives the device moving networks.

Requirements learned while wiring it:
- Create **both** netifs before starting Wi-Fi.
- Apply **both** configs before starting, not after.
- Set the AP channel to 0 so it follows the station's channel (one shared radio).
- A WPA2 AP password must be **>= 8 characters** or the AP silently never starts.

Other options: disable client isolation for that SSID, or use a wired connection.

## Tried and did not work

- Moving both devices to the same SSID, same band, same channel. Still blocked.
- Rewriting the server. It was never the problem -- the loopback self-test proved it
  served correct bytes throughout.

## Prevention

When a device-hosted server is unreachable but the device's own outbound traffic works,
run the broadcast-vs-unicast UDP probe **before** touching the server code. It separates
a network policy from a firmware fault in about 30 seconds.

And test a server by actually fetching from it -- "it started without error" is not
evidence it serves. A sibling project was marked done on that basis and had never served
a byte.
