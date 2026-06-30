#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <glm/glm.hpp>
#include <mu/utils.h>

// for stream at callback, bigger is slower, should be power of 2
constexpr auto AUDIO_STREAM_SIZE = 512;
constexpr int AUDIO_FREQUENCY = 22050;
constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_U16SYS;
constexpr uint8_t AUDIO_CHANNELS = 2;

struct AudioBuffer {
	mu::Str file_path;
	uint8_t* data;
	uint32_t len;
};

inline AudioBuffer audio_buffer_from_wav(mu::StrView filename) {
	AudioBuffer self { .file_path = mu::Str(filename) };
	SDL_AudioSpec spec {};
	if (SDL_LoadWAV(filename.data(), &spec, &self.data, &self.len) == nullptr) {
        mu::panic("failed to open wave file '{}', err: {}", filename, SDL_GetError());
    }

	// convert
	SDL_AudioCVT cvt;
	if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_FORMAT, AUDIO_CHANNELS, AUDIO_FREQUENCY) < 0) {
		mu::panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
	}
	if (cvt.needed) {
		cvt.len = self.len;
		cvt.buf = (Uint8*) SDL_malloc(cvt.len * cvt.len_mult);
		SDL_memcpy(cvt.buf, self.data, cvt.len);

		if (SDL_ConvertAudio(&cvt) < 0) {
			mu::panic("failed to convert audio '{}', err: {}", filename, SDL_GetError());
		}

		SDL_FreeWAV(self.data);
		self.data = cvt.buf;
		self.len = cvt.len_cvt;
	}

	return self;
}

inline void audio_buffer_free(AudioBuffer& self) {
	SDL_FreeWAV(self.data);
}

struct AudioPlayback {
	const AudioBuffer* audio;
	uint32_t pos;
	float gain = 1.0f;
	uint64_t id = 0;
};

struct AudioDevice {
    SDL_AudioDeviceID id;
	mu::Vec<AudioPlayback> playbacks, looped_playbacks;
	uint64_t next_playback_id = 1;
};

constexpr uint32_t min_u32(uint32_t a, uint32_t b) {
	return a < b ? a : b;
}

inline void audio_device_init(AudioDevice* self) {
	const SDL_AudioSpec spec {
		.freq = AUDIO_FREQUENCY,
		.format = AUDIO_FORMAT,
		.channels = AUDIO_CHANNELS,
		.samples = AUDIO_STREAM_SIZE,
		.callback = [](void* userdata, uint8_t* stream, int stream_len_int) {
			auto dev = (AudioDevice*) userdata;
			const uint32_t stream_len = stream_len_int;
			const uint32_t num_u16 = stream_len / sizeof(uint16_t);

			float accum[1024];
			mu_assert(num_u16 <= 1024);
			for (uint32_t i = 0; i < num_u16; i++)
				accum[i] = 0.0f;

			// one-shot playbacks
			for (auto& playback : dev->playbacks) {
				mu_assert(playback.pos < playback.audio->len);

				uint32_t to_mix = min_u32(stream_len, playback.audio->len - playback.pos);
				uint32_t num = to_mix / sizeof(uint16_t);
				float gain = glm::clamp(playback.gain, 0.0f, 1.0f);
				uint16_t* src = (uint16_t*)(playback.audio->data + playback.pos);
				for (uint32_t i = 0; i < num; i++) {
					float f = ((int)src[i] - 32768) / 32767.0f;
					accum[i] += f * gain;
				}
				playback.pos += to_mix;
			}
			for (int i = dev->playbacks.size()-1; i >= 0; i--) {
				mu_assert(dev->playbacks[i].pos <= dev->playbacks[i].audio->len);
				if (dev->playbacks[i].pos == dev->playbacks[i].audio->len)
					mu::vec_remove_unordered(dev->playbacks, i);
			}

			// looped playbacks
			for (auto& playback : dev->looped_playbacks) {
				mu_assert(playback.pos < playback.audio->len);

				float gain = glm::clamp(playback.gain, 0.0f, 1.0f);
				uint32_t stream_byte_pos = 0;
				while (stream_byte_pos < stream_len) {
					uint32_t to_mix = min_u32(stream_len - stream_byte_pos, playback.audio->len - playback.pos);
					uint32_t num = to_mix / sizeof(uint16_t);
					uint32_t accum_idx = stream_byte_pos / sizeof(uint16_t);
					uint16_t* src = (uint16_t*)(playback.audio->data + playback.pos);
					for (uint32_t i = 0; i < num; i++) {
						float f = ((int)src[i] - 32768) / 32767.0f;
						accum[accum_idx + i] += f * gain;
					}
					stream_byte_pos += to_mix;
					playback.pos += to_mix;
					playback.pos %= playback.audio->len;
				}
			}

			// convert float accum back to U16
			for (uint32_t i = 0; i < num_u16; i++) {
				float s = glm::clamp(accum[i], -1.0f, 1.0f);
				((uint16_t*)stream)[i] = (uint16_t)((int)(s * 32767.0f) + 32768);
			}
		},
		.userdata = self,
	};

	self->id = SDL_OpenAudioDevice(nullptr, false, &spec, nullptr, 0);
    if (self->id == 0) {
        mu::panic("failed to open audio device: {}", SDL_GetError());
    }

	self->playbacks.reserve(32);
	self->looped_playbacks.reserve(32);

	// unpause
	SDL_PauseAudioDevice(self->id, false);
}

