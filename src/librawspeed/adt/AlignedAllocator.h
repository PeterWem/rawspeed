/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2023 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "rawspeedconfig.h"
#include "AddressSanitizer.h"
#include "adt/Invariant.h"
#include "common/Common.h"
#include "common/RawspeedException.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace rawspeed {

namespace impl {

[[nodiscard]] inline void* __attribute__((malloc, warn_unused_result,
                                          alloc_size(1), alloc_align(2)))
alignedMalloc(size_t size, size_t alignment) {
  invariant(isPowerOfTwo(alignment)); // for posix_memalign, _aligned_malloc
  invariant(isAligned(alignment, sizeof(void*))); // for posix_memalign
  invariant(isAligned(size, alignment));          // for aligned_alloc

  void* ptr = nullptr;

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  // workaround ASAN's broken allocator_may_return_null option
  // plus, avoidance of libFuzzer's rss_limit_mb option
  // if trying to alloc more than 2GB, just return null.
  // else it would abort() the whole program...
  if (size > 2UL << 30UL)
    return ptr;
#endif

#if defined(HAVE_ALIGNED_ALLOC)
  ptr = aligned_alloc(alignment, size);
#elif defined(HAVE_POSIX_MEMALIGN)
  if (0 != posix_memalign(&ptr, alignment, size))
    return nullptr;
#elif defined(HAVE_ALIGNED_MALLOC)
  ptr = _aligned_malloc(size, alignment);
#else
#error "No aligned malloc() implementation available!"
#endif

  invariant(isAligned(ptr, alignment));

  return ptr;
}

inline void alignedFree(void* ptr) {
#if defined(HAVE_ALIGNED_MALLOC)
  _aligned_free(ptr);
#else
  free(ptr); // NOLINT
#endif
}

} // namespace impl

template <class T, int alignment> class AlignedAllocator {
  using self = AlignedAllocator<T, alignment>;
  using allocator_traits = std::allocator_traits<self>;

public:
  using value_type = T;

  template <class U> struct rebind {
    using other = AlignedAllocator<U, alignment>;
  };

  [[nodiscard]] T* allocate(std::size_t numElts) const {
    static_assert(alignment >= alignof(T), "insufficient alignment");
    invariant(numElts > 0 && "Should not be trying to allocate no elements");
    assert(numElts <= allocator_traits::max_size(*this) &&
           "Can allocate this many elements.");
    invariant(numElts <= SIZE_MAX / sizeof(T) &&
              "Byte count calculation will not overflow");

    std::size_t numBytes = sizeof(T) * numElts;
    std::size_t numPaddedBytes = roundUp(numBytes, alignment);
    invariant(numPaddedBytes >= numBytes &&
              "Alignment did not cause wraparound.");

    auto* r = static_cast<T*>(impl::alignedMalloc(numPaddedBytes, alignment));
    if (!r)
      ThrowRSE("Out of memory while trying to allocate %zu bytes",
               numPaddedBytes);
    ASan::PoisonMemoryRegion(r + numElts, numPaddedBytes - numBytes);
    return r;
  }

  void deallocate(T* p, std::size_t n) const noexcept {
    invariant(p);
    invariant(n > 0);
    impl::alignedFree(p);
  }

  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
};

template <class T1, int A1, class T2, int A2>
bool operator==(const AlignedAllocator<T1, A1>& /*unused*/,
                const AlignedAllocator<T2, A2>& /*unused*/) {
  return A1 == A2;
}

template <class T1, int A1, class T2, int A2>
bool operator!=(const AlignedAllocator<T1, A1>& /*unused*/,
                const AlignedAllocator<T2, A2>& /*unused*/) {
  return !(A1 == A2);
}

} // namespace rawspeed
