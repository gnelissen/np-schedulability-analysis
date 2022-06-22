#ifndef CONFIG_H
#define CONFIG_H

// DM : debug message -- disable for now
// #define DM(x) std::cerr << x
#define DM(x)

#ifndef NDEBUG
#define TBB_USE_DEBUG 1
#endif

#endif