inline void audio_device_free(AudioDevice& self) {
	SDL_PauseAudioDevice(self.id, true);
#ifndef OS_MACOS
	// macOS: SDL_CloseAudioDevice blocks on CoreAudio AudioUnitUninitialize (many ms).
	// SDL_Quit also triggers the same slow teardown, so both slow.
	// Guarding both so the coreaudio close happens only in SDL_Quit (and even that
	// is skipped — see sdl_free). On macOS we just pause and let the process exit;
	// CoreAudio handles cleanup at process death without the long wait.
	SDL_CloseAudioDevice(self.id);
#endif
}

// `audio` must be alive as long as it's played
inline void audio_device_play(AudioDevice& self, const AudioBuffer& audio) {
	SDL_LockAudioDevice(self.id);
	self.playbacks.push_back(AudioPlayback { .audio = &audio });
    SDL_UnlockAudioDevice(self.id);
}

// `audio` must be alive as long as it's played
inline uint64_t audio_device_play_looped(AudioDevice& self, const AudioBuffer& audio) {
	SDL_LockAudioDevice(self.id);
	uint64_t id = self.next_playback_id++;
	self.looped_playbacks.push_back(AudioPlayback{&audio, 0, 1.0f, id});
	SDL_UnlockAudioDevice(self.id);
	return id;
}

inline void audio_device_set_gain(AudioDevice& self, uint64_t playback_id, float gain) {
	SDL_LockAudioDevice(self.id);
	for (auto& pb : self.looped_playbacks) {
		if (pb.id == playback_id) {
			pb.gain = gain;
			break;
		}
	}
	SDL_UnlockAudioDevice(self.id);
}

inline void audio_device_stop_by_id(AudioDevice& self, uint64_t playback_id) {
	SDL_LockAudioDevice(self.id);
	for (int i = self.looped_playbacks.size() - 1; i >= 0; i--) {
		if (self.looped_playbacks[i].id == playback_id) {
			mu::vec_remove_unordered(self.looped_playbacks, i);
			break;
		}
	}
	SDL_UnlockAudioDevice(self.id);
}

inline bool audio_device_is_playing(const AudioDevice& self, const AudioBuffer& audio) {
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

inline void audio_device_stop(AudioDevice& self, const AudioBuffer& audio) {
    SDL_LockAudioDevice(self.id);
	mu_defer(SDL_UnlockAudioDevice(self.id));

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

	mu::log_warning("didn't find audio '{}' to stop", mu::file_get_base_name(audio.file_path));
}
