#pragma once

#include <arch/context.h>
#include <kernel/com/util.h>

// NOTE: since this kernel's architecture is old and bad, some functions are
// expected to handle both user addresses and kernel addresses. The *_maybe_user
// variants exist for this very reason: they skip checking if the address is in
// the user range, and instead jump straight into the fault-tollerant copy. If
// the address is in the kernel range and a fault occurs, it is treated as a
// normal kernel fault (i.e., it panics); otherwise it is treated as a user
// fault and returns EFAULT

int memcpy_maybe_user(void *dst, const void *src, usize buffsize);
int memmove_maybe_user(void *dst, void *src, usize n);
int memset_maybe_user(void *buff, u8 val, usize buffsize);

int memcpy_to_user(void *dst, const void *src, usize buffsize);
int memcpy_from_user(void *dst, const void *src, usize buffsize);
int memset_user(void *buff, u8 val, usize buffsize);

// Called by the fault handler: resumes the running user copy with EFAULT
bool usercopy_fixup(Context *ctx);
