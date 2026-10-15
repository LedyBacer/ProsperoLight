# Native controller audio probe

Development payload only; never launched by production or automatically on stream
startup. Build with the existing PS5 Payload SDK:

```sh
make -C tools/controllers/probe PROBE_SIGNAL=0
```

This uses the foreground logged-in user's port, index 0, grain 256, 48000 Hz, format
1 (S16 stereo). It queries the public 32-byte port-state ABI, outputs silence and
closes. Read `/data/prosperolight/waveform-probe.json` through existing FTP.
Returned handles/state are API evidence, **not proof of physical actuator output**.
Close ProsperoLight normally before loading a probe. Use the configured Payload
Manager's normal ELF loader; do not run probes while another title owns audio.
A loader acceptance is not a probe completion receipt. Never retry a missing
receipt automatically: inspect the console/manager first.

Explicitly operator-triggered physical test:

```sh
make -C tools/controllers/probe PROBE_SIGNAL=1
```

This plays left only, right only, both, then silence/close. Each segment is about
160ms; the triangle is limited to approximately 1/32 full scale. State whether
actuator separation/polarity/detail was physically observed. The generated JSON
always leaves `physical_actuator_verified=false`; record an operator observation
separately. An unexpected stall/error does not justify escalating amplitude.

Speaker and microphone work is deferred. This probe targets only native actuator
PCM; it does not reroute or capture ordinary stream audio.
