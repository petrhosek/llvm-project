//===--- Helper functions for file I/O on baremetal -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H
#define LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/FILE.h"
#include "hdr/types/cookie_io_functions_t.h"
#include "hdr/types/off_t.h"
#include "hdr/types/size_t.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file_io_result.h"
#include "src/__support/File/file_variant.h"
#include "src/__support/File/io_backend.h"
#include "src/__support/OSUtil/io.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// The vendor-supplied embedding hooks report failure by returning a negative
// errno value, whereas cookie_io_functions_t expects -1 with errno set. These
// adapters bridge the two. They inline away, so a call through the resulting
// table still lowers to a direct call to the underlying hook.
namespace embedding {

LIBC_INLINE ssize_t read(void *cookie, char *buf, size_t size) {
  ssize_t ret = __llvm_libc_stdio_read(cookie, buf, size);
  if (ret < 0) {
    libc_errno = static_cast<int>(-ret);
    return -1;
  }
  return ret;
}

LIBC_INLINE ssize_t write(void *cookie, const char *buf, size_t size) {
  ssize_t ret = __llvm_libc_stdio_write(cookie, buf, size);
  if (ret < 0) {
    libc_errno = static_cast<int>(-ret);
    return -1;
  }
  return ret;
}

LIBC_INLINE int seek(void *cookie, off_t *offset, int whence) {
  int ret = __llvm_libc_stdio_seek(cookie, offset, whence);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

LIBC_INLINE int close(void *cookie) {
  int ret = __llvm_libc_stdio_close(cookie);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

// Declared LIBC_INLINE_VAR so that the table has external linkage and every
// translation unit agrees on the type of StaticBackend<io_funcs>.
LIBC_INLINE_VAR constexpr cookie_io_functions_t io_funcs = {
    read,
    write,
    seek,
    close,
};

} // namespace embedding

// Dispatches directly to the embedding hooks with no indirection. Marked
// stateless so that when it is the sole alternative in `BaremetalFile`, the
// `FILE *` pointer IS the embedder's cookie and libc owns zero per-stream
// state.
using EmbeddingBackend = StaticBackend<embedding::io_funcs, /*Stateless=*/true>;

// The baremetal FILE. Streams always dispatch statically to the embedding
// hooks; the dynamic backend is only included when explicitly enabled.
#ifdef LIBC_CONF_STDIO_ENABLE_DYNAMIC_BACKEND
using BaremetalFile = FileVariant<EmbeddingBackend, DynamicBackend>;
#else
using BaremetalFile = FileVariant<EmbeddingBackend>;
#endif

LIBC_INLINE int ungetc_internal(int c, ::FILE *stream) {
  return BaremetalFile::ungetc(c, stream);
}

LIBC_INLINE FileIOResult read_internal(char *buf, size_t size, ::FILE *stream) {
  return BaremetalFile::read(stream, buf, size);
}

LIBC_INLINE FileIOResult write_internal(const char *buf, size_t size,
                                        ::FILE *stream) {
  return BaremetalFile::write(stream, buf, size);
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_BAREMETAL_FILE_INTERNAL_H
