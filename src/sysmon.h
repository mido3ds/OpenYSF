#pragma once

#include <cstdint>
#include <chrono>
#include <algorithm>

#include <mu/utils.h>

struct SysInfo {
	mu::Str name;
	bool enabled;
	uint64_t latency_micros, latency_micros_min, latency_micros_max, latency_micros_avg;
	uint64_t num_calls;
};

// systems performance monitor
struct SysMon {
	mu::Vec<SysInfo> systems;
};

#ifdef DEBUG
	// called once per system
	inline int _sysmon_register_system(SysMon& self, mu::StrView&& system_name) {
		self.systems.push_back(SysInfo {
			.name = mu::Str(system_name),
			.enabled = true,
			.latency_micros_min = UINT64_MAX,
			.latency_micros_max = 0,
		});
		return self.systems.size()-1;
	}

	inline void _sysinfo_update(SysInfo& self, auto start_time) {
		self.latency_micros = std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::high_resolution_clock::now() - start_time
		).count();

		self.latency_micros_avg = double(self.num_calls * self.latency_micros_avg + self.latency_micros) / (self.num_calls+1);
		self.num_calls++;

		self.latency_micros_max = std::max(self.latency_micros, self.latency_micros_max);
		self.latency_micros_min = std::min(self.latency_micros, self.latency_micros_min);
	}

	#ifndef __FUNCTION_NAME__
		#ifdef WIN32   // WINDOWS
			#define __FUNCTION_NAME__   __FUNCTION__
		#else          // OTHER
			#define __FUNCTION_NAME__   __func__
		#endif
	#endif

	#define DEF_SYSTEM																					\
		static const auto __sysmon_index = _sysmon_register_system(world.sysmon, __FUNCTION_NAME__);	\
		if (world.sysmon.systems[__sysmon_index].enabled == false) { return; }							\
		const auto __sysmon_start = std::chrono::high_resolution_clock::now();							\
		mu_defer(_sysinfo_update(world.sysmon.systems[__sysmon_index], __sysmon_start));
#else
	#define DEF_SYSTEM(_) void();
#endif
