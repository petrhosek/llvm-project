//===-- Implementation of fopencookie for baremetal -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/fopencookie.h"

#include "hdr/errno_macros.h"
#include "hdr/types/FILE.h"
#include "hdr/types/cookie_io_functions_t.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/stdio/baremetal/file_internal.h"

namespace LIBC_NAMESPACE_DECL {

namespace {

// Accepts the mode strings fopen does: one of r/w/a, then any mix of '+', 'b'
// and the glibc extensions 'e' and 'x'.
LIBC_INLINE bool is_valid_mode(const char *mode) {
  if (mode == nullptr)
    return false;
  if (*mode != 'r' && *mode != 'w' && *mode != 'a')
    return false;
  for (const char *c = mode + 1; *c != '\0'; ++c)
    if (*c != '+' && *c != 'b' && *c != 'e' && *c != 'x')
      return false;
  return true;
}

} // anonymous namespace

static_assert(BaremetalFile::has_dynamic_backend(),
              "fopencookie needs the dynamic backend");

LLVM_LIBC_FUNCTION(::FILE *, fopencookie,
                   (void *cookie, const char *mode,
                    cookie_io_functions_t io_funcs)) {
  if (!is_valid_mode(mode)) {
    libc_errno = EINVAL;
    return nullptr;
  }

  // TODO: Enforce the access mode, so that reading a stream opened "w" fails
  // rather than reaching a hook the caller never meant to be used for it.

  AllocChecker ac;
  auto *file = new (ac)
      BaremetalFile(cpp::in_place_index<BaremetalFile::dynamic_backend_index()>,
                    cookie, io_funcs, /*is_allocated=*/true);
  if (!ac) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  return reinterpret_cast<::FILE *>(file);
}

} // namespace LIBC_NAMESPACE_DECL
