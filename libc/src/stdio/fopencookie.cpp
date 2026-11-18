//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of fopencookie.
///
//===----------------------------------------------------------------------===//

#include "src/stdio/fopencookie.h"

#include "hdr/func/free.h"
#include "hdr/types/FILE.h"
#include "hdr/types/cookie_io_functions_t.h"
#include "src/__support/CPP/new.h"
#include "src/__support/File/file.h"
#include "src/__support/File/file_mode.h"
#include "src/__support/File/platform_file.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(::FILE *, fopencookie,
                   (void *cookie, const char *mode,
                    cookie_io_functions_t ops)) {
  uint8_t *buffer;
  {
    AllocChecker ac;
    buffer = new (ac) uint8_t[File::DEFAULT_BUFFER_SIZE];
    if (!ac)
      return nullptr;
  }
  AllocChecker ac;
  auto *file = new (ac)
      CookieFile(cookie, {ops.read, ops.write, ops.seek, ops.close}, buffer,
                 File::DEFAULT_BUFFER_SIZE, 0 /* default buffering mode */,
                 true /* File owns buffer */, FileMode(mode));
  if (!ac) {
    free(buffer);
    return nullptr;
  }
  return reinterpret_cast<::FILE *>(file);
}

} // namespace LIBC_NAMESPACE_DECL
