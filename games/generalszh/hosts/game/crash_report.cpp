// A crash's report on stderr (Windows): the exception, where it happened and
// the stack, as module+offset (resolve with the host's linker map), so an
// intermittent fault leaves evidence without a debugger attached.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <cstdio>

namespace generalszh::host
{
#ifdef _WIN32
namespace
{
void PrintAddress(const void *address)
{
	HMODULE module = nullptr;
	char path[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(address), &module) &&
		module != nullptr)
		GetModuleFileNameA(module, path, MAX_PATH);
	const char *name = path;
	for (const char *c = path; *c != '\0'; ++c)
		if (*c == '\\' || *c == '/')
			name = c + 1;
	std::fprintf(stderr, "  %s+0x%llx\n", name,
		static_cast<unsigned long long>(reinterpret_cast<const char *>(address) - reinterpret_cast<const char *>(module)));
}

LONG WINAPI Report(EXCEPTION_POINTERS *exception)
{
	const EXCEPTION_RECORD &record = *exception->ExceptionRecord;
	std::fprintf(stderr, "crash: exception 0x%08lx at", static_cast<unsigned long>(record.ExceptionCode));
	PrintAddress(record.ExceptionAddress);
	if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
		std::fprintf(stderr, "  (%s 0x%llx)\n", record.ExceptionInformation[0] == 0 ? "reading" : "writing",
			static_cast<unsigned long long>(record.ExceptionInformation[1]));
	std::fprintf(stderr, "  thread %lu stack:\n", GetCurrentThreadId());
	// Walk the faulting thread's stack from the exception's context (x64 unwind data).
	CONTEXT context = *exception->ContextRecord;
	for (int frame = 0; frame < 48 && context.Rip != 0; ++frame)
	{
		PrintAddress(reinterpret_cast<const void *>(context.Rip));
		DWORD64 base = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
		if (function == nullptr)
		{
			// A leaf: its return address is at the top of the stack.
			context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
			context.Rsp += 8;
			continue;
		}
		void *handlerData = nullptr;
		DWORD64 establisher = 0;
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handlerData, &establisher, nullptr);
	}
	std::fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}
}

void InstallCrashReport() { SetUnhandledExceptionFilter(Report); }
#else
void InstallCrashReport() {}
#endif
}
