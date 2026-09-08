//===-- Unittests for FileVariant -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/cookie_io_functions_t.h"
#include "hdr/types/off_t.h"
#include "hdr/types/size_t.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/CPP/utility/in_place.h"
#include "src/__support/File/file_variant.h"
#include "src/__support/File/io_backend.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;

namespace {

struct TestBuffer {
  char data[256] = {};
  size_t size = 0;
  size_t pos = 0;
  bool closed = false;
  // When set, the next read or write fails with this errno.
  int fail_with = 0;
  // When set, writes accept at most this many bytes per call.
  size_t write_chunk = 0;
  int close_result = 0;
};

ssize_t test_read(void *cookie, char *buf, size_t size) {
  auto *tb = static_cast<TestBuffer *>(cookie);
  if (tb->fail_with != 0) {
    libc_errno = tb->fail_with;
    return -1;
  }
  size_t available = tb->size - tb->pos;
  size_t to_read = (size < available) ? size : available;
  for (size_t i = 0; i < to_read; ++i)
    buf[i] = tb->data[tb->pos + i];
  tb->pos += to_read;
  return static_cast<ssize_t>(to_read);
}

ssize_t test_write(void *cookie, const char *buf, size_t size) {
  auto *tb = static_cast<TestBuffer *>(cookie);
  if (tb->fail_with != 0) {
    libc_errno = tb->fail_with;
    return -1;
  }
  if (tb->write_chunk != 0 && size > tb->write_chunk)
    size = tb->write_chunk;
  for (size_t i = 0; i < size && tb->pos < sizeof(tb->data); ++i) {
    tb->data[tb->pos++] = buf[i];
    if (tb->pos > tb->size)
      tb->size = tb->pos;
  }
  return static_cast<ssize_t>(size);
}

int test_seek(void *cookie, off_t *offset, int whence) {
  auto *tb = static_cast<TestBuffer *>(cookie);
  off_t target;
  if (whence == SEEK_SET)
    target = *offset;
  else if (whence == SEEK_CUR)
    target = static_cast<off_t>(tb->pos) + *offset;
  else
    target = static_cast<off_t>(tb->size) + *offset;
  if (target < 0) {
    libc_errno = EINVAL;
    return -1;
  }
  tb->pos = static_cast<size_t>(target);
  *offset = target;
  return 0;
}

int test_close(void *cookie) {
  auto *tb = static_cast<TestBuffer *>(cookie);
  tb->closed = true;
  return tb->close_result;
}

LIBC_INLINE_VAR constexpr cookie_io_functions_t test_ops = {
    test_read,
    test_write,
    test_seek,
    test_close,
};

// A backend whose hooks are all null, to exercise the degenerate cases.
LIBC_INLINE_VAR constexpr cookie_io_functions_t null_ops = {nullptr, nullptr,
                                                            nullptr, nullptr};

using TestBackend = StaticBackend<test_ops>;
using NullBackend = StaticBackend<null_ops>;

using StaticFile = FileVariant<TestBackend>;
using HybridFile = FileVariant<TestBackend, DynamicBackend>;
using NullFile = FileVariant<NullBackend>;

} // anonymous namespace

// A pure static variant must cost no more than the cookie plus a word of
// bookkeeping; adding the dynamic alternative adds exactly the function table.
static_assert(sizeof(StaticFile) == 2 * sizeof(void *),
              "A pure static FileVariant should be two words");
static_assert(sizeof(HybridFile) == 6 * sizeof(void *),
              "A hybrid FileVariant should be the static one plus a table");
static_assert(StaticFile::dynamic_backend_index() == 1,
              "A pure static variant has no dynamic alternative");
static_assert(HybridFile::dynamic_backend_index() == 1);
static_assert(!StaticFile::has_dynamic_backend());
static_assert(HybridFile::has_dynamic_backend());

