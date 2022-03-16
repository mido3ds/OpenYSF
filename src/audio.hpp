#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <mn/OS.h>
#include <mn/Log.h>

constexpr auto AUDIO_FORMAT = AUDIO_U8;

/* Frequency of the file */
constexpr auto AUDIO_FREQUENCY = 8000;

/* 1 mono, 2 stereo, 4 quad, 6 (5.1) */
constexpr auto AUDIO_CHANNELS = 1;

/* Specifies a unit of audio data to be used at a time. Must be a power of 2 */
constexpr auto AUDIO_SAMPLES = 4096;

/* Flags OR'd together, which specify how SDL should behave when a device cannot offer a specific feature
 * If flag is set, SDL will change the format in the actual audio file structure (as opposed to dev.specs)
 *
 * Note: If you're having issues with Emscripten / EMCC play around with these flags
 *
 * 0                                    Allow no changes
 * SDL_AUDIO_ALLOW_FREQUENCY_CHANGE     Allow frequency changes (e.g. AUDIO_FREQUENCY is 48k, but allow files to play at 44.1k
 * SDL_AUDIO_ALLOW_FORMAT_CHANGE        Allow Format change (e.g. AUDIO_FORMAT may be S32LSB, but allow wave files of S16LSB to play)
 * SDL_AUDIO_ALLOW_CHANNELS_CHANGE      Allow any number of channels (e.g. AUDIO_CHANNELS being 2, allow actual 1)
 * SDL_AUDIO_ALLOW_ANY_CHANGE           Allow all changes above
 */
constexpr auto SDL_AUDIO_ALLOW_CHANGES = 0;

constexpr int MAX_PLAYABLE_SOUNDS = 32;

struct Audio {
	uint8_t* buffer;
	uint32_t len;
	SDL_AudioSpec specs;
	bool loop;
};

Audio audio_new(const char* filename, bool loop) {
	Audio self { .loop = loop };
	if (SDL_LoadWAV(filename, &self.specs, &self.buffer, &self.len) == nullptr) {
        mn::panic("failed to open wave file '{}', err: {}", filename, SDL_GetError());
    }
	return self;
}

void audio_free(Audio& self) {
	SDL_FreeWAV(self.buffer);
}

void destruct(Audio& self) {
	audio_free(self);
}

struct AudioSession {
	const Audio* audio;
	uint32_t pos;
};

struct AudioDevice {
    SDL_AudioDeviceID id;
    SDL_AudioSpec specs;

	AudioSession audio_sessions[MAX_PLAYABLE_SOUNDS];
	int audio_sessions_last;
};

void audio_device_init(AudioDevice* self) {
	self->specs = {
		.freq = AUDIO_FREQUENCY,
		.format = AUDIO_FORMAT,
		.channels = AUDIO_CHANNELS,
		.samples = AUDIO_SAMPLES,
		.callback = [](void* userdata, uint8_t* stream, int stream_len) {
			// silence the main buffer
			::memset(stream, 0, stream_len);

			auto dev = (AudioDevice*) userdata;
			for (int i = 0; i < MAX_PLAYABLE_SOUNDS; i++) {
				auto& session = dev->audio_sessions[i];
				if (session.audio == nullptr) {
					break;
				}

				const uint32_t audio_len = session.audio->len - session.pos;
				const uint32_t min_len = ((uint32_t)stream_len < session.audio->len)? (uint32_t)stream_len : session.audio->len;
				SDL_MixAudioFormat(stream, session.audio->buffer+session.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);

				session.pos += min_len;
				if (session.pos == session.audio->len) {
					if (session.audio->loop) {
						session.pos = 0;
					} else {
						session.audio = nullptr;
						session = dev->audio_sessions[--dev->audio_sessions_last];
						i--;
					}
				}
			}
		},
		.userdata = self,
	};

	self->id = SDL_OpenAudioDevice(nullptr, false, &self->specs, nullptr, SDL_AUDIO_ALLOW_CHANGES);
    if (self->id == 0) {
        mn::panic("failed to open audio device: {}", SDL_GetError());
    }

	// unpause
	SDL_PauseAudioDevice(self->id, false);
}

void audio_device_free(AudioDevice& dev) {
	SDL_PauseAudioDevice(dev.id, true);
	SDL_CloseAudioDevice(dev.id);
}

// makes shallow copy of `audio`, which must be alive as long as it's played
void audio_device_play(AudioDevice& self, const Audio& audio) {
	if (self.audio_sessions_last+1 == MAX_PLAYABLE_SOUNDS) {
		mn::log_warning("full audio buffer");
		return;
	}

    SDL_LockAudioDevice(self.id);
	self.audio_sessions[self.audio_sessions_last++] = AudioSession { .audio = &audio };
    SDL_UnlockAudioDevice(self.id);
}

/*
TODO:
- test for all files
- imgui: load any file
- why multiple clicks result in higher volume?
*/
