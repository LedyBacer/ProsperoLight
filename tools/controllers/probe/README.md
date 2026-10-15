# Native actuator PCM probe

Development payload only; never launched by production or automatically on stream
startup. Build with the existing PS5 Payload SDK:

```sh
make -C tools/controllers/probe PROBE_SIGNAL=0
```

This targets vibration port 10. It opens a native pad for the foreground user
(pad port type 0), selects haptics mode 1 if successful, and restores legacy mode 2
before releasing its pad. This uses the foreground logged-in user's port, index 0, grain 256, 48000 Hz, format
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

## Windows transport source

During a stream, run `python tools/controllers/waveform_source.py` in the Windows
user session to list output devices. To explicitly test one four-channel DualSense
composite endpoint, use `--device-id N --play`. There is no default-device mapper
or stereo fallback. Channels 1/2 are zero; channels 3/4 contain left, right, both
(160ms each, peak 1000/32768), followed by silence. The tool uses the existing
Vibepollo endpoint rather than injecting controller/network packets. `completed`
means the Windows audio buffer was returned, not physical actuator acceptance.
For an operator-requested, easier-to-distinguish sequence, explicitly add
`--strong-long`: left/right/both for 1.5s each, separated by 1s of silence,
peak 6000/32768 (about 18%), and 0.5s trailing silence. No automatic retry or
amplitude escalation is performed.

API references: [waveOutOpen](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutopen),
[WAVEHDR lifetime](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-wavehdr).
A missing endpoint, unsupported four-channel format or ambiguous identity is a
reported precondition failure; do not route this signal to TV/default audio.

WinMM legacy device caps can report two channels for this endpoint. Selection
therefore queries the exact four-channel 48kHz S16 `WAVEFORMATEXTENSIBLE` PCM
format with `WAVE_FORMAT_QUERY | WAVE_FORMAT_DIRECT`; it never treats a legacy
channel-count field alone as capability proof.
