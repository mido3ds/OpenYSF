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

// calculated from original assets sound/silence.wav after converting to stereo/22050HZ/16LSB
constexpr uint16_t AUDIO_SILENCE_VALUE = 0x7FFF;

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

	// convert
	SDL_AudioCVT cvt;
	if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_FORMAT, AUDIO_CHANNELS, AUDIO_FREQUENCY) < 0) {
		mn::panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
	}
	if (cvt.needed) {
		cvt.len = self.len;
		cvt.buf = (Uint8*) SDL_malloc(cvt.len * cvt.len_mult);
		SDL_memcpy(cvt.buf, self.buffer, cvt.len);

		if (SDL_ConvertAudio(&cvt) < 0) {
			mn::panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
		}

		SDL_FreeWAV(self.buffer);
		self.buffer = cvt.buf;
		self.len = cvt.len_cvt;
	}

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

constexpr uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

void memset_u16(void* dst, uint16_t val, size_t len) {
	mn_assert(len % 2 == 0);
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
				mn_assert(playback.pos < playback.audio->len);

				const uint32_t min_len = min_u32(stream_len, playback.audio->len - playback.pos);
				SDL_MixAudioFormat(stream, playback.audio->buffer+playback.pos, AUDIO_FORMAT, min_len, SDL_MIX_MAXVOLUME);
				playback.pos += min_len;

				silence_pos = min_u32(silence_pos + min_len, stream_len);
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
- mixing anything with propoller isn't loud enough

TODO:
- don't use SDL_MixAudioFormat
- what to do with multiple playbacks of same sound? (ignore new? increase volume unlimited? increase volume within limit? ??)
- use silence.wav to get correct silence value dynamically?
*/
