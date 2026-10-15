/* ps5-native-app-boilerplate - Explicit, amplitude-limited AudioOut port probe.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifndef PROBE_SIGNAL
#define PROBE_SIGNAL 0
#endif
#define PROBE_PORT 10
extern int sceUserServiceInitialize(const void *);
extern int sceUserServiceGetForegroundUser(int32_t *);
extern int scePadInit(void);
extern int scePadOpen(int32_t, int, int, const void *);
extern int scePadReadState(int, void *);
extern int scePadClose(int);
extern int scePadSetVibrationMode(int, int);
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int32_t, int, int, unsigned, unsigned, unsigned);
extern int sceAudioOutOutput(int, const void *);
extern int sceAudioOutGetPortState(int, void *);
extern int sceAudioOutClose(int);
struct State
{
    uint16_t output;
    uint8_t channels, reserved;
    int16_t volume;
    uint16_t reroutes;
    uint64_t flags, unused[2];
};
int main(void)
{
    // Port-open/silence is the default. Nonzero synthesized output requires an explicit build flag.
    int32_t user = -1;
    int init_user = sceUserServiceInitialize(NULL);
    int user_rc = sceUserServiceGetForegroundUser(&user);
    int pad_init = scePadInit();
    int pad = user_rc >= 0 ? scePadOpen(user, 0, 0, NULL) : user_rc;
    _Alignas(16) uint8_t pad_sample[120] = {0};
    int pad_read = pad >= 0 ? scePadReadState(pad, pad_sample) : pad;
    int32_t connected = 0;
    memcpy(&connected, pad_sample + 0x4c, sizeof(connected));
    int mode = pad >= 0 ? scePadSetVibrationMode(pad, 1) : pad;
    int init = sceAudioOutInit();
    int port = user_rc >= 0 ? sceAudioOutOpen(user, PROBE_PORT, 0, 256, 48000, 1) : user_rc;
    struct State state = {0};
    int state_rc = port >= 0 ? sceAudioOutGetPortState(port, &state) : port;
    int output_rc = port, close_rc = port;
    _Alignas(64) int16_t pcm[512] = {0};
    if (port >= 0)
    {
        // Three 160ms segments, 1/32 full scale; integer triangle, no copied third-party DSP.
        for (unsigned segment = 0; segment < (PROBE_SIGNAL ? 3u : 1u) && output_rc >= 0; ++segment)
            for (unsigned block = 0; block < (PROBE_SIGNAL ? 30u : 1u); ++block)
            {
                memset(pcm, 0, sizeof(pcm));
                for (unsigned f = 0; PROBE_SIGNAL && f < 256; ++f)
                {
                    unsigned phase = (block * 256 + f) % 400;
                    int16_t sample =
                        (int16_t)((phase < 200 ? (int)phase : 400 - (int)phase) * 10 - 1000);
                    pcm[f * 2] = segment != 1 ? sample : 0;
                    pcm[f * 2 + 1] = segment != 0 ? sample : 0;
                }
                output_rc = sceAudioOutOutput(port, pcm);
                if (output_rc < 0)
                    break;
                usleep(5333);
            }
        memset(pcm, 0, sizeof(pcm));
        (void)sceAudioOutOutput(port, pcm);
        close_rc = sceAudioOutClose(port);
    }
    int mode_restore = pad >= 0 ? scePadSetVibrationMode(pad, 2) : pad;
    int pad_close = pad >= 0 ? scePadClose(pad) : pad;
    FILE *file = fopen("/data/prosperolight/waveform-probe.json", "w");
    if (file)
    {
        fprintf(file,
                "{\"port_type\":%d,\"grain\":256,\"rate\":48000,\"signal_enabled\":%s,\"native_"
                "mode_rc\":%d,\"mode_restore_rc\":%d,\"pad_init\":%d,\"pad_open\":%d,\"pad_read\":%"
                "d,\"pad_connected\":%d,\"pad_close\":%d,\"user_init\":%d,\"user_rc\":%d,\"init_"
                "rc\":%d,\"open_rc\":%d,\"state_rc\":%d,\"channels\":%u,\"output_rc\":%d,\"close_"
                "rc\":%d,\"physical_actuator_verified\":false}\n",
                PROBE_PORT, PROBE_SIGNAL ? "true" : "false", mode, mode_restore, pad_init, pad,
                pad_read, connected, pad_close, init_user, user_rc, init, port, state_rc,
                state.channels, output_rc, close_rc);
        fclose(file);
    }
    return port < 0 || state_rc < 0 || output_rc < 0 || close_rc < 0;
}
