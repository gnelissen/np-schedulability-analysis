#ifndef CONFIG_H
#define CONFIG_H

// Existing configuration options
#ifdef CONFIG_COLLECT_SCHEDULE_GRAPH
// Schedule graph collection is enabled
#endif

#ifdef CONFIG_PARALLEL
// Parallel execution is enabled
#include <tbb/parallel_for.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_vector.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <mutex>
#endif

// Debug macros
#ifdef DEBUG
#define DM(x) std::cerr << x
#else
#define DM(x) 
#endif

#endif
