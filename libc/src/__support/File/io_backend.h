//===--- I/O backend alternatives for FileVariant ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The alternatives a `FileVariant` can dispatch to. Each alternative supplies
/// a `cookie_io_functions_t` table; the only thing that distinguishes them is
/// *when* that table is known.
///
/// A `StaticBackend` binds the table at compile time, so the compiler
/// devirtualizes every operation into a direct call. A `DynamicBackend`
/// carries the table per instance, which is what `fopencookie` needs.
///
/// Targets define their own `StaticBackend` specializations over whatever
/// function table they provide, so this header stays free of any per-target
/// policy.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_FILE_IO_BACKEND_H
#define LLVM_LIBC_SRC___SUPPORT_FILE_IO_BACKEND_H

#include "hdr/types/cookie_io_functions_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Dispatches to a `cookie_io_functions_t` table that is known at compile time.
// `Funcs` must have external linkage (declare it `LIBC_INLINE_VAR constexpr`)
// so that every translation unit agrees on the resulting type. When
// `Stateless` is true and this is the sole alternative in a `FileVariant`, the
// `FILE *` pointer itself is treated directly as the `void *` cookie with no
// libc-owned per-stream state.
template <const cookie_io_functions_t &Funcs, bool Stateless = false>
struct StaticBackend {
  static constexpr bool is_dynamic = false;
  static constexpr bool is_stateless = Stateless;
  static constexpr const cookie_io_functions_t &ops = Funcs;
};

// Dispatches to a `cookie_io_functions_t` table supplied at runtime, as by
// `fopencookie`. Instances carry their own copy of the table.
struct DynamicBackend {
  static constexpr bool is_dynamic = true;
  static constexpr bool is_stateless = false;
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_FILE_IO_BACKEND_H
