# Tailscale LAN WebSocket Proxy

Use this when a remote brain server, such as a Hetzner box, needs to talk to a local StackChan without exposing StackChan or changing firmware.

## Topology

```text
Hetzner brain --Tailscale--> LAN proxy --LAN Wi-Fi--> StackChan
```

The proxy preserves the existing Stacky protocol unchanged. It forwards WebSocket text and binary frames both directions and closes both sides together.

## Run The Proxy

Run this on a LAN machine that can reach StackChan and is joined to your Tailscale tailnet:

```bash
STACKY_PROXY_TARGET_WS_URL=ws://STACKCHAN_LAN_IP:6001/stacky/device \
STACKY_PROXY_PORT=6002 \
bun run proxy:stacky
```

The proxy listens on `ws://0.0.0.0:6002/stacky/device` by default.

## Point Hetzner At The Proxy

On the Hetzner brain server, set `STACKY_DEVICE_WS_URL` to the proxy's Tailscale address:

```dotenv
STACKY_DEVICE_WS_URL=ws://LAN-MACHINE-MAGICDNS:6002/stacky/device
```

You can also use the LAN machine's Tailscale `100.x.y.z` address.

## Notes

- Tailscale provides the private authenticated network path; the proxy does not add an application token or modify frames.
- StackChan only sees a normal local WebSocket client from the LAN proxy.
- Hetzner only needs Tailscale reachability to the proxy, not direct LAN reachability to StackChan.
- Keep `STACKY_PUBLIC_BASE_URL` on the Hetzner brain set to a URL StackChan can fetch for audio. If StackChan cannot reach Hetzner directly, serve audio through a similar LAN/Tailscale HTTP path or keep the brain audio endpoint LAN-reachable.
