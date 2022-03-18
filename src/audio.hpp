#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <mn/OS.h>
#include <mn/Log.h>
#include <mn/Str.h>
#include <mn/Defer.h>

constexpr int MAX_PLAYABLE_SOUNDS = 32;
constexpr int AUDIO_FREQUENCY = 22000;
constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_S16MSB;
constexpr uint8_t AUDIO_CHANNELS = 2;

// size of stream at callback (bigger is slower), should be power of 2
constexpr auto AUDIO_SAMPLES = 512;

struct Audio {
	mn::Str file_path;
	uint8_t* buffer;
	uint32_t len;
};

Audio audio_new(const char* filename) {
	Audio self { .file_path = mn::str_from_c(filename) };
	SDL_AudioSpec spec {};
	if (SDL_LoadWAV(filename, &spec, &self.buffer, &self.len) == nullptr) {
        mn::panic("failed to open wave file '{}', err: {}", filename, SDL_GetError());
    }

	// resample
	auto stream = SDL_NewAudioStream(spec.format, spec.channels, spec.freq, AUDIO_FORMAT, AUDIO_CHANNELS, AUDIO_FREQUENCY);
	if (stream == nullptr) {
		mn::panic("failed to resample '{}'", filename);
	}

	if (SDL_AudioStreamPut(stream, self.buffer, self.len)) {
		mn::panic("failed to resample '{}'", filename);
	}

	if (SDL_AudioStreamFlush(stream)) {
		mn::panic("failed to resample '{}'", filename);
	}

	self.len = SDL_AudioStreamAvailable(stream);
	SDL_FreeWAV(self.buffer);
	self.buffer = (uint8_t*) SDL_malloc(self.len);

	if (SDL_AudioStreamGet(stream, self.buffer, self.len) != self.len) {
		mn::panic("failed to resample '{}'", filename);
	}

	SDL_FreeAudioStream(stream);

	return self;
}

void audio_free(Audio& self) {
	mn::str_free(self.file_path);
	SDL_FreeWAV(self.buffer);
}

void destruct(Audio& self) {
	audio_free(self);
}

struct AudioPlayback {
	const Audio* audio;
	uint32_t pos;
};

struct AudioDevice {
    SDL_AudioDeviceID id;

	AudioPlayback playbacks[MAX_PLAYABLE_SOUNDS];
	int playbacks_count;

	AudioPlayback looped_playbacks[MAX_PLAYABLE_SOUNDS];
	int looped_playbacks_count;
};

void audio_device_init(AudioDevice* self) {
	const SDL_AudioSpec spec {
		.freq = AUDIO_FREQUENCY,
		.format = AUDIO_FORMAT,
		.channels = AUDIO_CHANNELS,
		.samples = AUDIO_SAMPLES,
		.callback = [](void* userdata, uint8_t* stream, int stream_len) {
			auto dev = (AudioDevice*) userdata;
			mn_assert(dev->playbacks_count >= 0 && dev->playbacks_count <= MAX_PLAYABLE_SOUNDS);
			mn_assert(dev->looped_playbacks_count >= 0 && dev->looped_playbacks_count <= MAX_PLAYABLE_SOUNDS);

			// silence the main buffer
			::memset(stream, 0, stream_len);

			// one shot
			for (int i = dev->playbacks_count - 1; i >= 0; i--) {
				auto& playback = dev->playbacks[i];
				mn_assert(playback.audio);
				mn_assert(playback.pos < playback.audio->len);

				const uint32_t audio_current_len = playback.audio->len - playback.pos;
				const uint32_t min_len = ((uint32_t)stream_len < audio_current_len)? (uint32_t)stream_len : audio_current_len;
				SDL_MixAudioFormat(stream, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);

				playback.pos += min_len;

				mn_assert(playback.pos <= playback.audio->len);
				if (playback.pos == playback.audio->len) {
					playback = dev->playbacks[--dev->playbacks_count];
				}
			}

			// looped
			for (int i = 0; i < dev->looped_playbacks_count; i++) {
				auto& playback = dev->looped_playbacks[i];
				mn_assert(playback.audio);
				mn_assert(playback.pos < playback.audio->len);

				const uint32_t audio_current_len = playback.audio->len - playback.pos;
				const uint32_t min_len = ((uint32_t)stream_len < audio_current_len)? (uint32_t)stream_len : audio_current_len;
				SDL_MixAudioFormat(stream, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);

				playback.pos = (playback.pos + min_len) % playback.audio->len;
			}
		},
		.userdata = self,
	};

	self->id = SDL_OpenAudioDevice(nullptr, false, &spec, nullptr, 0);
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

// `audio` must be alive as long as it's played
void audio_device_play(AudioDevice& self, const Audio& audio) {
	if (self.playbacks_count == MAX_PLAYABLE_SOUNDS) {
		mn::log_warning("full audio buffer");
		return;
	}

    SDL_LockAudioDevice(self.id);
	self.playbacks[self.playbacks_count++] = AudioPlayback { .audio = &audio };
    SDL_UnlockAudioDevice(self.id);
}

// `audio` must be alive as long as it's played
void audio_device_play_looped(AudioDevice& self, const Audio& audio) {
	if (self.looped_playbacks_count == MAX_PLAYABLE_SOUNDS) {
		mn::log_warning("full audio buffer");
		return;
	}

    SDL_LockAudioDevice(self.id);
	self.looped_playbacks[self.looped_playbacks_count++] = AudioPlayback { .audio = &audio };
    SDL_UnlockAudioDevice(self.id);
}

bool audio_device_is_playing(const AudioDevice& self, const Audio& audio) {
	// one shot
	for (int i = 0; i < self.playbacks_count; i++) {
		if (self.playbacks[i].audio == &audio) {
			return true;
		}
	}

	// looped
	for (int i = 0; i < self.looped_playbacks_count; i++) {
		if (self.looped_playbacks[i].audio == &audio) {
			return true;
		}
	}

	return false;
}

void audio_device_stop(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	mn_defer(SDL_UnlockAudioDevice(self.id));

	// one shot
	for (int i = 0; i < self.playbacks_count; i++) {
		if (self.playbacks[i].audio == &audio) {
			self.playbacks[i] = self.playbacks[--self.playbacks_count];
			return;
		}
	}

	// looped
	for (int i = 0; i < self.looped_playbacks_count; i++) {
		if (self.looped_playbacks[i].audio == &audio) {
			self.looped_playbacks[i] = self.looped_playbacks[--self.looped_playbacks_count];
			return;
		}
	}

	mn::log_warning("didn't find audio '{}' to stop", mn::file_name(audio.file_path, mn::memory::tmp()));
}

/*
BUG:
- gun sound is too short
- looping engine/prop isn't pleasing (maybe loop by copying all over buffer? maybe jump to 20% after loop?)

TODO:
- is silence 0?
- what to do with multiple playbacks of same sound? (ignore new? increase volume unlimited? increase volume within limit? ??)
*/
