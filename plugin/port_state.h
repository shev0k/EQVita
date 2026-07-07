#pragma once

#include <stdint.h>

#define EQ_AUDIO_KEEP_U32 UINT32_MAX
#define EQ_AUDIO_PORT_TYPE_MAIN 0u
#define EQ_AUDIO_PORT_TYPE_BGM 1u
#define EQ_AUDIO_PORT_TYPE_VOICE 2u
#define EQ_AUDIO_BGM_PORT_ID 256
#define EQ_AUDIO_MODE_MONO 0
#define EQ_AUDIO_MODE_STEREO 1
#define EQ_AUDIO_MIN_LEN 64u
#define EQ_AUDIO_MAX_LEN 65472u

typedef struct eq_audio_port_config
{
    uint8_t in_use;
    uint8_t channels;
    uint8_t reserved0;
    uint8_t reserved1;
    uint32_t type;
    uint32_t len;
    uint32_t freq;
} eq_audio_port_config_t;

static inline int eq_audio_mode_to_channels(int mode)
{
    if (mode == EQ_AUDIO_MODE_MONO) {
        return 1;
    }
    if (mode == EQ_AUDIO_MODE_STEREO) {
        return 2;
    }
    return -1;
}

static inline uint32_t eq_audio_port_type_for_recovered_id(int port_id)
{
    if (port_id == EQ_AUDIO_BGM_PORT_ID) {
        return EQ_AUDIO_PORT_TYPE_BGM;
    }
    return EQ_AUDIO_PORT_TYPE_MAIN;
}

static inline int eq_audio_freq_supported(uint32_t freq)
{
    switch (freq) {
    case 8000u:
    case 11025u:
    case 12000u:
    case 16000u:
    case 22050u:
    case 24000u:
    case 32000u:
    case 44100u:
    case 48000u:
        return 1;
    default:
        return 0;
    }
}

static inline int eq_audio_len_supported(uint32_t len)
{
    return len >= EQ_AUDIO_MIN_LEN &&
           len <= EQ_AUDIO_MAX_LEN &&
           (len % EQ_AUDIO_MIN_LEN) == 0;
}

static inline int eq_audio_port_open(eq_audio_port_config_t *cfg, uint32_t type, uint32_t len, uint32_t freq, int mode)
{
    int channels = eq_audio_mode_to_channels(mode);
    if (!cfg || channels < 0 || !eq_audio_len_supported(len) || !eq_audio_freq_supported(freq)) {
        if (cfg) {
            cfg->in_use = 0;
        }
        return -1;
    }

    cfg->in_use = 1;
    cfg->channels = (uint8_t)channels;
    cfg->type = type;
    cfg->len = len;
    cfg->freq = freq;
    return 0;
}

static inline int eq_audio_port_set_config(eq_audio_port_config_t *cfg, uint32_t len, int freq, int mode)
{
    uint32_t next_len;
    uint32_t next_freq;
    uint8_t next_channels;

    if (!cfg || !cfg->in_use) {
        return -1;
    }

    next_len = cfg->len;
    next_freq = cfg->freq;
    next_channels = cfg->channels;

    if (len != EQ_AUDIO_KEEP_U32) {
        if (!eq_audio_len_supported(len)) {
            return -1;
        }
        next_len = len;
    }

    if (freq != -1) {
        if (freq <= 0 || !eq_audio_freq_supported((uint32_t)freq)) {
            return -1;
        }
        next_freq = (uint32_t)freq;
    }

    if (mode != -1) {
        int channels = eq_audio_mode_to_channels(mode);
        if (channels < 0) {
            return -1;
        }
        next_channels = (uint8_t)channels;
    }

    cfg->len = next_len;
    cfg->freq = next_freq;
    cfg->channels = next_channels;
    return 0;
}

static inline int eq_audio_port_can_process(const eq_audio_port_config_t *cfg, uint32_t scratch_max_frames)
{
    if (!cfg || !cfg->in_use) {
        return 0;
    }
    if (cfg->channels < 1 || cfg->channels > 2) {
        return 0;
    }
    if (!eq_audio_len_supported(cfg->len) || cfg->len > scratch_max_frames) {
        return 0;
    }
    return eq_audio_freq_supported(cfg->freq);
}
