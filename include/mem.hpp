#ifndef MEM_HPP
#define MEM_HPP

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

class Memory_monitor {

private:

#ifdef _WIN32 
	HANDLE currentProcess;
#else
	struct rusage u;
#endif

public:

	Memory_monitor() {
#ifdef _WIN32 
		currentProcess = GetCurrentProcess();
#else
		struct rusage u;
		long mem_used = 0;
		if (getrusage(RUSAGE_SELF, &u) == 0)
			mem_used = u.ru_maxrss;
#endif
	}

	~Memory_monitor() {
#ifdef _WIN32
		CloseHandle(currentProcess);
#endif
	}

	operator long() const {	
#ifdef _WIN32 
		PROCESS_MEMORY_COUNTERS pmc;
		GetProcessMemoryInfo(currentProcess, &pmc, sizeof(pmc));
		return pmc.PeakWorkingSetSize / 1024; //in kiB
#else
	if (getrusage(RUSAGE_SELF, const_cast<struct rusage*>(&u)) == 0)
		return u.ru_maxrss;
	else
		return 0;
#endif
	}

};

#endif // !MEM_HPP
