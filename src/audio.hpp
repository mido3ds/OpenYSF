#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "utils.hpp"

// for stream at callback, bigger is slower, should be power of 2
constexpr auto AUDIO_STREAM_SIZE = 512;
constexpr int AUDIO_FREQUENCY = 22050;
constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_U16SYS;
constexpr uint8_t AUDIO_CHANNELS = 2;

// calculated from original assets sound/silence.wav after converting to stereo/22050HZ/16LSB
constexpr uint16_t AUDIO_SILENCE_VALUE = 0x7FFF;

struct Audio {
	Str file_path;
	uint8_t* buffer;
	uint32_t len;
};

Audio audio_new(StrView filename) {
	Audio self { .file_path = Str(filename) };
	SDL_AudioSpec spec {};
	if (SDL_LoadWAV(filename.data(), &spec, &self.buffer, &self.len) == nullptr) {
        panic("failed to open wave file '{}', err: {}", filename, SDL_GetError());
    }

	// convert
	SDL_AudioCVT cvt;
	if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_FORMAT, AUDIO_CHANNELS, AUDIO_FREQUENCY) < 0) {
		panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
	}
	if (cvt.needed) {
		cvt.len = self.len;
		cvt.buf = (Uint8*) SDL_malloc(cvt.len * cvt.len_mult);
		SDL_memcpy(cvt.buf, self.buffer, cvt.len);

		if (SDL_ConvertAudio(&cvt) < 0) {
			panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
		}

		SDL_FreeWAV(self.buffer);
		self.buffer = cvt.buf;
		self.len = cvt.len_cvt;
	}

	return self;
}

void audio_free(Audio& self) {
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
	Vec<AudioPlayback> playbacks, looped_playbacks;
};

constexpr uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

void memset_u16(void* dst, uint16_t val, size_t len) {
	my_assert(len % 2 == 0);
	uint16_t* dst_as_16 = (uint16_t*) dst;
	const size_t len_as_16 = len / 2;
	for (size_t i = 0; i < len_as_16; i++) {
		dst_as_16[i] = val;
	}
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
			uint32_t silence_pos = 0;

			SDL_memset(stream, 0, stream_len);

			// one shot
			for (auto& playback : dev->playbacks) {
				my_assert(playback.pos < playback.audio->len);

				const uint32_t min_len = min_u32(stream_len, playback.audio->len - playback.pos);
				SDL_MixAudioFormat(stream, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);
				playback.pos += min_len;

				silence_pos = min_u32(silence_pos + min_len, stream_len);
			}
			for (int i = dev->playbacks.size()-1; i >= 0; i--) {
				my_assert(dev->playbacks[i].pos <= dev->playbacks[i].audio->len);
				if (dev->playbacks[i].pos == dev->playbacks[i].audio->len) {
					vec_remove_unordered(dev->playbacks, i);
				}
			}

			// looped
			for (auto& playback : dev->looped_playbacks) {
				my_assert(playback.pos < playback.audio->len);

				uint32_t stream_pos = 0;
				while (stream_pos < stream_len) {
					const uint32_t min_len = min_u32(stream_len - stream_pos, playback.audio->len - playback.pos);
					SDL_MixAudioFormat(stream+stream_pos, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);
					stream_pos += min_len;
					playback.pos += min_len;
					playback.pos %= playback.audio->len;
				}

				silence_pos = stream_len;
			}

			// silence the rest of stream
			if (silence_pos < stream_len) {
				memset_u16(stream+silence_pos, AUDIO_SILENCE_VALUE, stream_len-silence_pos);
			}
		},
		.userdata = self,
	};

	self->id = SDL_OpenAudioDevice(nullptr, false, &spec, nullptr, 0);
    if (self->id == 0) {
        panic("failed to open audio device: {}", SDL_GetError());
    }

	self->playbacks.reserve(32);
	self->looped_playbacks.reserve(32);

	// unpause
	SDL_PauseAudioDevice(self->id, false);
}

void audio_device_free(AudioDevice& self) {
	SDL_PauseAudioDevice(self.id, true);
	SDL_CloseAudioDevice(self.id);
}

// `audio` must be alive as long as it's played
void audio_device_play(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	self.playbacks.push_back(AudioPlayback { .audio = &audio });
    SDL_UnlockAudioDevice(self.id);
}

// `audio` must be alive as long as it's played
void audio_device_play_looped(AudioDevice& self, const Audio& audio) {
    SDL_LockAudioDevice(self.id);
	self.looped_playbacks.push_back(AudioPlayback { .audio = &audio });
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
	defer(SDL_UnlockAudioDevice(self.id));

	for (size_t i = 0; i < self.playbacks.size(); i++) {
		if (self.playbacks[i].audio == &audio) {
			self.playbacks.erase(self.playbacks.begin()+i);
			return;
		}
	}

	for (size_t i = 0; i < self.looped_playbacks.size(); i++) {
		if (self.looped_playbacks[i].audio == &audio) {
			self.looped_playbacks.erase(self.looped_playbacks.begin()+i);
			return;
		}
	}

	log_warning("didn't find audio '{}' to stop", mn::file_name(audio.file_path.c_str(), mn::memory::tmp()));
}

/*
TODO:
- mixing anything with propoller isn't loud enough
- don't use SDL_MixAudioFormat
- what to do with multiple playbacks of same sound? (ignore new? increase volume unlimited? increase volume within limit? ??)
- use silence.wav to get correct silence value dynamically?
*/
