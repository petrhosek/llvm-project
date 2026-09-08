//===--- Variant-like FILE with static or dynamic dispatch ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// `FileVariant` is a `FILE` body that can dispatch its I/O to any one of a
/// compile-time list of alternatives.
///
/// Each alternative is either a `StaticBackend`, whose `cookie_io_functions_t`
/// table is a compile-time constant, or a `DynamicBackend`, which carries
/// its table per instance. A variant over a single static alternative stores
/// nothing but the cookie and devirtualizes every call into a direct branch;
/// adding `DynamicBackend` is what buys `fopencookie` support, at the
/// cost of the table and a tag check.
///
/// Targets pick the alternatives they want, so an embedder that does not need
/// `fopencookie` does not pay for it:
///
/// ```
/// using MyFile = FileVariant<MyEmbeddingBackend>;                    // 2
/// words using MyFile = FileVariant<MyEmbeddingBackend, DynamicBackend>;
/// ```
///
/// Every alternative speaks the `cookie_io_functions_t` contract: `read` and
/// `write` return the number of bytes transferred or -1 with `errno` set,
/// `seek` returns 0 or -1 with `errno` set, and `close` returns 0 or `EOF`. A
/// target whose native hooks use a different convention adapts them in its own
/// `StaticBackend` table rather than here.
///
/// All operations are named `_unlocked` because they do no locking of their
/// own; callers are responsible for serializing access.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_FILE_FILE_VARIANT_H
#define LLVM_LIBC_SRC___SUPPORT_FILE_FILE_VARIANT_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/FILE.h"
#include "hdr/types/cookie_io_functions_t.h"
#include "hdr/types/off_t.h"
#include "hdr/types/size_t.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/utility/in_place.h"
#include "src/__support/File/file_io_result.h"
#include "src/__support/File/io_backend.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Per-instance storage for a dynamic alternative's function table. Variants
// with no dynamic alternative inherit the empty specialization, so the empty
// base optimization keeps it free.
template <bool HasDynamic> struct DynamicOps {
  cookie_io_functions_t ops;

  LIBC_INLINE constexpr DynamicOps() : ops() {}
  LIBC_INLINE constexpr explicit DynamicOps(const cookie_io_functions_t &funcs)
      : ops(funcs) {}
};

template <> struct DynamicOps<false> {
  LIBC_INLINE constexpr DynamicOps() {}
};

template <size_t N, typename T, typename... Rest> struct nth_type {
  using type = typename nth_type<N - 1, Rest...>::type;
};

template <typename T, typename... Rest> struct nth_type<0, T, Rest...> {
  using type = T;
};

template <size_t N, typename... Ts>
using nth_type_t = typename nth_type<N, Ts...>::type;

} // namespace internal

