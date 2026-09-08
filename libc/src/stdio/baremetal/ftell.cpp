//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the bare-metal implementation of ftell.
///
//===----------------------------------------------------------------------===//

#include "src/stdio/ftell.h"

#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/stdio/baremetal/file_internal.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(long, ftell, (::FILE * stream)) {
  off_t result = BaremetalFile::tell(stream);
  if (result < 0)
    return -1L;
  if (result > cpp::numeric_limits<long>::max()) {
    libc_errno = EOVERFLOW;
    return -1L;
  }
  return static_cast<long>(result);
}

} // namespace LIBC_NAMESPACE_DECL
