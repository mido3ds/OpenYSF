#pragma once

#include "containers.hpp"

struct ILogger {
	virtual void log_debug(StrView str) = 0;
	virtual void log_info(StrView str) = 0;
	virtual void log_warning(StrView str) = 0;
	virtual void log_error(StrView str) = 0;
};

extern ILogger* log_global_logger;

template<typename... TArgs>
inline static void
log_debug([[maybe_unused]] StrView fmt, [[maybe_unused]] TArgs&&... args) {
	#ifdef DEBUG
	if (log_global_logger) {
		log_global_logger->log_debug(str_tmpf(fmt, args...));
	} else {
		fmt::print("[debug] {}\n", str_tmpf(fmt, args...));
	}
	#endif
}

template<typename... TArgs>
inline static void
log_info(StrView fmt, TArgs&&... args) {
	if (log_global_logger) {
		log_global_logger->log_info(str_tmpf(fmt, args...));
	} else {
		fmt::print("[info] {}\n", str_tmpf(fmt, args...));
	}
}

template<typename... TArgs>
inline static void
log_warning(StrView fmt, TArgs&&... args) {
	if (log_global_logger) {
		log_global_logger->log_warning(str_tmpf(fmt, args...));
	} else {
		fmt::print("[warning] {}\n", str_tmpf(fmt, args...));
	}
}

template<typename... TArgs>
inline static void
log_error(StrView fmt, TArgs&&... args) {
	if (log_global_logger) {
		log_global_logger->log_error(str_tmpf(fmt, args...));
	} else {
		fmt::print("[error] {}\n", str_tmpf(fmt, args...));
	}
}
