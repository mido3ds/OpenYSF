#include "utils.hpp"

#if OS_WINDOWS
#include <string.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <DbgHelp.h>
#include <mbstring.h>
#include <tchar.h>
#include <Shlobj.h>

struct Debugger_Callstack {
	Debugger_Callstack() {
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
		SymInitialize(GetCurrentProcess(), NULL, true);
	}

	~Debugger_Callstack() {
		SymCleanup(GetCurrentProcess());
	}
};

size_t _callstack_capture(void** frames, size_t frames_count) {
	::memset(frames, 0, frames_count * sizeof(frames));
	return CaptureStackBackTrace(1, (DWORD)frames_count, frames, NULL);
}

void _callstack_print_to([[maybe_unused]] void** frames, [[maybe_unused]] size_t frames_count) {
	#if DEBUG
	static Debugger_Callstack _d;

	// gather all the loaded libraries first
	Vec<void*> libs {GetCurrentProcess()};
	DWORD bytes_needed = 0;
	if (EnumProcessModules(libs[0], NULL, 0, &bytes_needed)) {
		libs.resize(bytes_needed/sizeof(HMODULE) + libs.size());
		if (EnumProcessModules(libs[0], (HMODULE*)(libs.data() + 1), bytes_needed, &bytes_needed) == FALSE) {
			// reset it back to 1 we have failed
			libs.resize(1);
		}
	}

	constexpr size_t MAX_NAME_LEN = 256;
	// allocate a buffer for the symbol info
	// windows lays the symbol info in memory in this form
	// [struct][name buffer]
	// and the name buffer size is the same as the MaxNameLen set below
	char buffer[sizeof(SYMBOL_INFO) + MAX_NAME_LEN];

	SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
	::memset(symbol, 0, sizeof(SYMBOL_INFO));
	symbol->MaxNameLen = MAX_NAME_LEN;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for(size_t i = 0; i < frames_count; i++) {
		// we have reached the end
		if (frames[i] == nullptr) {
			break;
		}

		bool symbol_found = false;
		bool line_found = false;
		IMAGEHLP_LINE64 line{};
		line.SizeOfStruct = sizeof(line);
		DWORD dis = 0;
		for (auto lib: libs) {
			if (SymFromAddr(lib, (DWORD64)(frames[i]), NULL, symbol)) {
				symbol_found = true;
				line_found = SymGetLineFromAddr64(lib, (DWORD64)(frames[i]), &dis, &line);
				break;
			}
		}

		fmt::print(
			stderr,
			"[{}]: {}, {}:{}\n",
			frames_count - i - 1,
			symbol_found ? symbol->Name : "UNKNOWN_SYMBOL",
			line_found ? line.FileName : "<NO_FILE_FOUND>",
			line_found ? line.LineNumber : 0UL
		);
	}
	#endif
}

Str folder_config(memory::Allocator* allocator) {
	PWSTR wstr = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &wstr) != S_OK) {
		panic("No local config directory");
	}
	defer(CoTaskMemFree((LPVOID)wstr));

	const int len = wcslen(wstr);

	const int size_needed = WideCharToMultiByte(CP_UTF8, NULL, (LPWSTR)wstr, len, NULL, 0, NULL, NULL);
	Str str(size_needed, 0, allocator);
	WideCharToMultiByte(CP_UTF8, NULL, (LPWSTR)wstr, len, str.data(), str.length(), NULL, NULL);

	path_normalize(str);

	return std::move(str);
}
#else
// TODO implement for linux and mac
#endif

ILogger* log_global_logger = nullptr;

namespace memory {
	static thread_local Arena _tmp_allocator;

	Allocator* tmp() {
		return &_tmp_allocator;
	}

	void reset_tmp() {
		_tmp_allocator = {};
	}
}
