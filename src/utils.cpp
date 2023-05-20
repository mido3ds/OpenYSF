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
#elif OS_LINUX
#include <cxxabi.h>
#include <execinfo.h>

size_t _callstack_capture([[maybe_unused]] void** frames, [[maybe_unused]] size_t frames_count) {
    ::memset(frames, 0, frames_count * sizeof(frames));
    return backtrace(frames, frames_count);
}

void _callstack_print_to([[maybe_unused]] void** frames, [[maybe_unused]] size_t frames_count) {
    #if DEBUG
    constexpr size_t MAX_NAME_LEN = 255;
    //+1 for null terminated string
    char name_buffer[MAX_NAME_LEN+1];
    char** symbols = backtrace_symbols(frames, frames_count);
    if (symbols) {
        for (size_t i = 0; i < frames_count; ++i) {
            // isolate the function name
            char *name_begin = nullptr, *name_end = nullptr, *name_it = symbols[i];
            while (*name_it != 0) {
                if(*name_it == '(') {
                    name_begin = name_it+1;
                } else if(*name_it == ')' || *name_it == '+') {
                    name_end = name_it;
                    break;
                }

                ++name_it;
            }

            size_t mangled_name_size = name_end - name_begin;
            // function maybe inlined
            if (mangled_name_size == 0) {
                fmt::print("[{}]: {}\n", frames_count - i - 1, symbols[i]);
                continue;
            }

            // copy the function name into the name buffer
            size_t copy_size = mangled_name_size > MAX_NAME_LEN ? MAX_NAME_LEN : mangled_name_size;
            memcpy(name_buffer, name_begin, copy_size);
            name_buffer[copy_size] = 0;

            int status = 0;
            char* demangled_name = abi::__cxa_demangle(name_buffer, NULL, 0, &status);

            if (status == 0) {
                fmt::print("[{}]: {}\n", frames_count - i - 1, demangled_name);
            } else {
                fmt::print("[{}]: {}\n", frames_count - i - 1, name_buffer);
            }

            ::free(demangled_name);
        }
        ::free(symbols);
    }
    #endif
}
#elif OS_MACOS
#include <cxxabi.h>
#include <execinfo.h>

#include <stdlib.h>

size_t
_callstack_capture([[maybe_unused]] void** frames, [[maybe_unused]] size_t frames_count) {
    ::memset(frames, 0, frames_count * sizeof(frames));
    return backtrace(frames, frames_count);
}

void
_callstack_print_to([[maybe_unused]] void** frames, [[maybe_unused]] size_t frames_count) {
    #ifdef DEBUG
    char** symbols = backtrace_symbols(frames, frames_count);
    if (symbols) {
        for(size_t i = 0; i < frames_count; i++) {
            // example output
            // 0   <module_name>     0x0000000000000000 function_name + 00
            char function_name[1024] = {};
            char address[48] = {};
            char module_name[1024] = {};
            int offset = 0;

            ::sscanf(symbols[i], "%*s %s %s %s %*s %d", module_name, address, function_name, &offset);

            int status = 0;
            char* demangled_name = abi::__cxa_demangle(function_name, NULL, 0, &status);

            if (status == 0) {
                fmt::print("[{}]: {}\n", frames_count - i - 1, demangled_name);
            } else {
                fmt::print("[{}]: {}\n", frames_count - i - 1, function_name);
            }

            ::free(demangled_name);
        }
        ::free(symbols);
    }
    #endif
}
#endif

#ifdef OS_WINDOWS
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
Str folder_config(memory::Allocator* allocator) {
    char* s = getenv("XDG_CONFIG_HOME");
    if (s && ::strlen(s) > 0) {
        return Str(s, allocator);
    }

    s = getenv("HOME");
    if (s && ::strlen(s) > 0) {
        return str_format(allocator, "{}/.config", s);
    }

	return Str("~/.config", allocator);
}
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

    Arena::~Arena() noexcept {
        while (this->head) {
			Node* next = this->head->next;
			::free(this->head);
			this->head = next;
		}
    }

    void*
    Arena::do_allocate(std::size_t size, std::size_t alignment) {
        if (size == 0) {
			return {};
        }

        bool need_to_grow = true;

		if (this->head != nullptr) {
			size_t node_used_mem = this->head->alloc_head - (uint8_t*)this->head->mem_ptr;
			size_t node_free_mem = this->head->mem_size - node_used_mem;
			if (node_free_mem >= size) {
				need_to_grow = false;
            }
		}

        if (need_to_grow) {
            size_t request_size = size > BLOCK_SIZE ? size : BLOCK_SIZE;
            request_size += sizeof(Node);

            Node* new_node = (Node*) ::malloc(request_size);

            new_node->mem_ptr = &new_node[1];
            new_node->mem_size = request_size - sizeof(Node);
            new_node->alloc_head = (uint8_t*)new_node->mem_ptr;
            new_node->next = this->head;
            this->head = new_node;
        }

		uint8_t* ptr = this->head->alloc_head;
		this->head->alloc_head += size;

		return ptr;
    }
}

#ifdef COMPILER_APPLE_CLANG
namespace std { namespace experimental { inline namespace fundamentals_v1 { namespace pmr {
    static memory::LibcAllocator _c_allocator;
    static thread_local memory_resource* _global_default_resource = nullptr;

    memory_resource*
    get_default_resource() noexcept {
        if (_global_default_resource == nullptr) {
            _global_default_resource = &_c_allocator;
        }
		return _global_default_resource;
    }

    memory_resource*
    set_default_resource(memory_resource* res) noexcept {
        auto x = get_default_resource();
		_global_default_resource = res;
		return x;
    }
} } } }
#endif
