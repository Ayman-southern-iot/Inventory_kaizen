# 403 FORBIDDEN_NETWORK although the client's IP is allowlisted

| | |
|---|---|
| **Domain** | `voice-search` |
| **Date** | 2026-10-05 |
| **Task** | [2026-10-05-esp32-p4-voice-firmware](../tasks/2026-10-05-esp32-p4-voice-firmware/README.md) |

## Symptom

```
GET http://10.10.8.51:8099/health
-> 403 {"code":"FORBIDDEN_NETWORK","message":"Not allowed from this address."}
```

Returned **identically with and without a valid token**, and identically from a device
whose IP *was* on the allowlist and one that was not.

## Cause

The service allowlisted the client's **LAN** address (`192.168.20.118`), but the request
crosses two routers to reach `10.10.8.x` and is NAT-translated on the way:

```
192.168.68.134  ->  192.168.68.100  ->  192.168.20.1  ->  10.10.8.51
```

The server's log showed the source as the office **public** NAT address
(`27.147.252.161`) -- never the client's own IP. Confirmed because two different clients
on the same network, only one allowlisted, received the same 403.

A 403 that is identical with and without credentials also tells you the IP check runs
**before** authentication, so it masks any token problem underneath.

## Fix

Allowlist the address the server actually observes -- read it from the access log rather
than assuming. Trigger a request with a findable marker:

```bash
curl -A "probe-$(date +%H%M%S)" "http://host:8099/health?probe=$(date +%H%M%S)"
# then: docker logs voice-svc | grep probe-
```

## Tried and did not work

- Moving both devices to a different SSID. The egress address was unchanged; the client's
  own IP is never what the server sees.
- Assuming the token was wrong. The 403 precedes auth, so the token could not be judged
  at all until the IP check passed.

## Prevention

**Behind NAT an IP allowlist is not a security boundary.** Every machine in the office
presents one address, so the filter cannot distinguish one device from another -- it only
creates this failure mode. The bearer token is the real access control. If per-host
identity is needed, issue a token per client.

Also: never key anything long-lived on a DHCP address. In one afternoon this board held
`192.168.20.118` then `192.168.68.134`, and the laptop moved `.149` -> `.199` -> a
different subnet, simply by changing Wi-Fi band.
