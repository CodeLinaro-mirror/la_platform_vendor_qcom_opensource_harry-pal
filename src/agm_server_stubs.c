/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <errno.h>
#include <stdint.h>

struct agm_shmem_info;

int agm_shmem_buf_alloc(struct agm_shmem_info *buf_info)
{
    (void)buf_info;
    return -ENOSYS;
}

int agm_shmem_buf_free(uint32_t spf_mem_handle)
{
    (void)spf_mem_handle;
    return 0;
}