TEST(LlvmLibcFileVariantTest, PureStaticBackend) {
  TestBuffer tb;
  StaticFile f(&tb);

  EXPECT_EQ(f.get_cookie(), static_cast<void *>(&tb));

  const char hello[] = "Hello Baremetal";
  auto wres = f.write_unlocked(hello, sizeof(hello) - 1);
  EXPECT_EQ(wres.value, sizeof(hello) - 1);
  EXPECT_FALSE(wres.has_error());

  EXPECT_EQ(f.seek_unlocked(0, SEEK_SET), 0);
  char read_buf[32] = {};
  auto rres = f.read_unlocked(read_buf, sizeof(hello) - 1);
  EXPECT_EQ(rres.value, sizeof(hello) - 1);
  EXPECT_STREQ(read_buf, hello);

  EXPECT_FALSE(f.get_err());
  EXPECT_FALSE(tb.closed);
  EXPECT_EQ(f.close_unlocked(), 0);
  EXPECT_TRUE(tb.closed);
}

TEST(LlvmLibcFileVariantTest, HybridStaticAndDynamic) {
  TestBuffer tb_static;
  HybridFile f_static(cpp::in_place_index<0>, &tb_static);
  EXPECT_EQ(f_static.get_tag(), uint8_t(0));

  const char msg1[] = "StaticMessage";
  f_static.write_unlocked(msg1, sizeof(msg1) - 1);
  EXPECT_EQ(tb_static.size, sizeof(msg1) - 1);

  TestBuffer tb_dynamic;
  HybridFile f_dynamic(cpp::in_place_index<1>, &tb_dynamic, test_ops);
  EXPECT_EQ(f_dynamic.get_tag(), uint8_t(1));

  const char msg2[] = "DynamicMessage";
  f_dynamic.write_unlocked(msg2, sizeof(msg2) - 1);
  EXPECT_EQ(tb_dynamic.size, sizeof(msg2) - 1);

  f_dynamic.seek_unlocked(0, SEEK_SET);
  char read_buf[32] = {};
  f_dynamic.read_unlocked(read_buf, sizeof(msg2) - 1);
  EXPECT_STREQ(read_buf, msg2);

  EXPECT_FALSE(f_dynamic.get_eof());
  EXPECT_FALSE(f_dynamic.get_err());
  EXPECT_EQ(f_dynamic.close_unlocked(), 0);
  EXPECT_TRUE(tb_dynamic.closed);
}

TEST(LlvmLibcFileVariantTest, AllocatedStreamFreesItselfOnClose) {
  TestBuffer tb;
  auto *f = new HybridFile(cpp::in_place_index<1>, &tb, test_ops,
                           /*is_allocated=*/true);
  EXPECT_FALSE(tb.closed);
  // Frees *f, so it must not be touched afterwards.
  EXPECT_EQ(f->close_unlocked(), 0);
  EXPECT_TRUE(tb.closed);
}

TEST(LlvmLibcFileVariantTest, ShortWritesAreRetried) {
  TestBuffer tb;
  tb.write_chunk = 4;
  StaticFile f(&tb);

  const char msg[] = "0123456789";
  auto res = f.write_unlocked(msg, sizeof(msg) - 1);
  EXPECT_EQ(res.value, sizeof(msg) - 1);
  EXPECT_FALSE(res.has_error());
  EXPECT_EQ(tb.size, sizeof(msg) - 1);
}

TEST(LlvmLibcFileVariantTest, ErrorsPropagateAndSetFlag) {
  TestBuffer tb;
  tb.fail_with = EIO;
  StaticFile f(&tb);

  auto wres = f.write_unlocked("x", 1);
  EXPECT_TRUE(wres.has_error());
  EXPECT_EQ(wres.error, EIO);
  EXPECT_EQ(wres.value, size_t(0));
  EXPECT_TRUE(f.get_err());

  f.clear_err();
  EXPECT_FALSE(f.get_err());

  char buf[4];
  auto rres = f.read_unlocked(buf, sizeof(buf));
  EXPECT_TRUE(rres.has_error());
  EXPECT_EQ(rres.error, EIO);
  EXPECT_TRUE(f.get_err());
}

