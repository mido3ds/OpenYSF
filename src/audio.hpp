#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <mn/OS.h>
#include <mn/Log.h>
#include <mn/Str.h>
#include <mn/Buf.h>
#include <mn/Defer.h>

// for stream at callback, bigger is slower, should be power of 2
constexpr auto AUDIO_STREAM_SIZE = 512;
constexpr int AUDIO_FREQUENCY = 22050;
constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_U16SYS;
constexpr uint8_t AUDIO_CHANNELS = 2;

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
	mn::Buf<AudioPlayback> playbacks, looped_playbacks;
};

constexpr uint32_t min32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

void audio_device_init(AudioDevice* self) {
	const SDL_AudioSpec spec {
		.freq = AUDIO_FREQUENCY,
		.format = AUDIO_FORMAT,
		.channels = AUDIO_CHANNELS,
		.samples = AUDIO_STREAM_SIZE,
		.callback = [](void* userdata, uint8_t* stream, int stream_len_int) {
			auto dev = (AudioDevice*) userdata;
			const uint32_t stream_len = stream_len_int;

			// silence the main buffer
			::memset(stream, 0, stream_len);

			// one shot
			for (auto& playback : dev->playbacks) {
				mn_assert(playback.pos < playback.audio->len);

				const uint32_t min_len = min32(stream_len, playback.audio->len - playback.pos);
				SDL_MixAudioFormat(stream, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);
				playback.pos += min_len;
			}
			mn::buf_remove_if(dev->playbacks, [](const AudioPlayback& a) {
				mn_assert(a.pos <= a.audio->len);
				return a.pos == a.audio->len;
			});

			// looped
			for (auto& playback : dev->looped_playbacks) {
				mn_assert(playback.pos < playback.audio->len);

				uint32_t stream_pos = 0;
				while (stream_pos < stream_len) {
					const uint32_t min_len = min32(stream_len - stream_pos, playback.audio->len - playback.pos);
					SDL_MixAudioFormat(stream+stream_pos, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);
					stream_pos += min_len;
					playback.pos += min_len;
					playback.pos %= playback.audio->len;
				}
			}
		},
		.userdata = self,
	};

	self->id = SDL_OpenAudioDevice(nullptr, false, &spec, nullptr, 0);
    if (self->id == 0) {
        mn::panic("failed to open audio device: {}", SDL_GetError());
    }

	self->playbacks = mn::buf_with_capacity<AudioPlayback>(32);
	self->looped_playbacks = mn::buf_with_capacity<AudioPlayback>(32);

	// unpause
	SDL_PauseAudioDevice(self->id, false);
}

void audio_device_free(AudioDevice& self) {
	SDL_PauseAudioDevice(self.id, true);
	SDL_CloseAudioDevice(self.id);
	mn::buf_free(self.playbacks);
	mn::buf_free(self.looped_playbacks);
}

// `audio` must be alive as long as it's played
void audio_device_play(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	mn::buf_push(self.playbacks, AudioPlayback { .audio = &audio });
    SDL_UnlockAudioDevice(self.id);
}

// `audio` must be alive as long as it's played
void audio_device_play_looped(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	mn::buf_push(self.looped_playbacks, AudioPlayback { .audio = &audio });
    SDL_UnlockAudioDevice(self.id);
}

bool audio_device_is_playing(const AudioDevice& self, const Audio& audio) {
	for (const auto& playback : self.playbacks) {
		if (playback.audio == &audio) {
			return true;
		}
	}
	for (const auto& playback : self.looped_playbacks) {
		if (playback.audio == &audio) {
			return true;
		}
	}
	return false;
}

void audio_device_stop(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	mn_defer(SDL_UnlockAudioDevice(self.id));

	for (size_t i = 0; i < self.playbacks.count; i++) {
		if (self.playbacks[i].audio == &audio) {
			mn::buf_remove(self.playbacks, i);
			return;
		}
	}

	for (size_t i = 0; i < self.looped_playbacks.count; i++) {
		if (self.looped_playbacks[i].audio == &audio) {
			mn::buf_remove(self.looped_playbacks, i);
			return;
		}
	}

	mn::log_warning("didn't find audio '{}' to stop", mn::file_name(audio.file_path, mn::memory::tmp()));
}

/*
BUG:
- gun sound is too short
- mixing anything with propoller isn't loud enough

TODO:
- try
- is silence 0?
- what to do with multiple playbacks of same sound? (ignore new? increase volume unlimited? increase volume within limit? ??)
*/
