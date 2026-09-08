//===--- Definition of baremetal stdout -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdio/stdout.h"

#include "hdr/types/FILE.h"
#include "src/__support/OSUtil/baremetal/io.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/stdio/baremetal/file_internal.h"

namespace LIBC_NAMESPACE_DECL {

extern "C" struct __llvm_libc_stdio_cookie __llvm_libc_stdout_cookie;

LLVM_LIBC_VARIABLE(FILE *, stdout) = reinterpret_cast<FILE *>(
    StandardStream<BaremetalFile, &__llvm_libc_stdout_cookie>::value);

} // namespace LIBC_NAMESPACE_DECL
