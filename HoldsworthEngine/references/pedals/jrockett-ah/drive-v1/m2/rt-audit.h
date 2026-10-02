#pragma once
// Native macOS-only validation interposers; never part of the product build.
#include <atomic>
#include <cstdlib>
#include <pthread.h>
extern thread_local bool inAudio;
extern std::atomic<unsigned> rtViolations;
#ifdef AH_RT_INTERPOSER
static void rtCheck() { if(inAudio) rtViolations.fetch_add(1,std::memory_order_relaxed); }
static void* checkedMalloc(size_t n) { rtCheck(); return malloc(n); }
static void* checkedCalloc(size_t n,size_t s) { rtCheck(); return calloc(n,s); }
static void* checkedRealloc(void* p,size_t n) { rtCheck(); return realloc(p,n); }
static void checkedFree(void* p) { rtCheck(); free(p); }
static int checkedMutex(pthread_mutex_t* m) { rtCheck(); return pthread_mutex_lock(m); }
static int checkedTryMutex(pthread_mutex_t* m) { rtCheck(); return pthread_mutex_trylock(m); }
__attribute__((used,section("__DATA,__interpose"))) static const struct {const void* replacement;const void* original;} rtHooks[] = {
  {reinterpret_cast<const void*>(checkedMalloc),reinterpret_cast<const void*>(malloc)},
  {reinterpret_cast<const void*>(checkedCalloc),reinterpret_cast<const void*>(calloc)},
  {reinterpret_cast<const void*>(checkedRealloc),reinterpret_cast<const void*>(realloc)},
  {reinterpret_cast<const void*>(checkedFree),reinterpret_cast<const void*>(free)},
  {reinterpret_cast<const void*>(checkedMutex),reinterpret_cast<const void*>(pthread_mutex_lock)},
  {reinterpret_cast<const void*>(checkedTryMutex),reinterpret_cast<const void*>(pthread_mutex_trylock)}
};
#endif