TEST(LlvmLibcFileVariantTest, ReadAtEndOfFileSetsEof) {
  TestBuffer tb;
  StaticFile f(&tb);

  char buf[4];
  auto res = f.read_unlocked(buf, sizeof(buf));
  EXPECT_EQ(res.value, size_t(0));
  EXPECT_FALSE(res.has_error());
  EXPECT_TRUE(f.get_eof());
}

TEST(LlvmLibcFileVariantTest, NullHooks) {
  TestBuffer tb;
  NullFile f(&tb);

  // A null write hook discards output but reports success.
  auto wres = f.write_unlocked("discarded", 9);
  EXPECT_EQ(wres.value, size_t(9));
  EXPECT_FALSE(wres.has_error());
  EXPECT_EQ(tb.size, size_t(0));

  // A null read hook means immediate end-of-file.
  char buf[4];
  auto rres = f.read_unlocked(buf, sizeof(buf));
  EXPECT_EQ(rres.value, size_t(0));
  EXPECT_TRUE(f.get_eof());

  // A null seek hook means the stream is not seekable.
  libc_errno = 0;
  EXPECT_EQ(f.seek_unlocked(0, SEEK_SET), -1);
  EXPECT_EQ(static_cast<int>(libc_errno), ESPIPE);
  libc_errno = 0;
  EXPECT_EQ(static_cast<int>(f.tell_unlocked()), -1);
  EXPECT_EQ(static_cast<int>(libc_errno), ESPIPE);

  // A null close hook still closes successfully.
  EXPECT_EQ(f.close_unlocked(), 0);
  libc_errno = 0;
}

TEST(LlvmLibcFileVariantTest, Ungetc) {
  TestBuffer tb;
  StaticFile f(&tb);

  const char msg[] = "abc";
  f.write_unlocked(msg, sizeof(msg) - 1);
  f.seek_unlocked(0, SEEK_SET);

  char c = 0;
  f.read_unlocked(&c, 1);
  EXPECT_EQ(c, 'a');

  EXPECT_EQ(f.ungetc_unlocked('Z'), static_cast<int>('Z'));
  // Only one byte of pushback is supported.
  EXPECT_EQ(f.ungetc_unlocked('Y'), EOF);
  // EOF is never pushed back.
  EXPECT_EQ(f.ungetc_unlocked(EOF), EOF);

  f.read_unlocked(&c, 1);
  EXPECT_EQ(c, 'Z');
  // The pushback is consumed, so the next byte comes from the backend.
  f.read_unlocked(&c, 1);
  EXPECT_EQ(c, 'b');
}

TEST(LlvmLibcFileVariantTest, UngetcAdjustsReportedPosition) {
  TestBuffer tb;
  StaticFile f(&tb);

  const char msg[] = "abcdef";
  f.write_unlocked(msg, sizeof(msg) - 1);
  f.seek_unlocked(0, SEEK_SET);

  char buf[3] = {};
  f.read_unlocked(buf, 3);
  EXPECT_EQ(f.tell_unlocked(), off_t(3));

  // Pushback moves the logical position back by one.
  f.ungetc_unlocked('x');
  EXPECT_EQ(f.tell_unlocked(), off_t(2));

  // A relative seek is taken from the logical position, and discards the
  // pushed-back byte.
  EXPECT_EQ(f.seek_unlocked(1, SEEK_CUR), 0);
  EXPECT_EQ(f.tell_unlocked(), off_t(3));

  char c = 0;
  f.read_unlocked(&c, 1);
  EXPECT_EQ(c, 'd');
}

TEST(LlvmLibcFileVariantTest, SeekClearsEof) {
  TestBuffer tb;
  StaticFile f(&tb);

  char buf[4];
  f.read_unlocked(buf, sizeof(buf));
  EXPECT_TRUE(f.get_eof());

  f.seek_unlocked(0, SEEK_SET);
  EXPECT_FALSE(f.get_eof());
}