template <typename... Backends>
class FileVariant
    : private internal::DynamicOps<(Backends::is_dynamic || ...)> {
  static_assert(sizeof...(Backends) > 0,
                "A FileVariant needs at least one backend");
  static_assert(sizeof...(Backends) <= 255,
                "The backend tag is stored in a single byte");

  static constexpr bool HAS_DYNAMIC = (Backends::is_dynamic || ...);
  using DynamicBase = internal::DynamicOps<HAS_DYNAMIC>;
  using FirstBackend = internal::nth_type_t<0, Backends...>;

  void *cookie;
  // The byte pushed back by `ungetc`, or -1 when there is none.
  int16_t ungetc_buf;
  // Index of the active alternative. Always 0 for a single-alternative
  // variant, where it is never read.
  uint8_t tag;
  uint8_t eof : 1;
  uint8_t err : 1;

public:
  // True when there is a single static alternative that owns no per-stream
  // state. In this mode a `FILE *` IS the `void *` cookie directly and is
  // never dereferenced by libc.
  static constexpr bool IS_STATELESS =
      (sizeof...(Backends) == 1) && FirstBackend::is_stateless;

  // Index of the dynamic alternative, or sizeof...(Backends) if there is none.
  LIBC_INLINE static constexpr size_t dynamic_backend_index() {
    const bool is_dynamic[] = {Backends::is_dynamic...};
    for (size_t i = 0; i < sizeof...(Backends); ++i)
      if (is_dynamic[i])
        return i;
    return sizeof...(Backends);
  }

  LIBC_INLINE static constexpr bool has_dynamic_backend() {
    return HAS_DYNAMIC;
  }

  // Constructs a stream over the alternative at index 0, which must be static.
  LIBC_INLINE constexpr explicit FileVariant(void *c = nullptr)
      : FileVariant(cpp::in_place_index<0>, c) {}

  // Constructs a stream over the static alternative at `Index`.
  template <size_t Index>
  LIBC_INLINE constexpr FileVariant(cpp::in_place_index_t<Index>, void *c)
      : DynamicBase(), cookie(c), ungetc_buf(-1),
        tag(static_cast<uint8_t>(Index)), eof(0), err(0) {
    static_assert(Index < sizeof...(Backends), "Backend index out of range");
    static_assert(!internal::nth_type_t<Index, Backends...>::is_dynamic,
                  "This constructor requires a static backend; pass a "
                  "cookie_io_functions_t to use a dynamic one");
  }

  // Constructs a stream over the dynamic alternative at `Index`.
  template <size_t Index>
  LIBC_INLINE constexpr FileVariant(cpp::in_place_index_t<Index>, void *c,
                                    const cookie_io_functions_t &funcs)
      : DynamicBase(funcs), cookie(c), ungetc_buf(-1),
        tag(static_cast<uint8_t>(Index)), eof(0), err(0) {
    static_assert(Index < sizeof...(Backends), "Backend index out of range");
    static_assert(internal::nth_type_t<Index, Backends...>::is_dynamic,
                  "This constructor requires a dynamic backend");
  }

  // Constructs a stream over the sole dynamic alternative.
  LIBC_INLINE constexpr FileVariant(void *c, const cookie_io_functions_t &funcs)
      : FileVariant(cpp::in_place_index<dynamic_backend_index()>, c, funcs) {
    static_assert(HAS_DYNAMIC, "This variant has no dynamic backend");
  }

  // A FILE is referred to by pointer and may own its own storage, so copying
  // one is never right.
  FileVariant(const FileVariant &) = delete;
  FileVariant &operator=(const FileVariant &) = delete;
  FileVariant(FileVariant &&) = delete;
  FileVariant &operator=(FileVariant &&) = delete;

  LIBC_INLINE void *get_cookie() const { return cookie; }
  LIBC_INLINE uint8_t get_tag() const { return tag; }
  LIBC_INLINE bool get_eof() const { return eof; }
  LIBC_INLINE bool get_err() const { return err; }

  LIBC_INLINE void clear_err() {
    err = 0;
    eof = 0;
  }

  // Invokes `visitor(ops, cookie)` on the active alternative. For a
  // single-alternative variant this compiles to a direct call with no test.
  template <typename Visitor>
  LIBC_INLINE decltype(auto) visit(Visitor visitor) {
    return visit_impl<0>(visitor);
  }

  // Static `::FILE *` entrypoints used by stdio functions. When `IS_STATELESS`
  // is true, `stream` IS the embedder's `void *` cookie and is passed directly
  // to `FirstBackend::ops` without ever being dereferenced.

  LIBC_INLINE static FileIOResult write(::FILE *stream, const void *data,
                                        size_t len) {
    if (stream == nullptr)
      return {0, EINVAL};
    if constexpr (IS_STATELESS) {
      if (len == 0)
        return 0;
      return raw_write(FirstBackend::ops, stream, data, len);
    } else {
      return from_stream(stream)->write_unlocked(data, len);
    }
  }

  LIBC_INLINE static FileIOResult read(::FILE *stream, void *data, size_t len) {
    if (stream == nullptr)
      return {0, EINVAL};
    if constexpr (IS_STATELESS) {
      if (len == 0 || FirstBackend::ops.read == nullptr)
        return 0;
      ssize_t ret =
          FirstBackend::ops.read(stream, static_cast<char *>(data), len);
      if (ret < 0)
        return {0, libc_errno};
      return static_cast<size_t>(ret);
    } else {
      return from_stream(stream)->read_unlocked(data, len);
    }
  }

  LIBC_INLINE static int ungetc(int c, ::FILE *stream) {
    if (stream == nullptr)
      return EOF;
    if constexpr (IS_STATELESS)
      return EOF;
    else
      return from_stream(stream)->ungetc_unlocked(c);
  }

  LIBC_INLINE static int flush(::FILE *stream) {
    if (stream == nullptr)
      return 0;
    if constexpr (IS_STATELESS)
      return 0;
    else
      return from_stream(stream)->flush_unlocked();
  }

  LIBC_INLINE static int seek(::FILE *stream, off_t offset, int whence) {
    if (stream == nullptr) {
      libc_errno = EINVAL;
      return -1;
    }
    if constexpr (IS_STATELESS) {
      if (FirstBackend::ops.seek == nullptr) {
        libc_errno = ESPIPE;
        return -1;
      }
      off_t off = offset;
      return FirstBackend::ops.seek(stream, &off, whence) == 0 ? 0 : -1;
    } else {
      return from_stream(stream)->seek_unlocked(offset, whence);
    }
  }

  LIBC_INLINE static off_t tell(::FILE *stream) {
    if (stream == nullptr) {
      libc_errno = EINVAL;
      return -1;
    }
    if constexpr (IS_STATELESS) {
      if (FirstBackend::ops.seek == nullptr) {
        libc_errno = ESPIPE;
        return -1;
      }
      off_t off = 0;
      return FirstBackend::ops.seek(stream, &off, SEEK_CUR) == 0 ? off : -1;
    } else {
      return from_stream(stream)->tell_unlocked();
    }
  }

  LIBC_INLINE static int close(::FILE *stream) {
    if (stream == nullptr) {
      libc_errno = EINVAL;
      return EOF;
    }
    if constexpr (IS_STATELESS) {
      if (FirstBackend::ops.close == nullptr)
        return 0;
      return FirstBackend::ops.close(stream) == 0 ? 0 : EOF;
    } else {
      return from_stream(stream)->close_unlocked();
    }
  }

  LIBC_INLINE static int get_eof(::FILE *stream) {
    if (stream == nullptr)
      return 0;
    if constexpr (IS_STATELESS)
      return 0;
    else
      return from_stream(stream)->get_eof() ? 1 : 0;
  }

  LIBC_INLINE static int get_err(::FILE *stream) {
    if (stream == nullptr)
      return 0;
    if constexpr (IS_STATELESS)
      return 0;
    else
      return from_stream(stream)->get_err() ? 1 : 0;
  }

  LIBC_INLINE FileIOResult write_unlocked(const void *data, size_t len) {
    if (len == 0)
      return 0;

    FileIOResult res =
        visit([&](const cookie_io_functions_t &ops, void *c) -> FileIOResult {
          return raw_write(ops, c, data, len);
        });
    if (res.has_error())
      err = 1;
    return res;
  }

  LIBC_INLINE FileIOResult read_unlocked(void *data, size_t len) {
    if (len == 0)
      return 0;

    char *dst = static_cast<char *>(data);
    size_t copied = 0;
    if (ungetc_buf >= 0) {
      dst[0] = static_cast<char>(static_cast<unsigned char>(ungetc_buf));
      ungetc_buf = -1;
      copied = 1;
      if (len == 1)
        return 1;
    }

    return visit(
        [&](const cookie_io_functions_t &ops, void *c) -> FileIOResult {
          // A null read hook means the stream is always at end-of-file.
          if (ops.read == nullptr) {
            eof = 1;
            return copied;
          }
          ssize_t ret = ops.read(c, dst + copied, len - copied);
          if (ret < 0) {
            err = 1;
            return {copied, libc_errno};
          }
          if (ret == 0)
            eof = 1;
          return copied + static_cast<size_t>(ret);
        });
  }

  LIBC_INLINE int ungetc_unlocked(int c) {
    // Only one byte of pushback is required by the C standard, and it may not
    // be EOF.
    if (c == EOF || ungetc_buf >= 0)
      return EOF;
    ungetc_buf = static_cast<int16_t>(static_cast<unsigned char>(c));
    eof = 0;
    return static_cast<unsigned char>(c);
  }

  // Streams are unbuffered, so there is never anything to flush.
  LIBC_INLINE int flush_unlocked() { return 0; }

  LIBC_INLINE int seek_unlocked(off_t offset, int whence) {
    // A pushed-back byte sits between the logical position and the backend's,
    // so a relative seek has to account for it. Seeking always discards it.
    if (whence == SEEK_CUR && ungetc_buf >= 0)
      --offset;
    ungetc_buf = -1;

    return visit([&](const cookie_io_functions_t &ops, void *c) -> int {
      if (ops.seek == nullptr) {
        libc_errno = ESPIPE;
        return -1;
      }
      off_t off = offset;
      if (ops.seek(c, &off, whence) != 0) {
        err = 1;
        return -1;
      }
      eof = 0;
      return 0;
    });
  }

  LIBC_INLINE off_t tell_unlocked() {
    return visit([&](const cookie_io_functions_t &ops, void *c) -> off_t {
      if (ops.seek == nullptr) {
        libc_errno = ESPIPE;
        return -1;
      }
      off_t off = 0;
      if (ops.seek(c, &off, SEEK_CUR) != 0) {
        err = 1;
        return -1;
      }
      // Report the logical position, which trails the backend's by the byte
      // still held in pushback.
      if (ungetc_buf >= 0 && off > 0)
        --off;
      return off;
    });
  }

  // Closes the stream, returning 0 or EOF.
  LIBC_INLINE int close_unlocked() {
    int ret = visit([](const cookie_io_functions_t &ops, void *c) -> int {
      return ops.close == nullptr ? 0 : ops.close(c);
    });
    if (ret != 0)
      ret = EOF;
    return ret;
  }

private:
  LIBC_INLINE static FileVariant *from_stream(::FILE *s) {
    return reinterpret_cast<FileVariant *>(s);
  }

  LIBC_INLINE static FileIOResult raw_write(const cookie_io_functions_t &ops,
                                            void *c, const void *data,
                                            size_t len) {
    // A null write hook means output is discarded, as in fopencookie.
    if (ops.write == nullptr)
      return len;

    // The hook may satisfy only part of the request, so keep going until
    // it does not make progress.
    const char *src = static_cast<const char *>(data);
    size_t written = 0;
    while (written < len) {
      ssize_t ret = ops.write(c, src + written, len - written);
      if (ret < 0)
        return {written, libc_errno};
      if (ret == 0)
        break;
      written += static_cast<size_t>(ret);
    }
    return written;
  }

  template <size_t I, typename Visitor>
  LIBC_INLINE decltype(auto) visit_impl(Visitor &visitor) {
    using B = internal::nth_type_t<I, Backends...>;
    if constexpr (I + 1 == sizeof...(Backends)) {
      // The last alternative is the only one left, so no test is needed. For a
      // single-alternative variant this is the whole of the dispatch.
      return dispatch<B>(visitor);
    } else {
      if (tag == I)
        return dispatch<B>(visitor);
      return visit_impl<I + 1>(visitor);
    }
  }

  template <typename B, typename Visitor>
  LIBC_INLINE decltype(auto) dispatch(Visitor &visitor) {
    if constexpr (B::is_dynamic)
      return visitor(this->DynamicBase::ops, cookie);
    else
      return visitor(B::ops, cookie);
  }
};

// Materializes a `FileVariant` in `.data` only when the variant is stateful;
// when stateless, yields the embedder's cookie pointer directly with zero
// libc-owned storage.
template <typename FileT, auto *Cookie, bool Stateless = FileT::IS_STATELESS>
struct StandardStream {
  static FileT instance;
  static constexpr void *value = &instance;
};

template <typename FileT, auto *Cookie, bool Stateless>
FileT StandardStream<FileT, Cookie, Stateless>::instance(Cookie);

template <typename FileT, auto *Cookie>
struct StandardStream<FileT, Cookie, true> {
  static constexpr void *value = Cookie;
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_FILE_FILE_VARIANT_H
