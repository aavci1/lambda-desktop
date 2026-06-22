#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void* (*real_malloc_fn)(size_t);
static void* (*real_calloc_fn)(size_t, size_t);
static void* (*real_realloc_fn)(void*, size_t);
static void (*real_free_fn)(void*);
static void* (*real_aligned_alloc_fn)(size_t, size_t);
static int (*real_posix_memalign_fn)(void**, size_t, size_t);

static atomic_ullong malloc_calls;
static atomic_ullong calloc_calls;
static atomic_ullong realloc_calls;
static atomic_ullong aligned_alloc_calls;
static atomic_ullong posix_memalign_calls;
static atomic_ullong free_calls;
static atomic_ullong requested_bytes;
static __thread bool inside_hook;

static void resolve_symbols(void) {
  if (real_malloc_fn && real_calloc_fn && real_realloc_fn && real_free_fn && real_aligned_alloc_fn &&
      real_posix_memalign_fn) {
    return;
  }
  bool const was_inside = inside_hook;
  inside_hook = true;
  if (!real_malloc_fn) real_malloc_fn = (void* (*)(size_t))dlsym(RTLD_NEXT, "malloc");
  if (!real_calloc_fn) real_calloc_fn = (void* (*)(size_t, size_t))dlsym(RTLD_NEXT, "calloc");
  if (!real_realloc_fn) real_realloc_fn = (void* (*)(void*, size_t))dlsym(RTLD_NEXT, "realloc");
  if (!real_free_fn) real_free_fn = (void (*)(void*))dlsym(RTLD_NEXT, "free");
  if (!real_aligned_alloc_fn) {
    real_aligned_alloc_fn = (void* (*)(size_t, size_t))dlsym(RTLD_NEXT, "aligned_alloc");
  }
  if (!real_posix_memalign_fn) {
    real_posix_memalign_fn = (int (*)(void**, size_t, size_t))dlsym(RTLD_NEXT, "posix_memalign");
  }
  inside_hook = was_inside;
}

static void add_requested(size_t bytes) {
  atomic_fetch_add_explicit(&requested_bytes, (unsigned long long)bytes, memory_order_relaxed);
}

void* malloc(size_t size) {
  resolve_symbols();
  if (!real_malloc_fn) _exit(127);
  void* result = real_malloc_fn(size);
  if (!inside_hook) {
    atomic_fetch_add_explicit(&malloc_calls, 1, memory_order_relaxed);
    add_requested(size);
  }
  return result;
}

void* calloc(size_t nmemb, size_t size) {
  resolve_symbols();
  if (!real_calloc_fn) _exit(127);
  void* result = real_calloc_fn(nmemb, size);
  if (!inside_hook) {
    size_t bytes = 0;
    if (!__builtin_mul_overflow(nmemb, size, &bytes)) add_requested(bytes);
    atomic_fetch_add_explicit(&calloc_calls, 1, memory_order_relaxed);
  }
  return result;
}

void* realloc(void* ptr, size_t size) {
  resolve_symbols();
  if (!real_realloc_fn) _exit(127);
  void* result = real_realloc_fn(ptr, size);
  if (!inside_hook) {
    atomic_fetch_add_explicit(&realloc_calls, 1, memory_order_relaxed);
    add_requested(size);
  }
  return result;
}

void free(void* ptr) {
  resolve_symbols();
  if (!real_free_fn) _exit(127);
  if (!inside_hook && ptr) atomic_fetch_add_explicit(&free_calls, 1, memory_order_relaxed);
  real_free_fn(ptr);
}

void* aligned_alloc(size_t alignment, size_t size) {
  resolve_symbols();
  if (!real_aligned_alloc_fn) _exit(127);
  void* result = real_aligned_alloc_fn(alignment, size);
  if (!inside_hook) {
    atomic_fetch_add_explicit(&aligned_alloc_calls, 1, memory_order_relaxed);
    add_requested(size);
  }
  return result;
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
  resolve_symbols();
  if (!real_posix_memalign_fn) _exit(127);
  int const result = real_posix_memalign_fn(memptr, alignment, size);
  if (!inside_hook && result == 0) {
    atomic_fetch_add_explicit(&posix_memalign_calls, 1, memory_order_relaxed);
    add_requested(size);
  }
  return result;
}

static void write_summary(void) __attribute__((destructor));

static void write_summary(void) {
  unsigned long long const malloc_count = atomic_load_explicit(&malloc_calls, memory_order_relaxed);
  unsigned long long const calloc_count = atomic_load_explicit(&calloc_calls, memory_order_relaxed);
  unsigned long long const realloc_count = atomic_load_explicit(&realloc_calls, memory_order_relaxed);
  unsigned long long const aligned_count = atomic_load_explicit(&aligned_alloc_calls, memory_order_relaxed);
  unsigned long long const posix_count = atomic_load_explicit(&posix_memalign_calls, memory_order_relaxed);
  unsigned long long const free_count = atomic_load_explicit(&free_calls, memory_order_relaxed);
  unsigned long long const total_allocs =
      malloc_count + calloc_count + realloc_count + aligned_count + posix_count;
  unsigned long long const bytes = atomic_load_explicit(&requested_bytes, memory_order_relaxed);

  char line[768];
  int const length = snprintf(line,
                              sizeof(line),
                              "lambda-malloc-count: pid=%ld malloc=%llu calloc=%llu realloc=%llu "
                              "aligned_alloc=%llu posix_memalign=%llu free=%llu total_allocs=%llu "
                              "requested_bytes=%llu\n",
                              (long)getpid(),
                              malloc_count,
                              calloc_count,
                              realloc_count,
                              aligned_count,
                              posix_count,
                              free_count,
                              total_allocs,
                              bytes);
  if (length <= 0) return;

  char const* path = getenv("LAMBDA_MALLOC_COUNT_LOG");
  int fd = path && *path ? open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644) : STDERR_FILENO;
  if (fd < 0) return;
  ssize_t unused = write(fd, line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line));
  (void)unused;
  if (fd != STDERR_FILENO) close(fd);
}
