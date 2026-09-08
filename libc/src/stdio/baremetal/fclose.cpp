//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the bare-metal implementation of fclose.
///
//===----------------------------------------------------------------------===//

#include "src/stdio/fclose.h"

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/stdio/baremetal/file_internal.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, fclose, (::FILE * stream)) {
  return BaremetalFile::close(stream);
}

} // namespace LIBC_NAMESPACE_DECL