TEST(LlvmLibcFileVariantTest, OperationsOnClosedStreamFail) {
  TestBuffer tb;
  StaticFile f(&tb);

  EXPECT_EQ(f.close_unlocked(), 0);
  EXPECT_TRUE(f.is_closed());

  // A second close is an error rather than a second call into the backend.
  tb.closed = false;
  EXPECT_EQ(f.close_unlocked(), EOF);
  EXPECT_FALSE(tb.closed);

  EXPECT_TRUE(f.write_unlocked("x", 1).has_error());
  char buf[4];
  EXPECT_TRUE(f.read_unlocked(buf, sizeof(buf)).has_error());
  EXPECT_EQ(f.seek_unlocked(0, SEEK_SET), -1);
  EXPECT_EQ(f.ungetc_unlocked('a'), EOF);
  libc_errno = 0;
}

TEST(LlvmLibcFileVariantTest, CloseFailureReportsEof) {
  TestBuffer tb;
  tb.close_result = EOF;
  StaticFile f(&tb);

  EXPECT_EQ(f.close_unlocked(), EOF);
  EXPECT_TRUE(tb.closed);
}

TEST(LlvmLibcFileVariantTest, ZeroLengthTransfersAreNoOps) {
  TestBuffer tb;
  StaticFile f(&tb);

  EXPECT_EQ(f.write_unlocked("x", 0).value, size_t(0));
  EXPECT_EQ(f.read_unlocked(nullptr, 0).value, size_t(0));
  EXPECT_EQ(tb.size, size_t(0));
  EXPECT_FALSE(f.get_eof());
}

using StatelessTestBackend =
    LIBC_NAMESPACE::StaticBackend<test_ops, /*Stateless=*/true>;
using StatelessFile = LIBC_NAMESPACE::FileVariant<StatelessTestBackend>;
using HybridWithStateless =
    LIBC_NAMESPACE::FileVariant<StatelessTestBackend,
                                LIBC_NAMESPACE::DynamicBackend>;

static_assert(StatelessFile::is_stateless,
              "Single stateless alternative must make FileVariant stateless");
static_assert(!HybridWithStateless::is_stateless,
              "Adding a dynamic alternative must make FileVariant stateful");
static_assert(!StaticFile::is_stateless,
              "Default StaticBackend must remain stateful");

static TestBuffer global_stateless_cookie;

TEST(LlvmLibcFileVariantTest, StatelessStreamPassesCookieDirectly) {
  global_stateless_cookie = TestBuffer{};
  // In stateless mode, StandardStream::value is the cookie address itself,
  // with no FileVariant object materialized.
  ::FILE *stream = reinterpret_cast<::FILE *>(
      LIBC_NAMESPACE::StandardStream<StatelessFile,
                                     &global_stateless_cookie>::value);
  EXPECT_EQ(static_cast<void *>(stream),
            static_cast<void *>(&global_stateless_cookie));

  auto written = StatelessFile::write(stream, "bare", 4);
  EXPECT_FALSE(written.has_error());
  EXPECT_EQ(written.value, size_t(4));
  EXPECT_EQ(StatelessFile::tell(stream), off_t(4));

  EXPECT_EQ(StatelessFile::seek(stream, 0, SEEK_SET), 0);
  char buf[4] = {};
  auto read = StatelessFile::read(stream, buf, 4);
  EXPECT_FALSE(read.has_error());
  EXPECT_EQ(read.value, size_t(4));
  EXPECT_EQ(buf[0], 'b');
  EXPECT_EQ(buf[3], 'e');

  EXPECT_EQ(StatelessFile::flush(stream), 0);
  EXPECT_EQ(StatelessFile::get_eof(stream), 0);
  EXPECT_EQ(StatelessFile::get_err(stream), 0);
  EXPECT_EQ(StatelessFile::close(stream), 0);
  EXPECT_TRUE(global_stateless_cookie.closed);
}
