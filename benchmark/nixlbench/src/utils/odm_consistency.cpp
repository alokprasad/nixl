/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-FileCopyrightText: Copyright (c) 2026 Marvell Technology, Inc. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils/odm_consistency.h"

#include <climits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "odm_ioctl.h"

namespace {

/*
 * Read device memory into a host buffer via MRVL_CXL_DMA_READ_COMMAND. This is
 * the ODM equivalent of POSIX/GDS consistency checking, which uses pread()
 * on the storage fd after a WRITE transfer.
 */
bool
odmHostReadDevice(const xferBenchIOV &iov, void **addr_out, bool *allocated_out) {
    *addr_out = nullptr;
    *allocated_out = false;

    if (iov.len > UINT32_MAX) {
        std::cerr << "ODM: consistency: iov length " << iov.len
                  << " exceeds 32-bit ioctl field limit" << std::endl;
        return false;
    }

    void *host = nullptr;
    if (posix_memalign(&host, xferBenchConfig::page_size, iov.len) != 0) {
        std::cerr << "ODM: consistency: host buffer alloc failed" << std::endl;
        return false;
    }
    *allocated_out = true;

    struct mrvl_dma_xfer_commands cmd{};
    cmd.host_va_addr = reinterpret_cast<uint64_t>(host);
    cmd.target_iova_addr = iov.addr;
    cmd.tranfer_size = static_cast<uint32_t>(iov.len);
    cmd.tranfer_type = ODM_XTYPE_OUTBOUND;
    cmd.qid = 0;

    int odm_fd = open(xferBenchConfig::odm_device_path.c_str(), O_RDWR);
    if (odm_fd < 0) {
        std::cerr << "ODM: consistency: open(" << xferBenchConfig::odm_device_path
                  << ") failed: " << strerror(errno) << std::endl;
        free(host);
        *allocated_out = false;
        return false;
    }

    const bool ok = ioctl(odm_fd, MRVL_CXL_DMA_READ_COMMAND, &cmd) == 0;
    close(odm_fd);
    if (!ok) {
        std::cerr << "ODM: consistency: host READ ioctl from IOVA 0x" << std::hex << iov.addr
                  << std::dec << " failed: " << strerror(errno) << std::endl;
        free(host);
        *allocated_out = false;
        return false;
    }

    *addr_out = host;
    return true;
}

} // namespace

OdmConsistencyContext::OdmConsistencyContext(
    const std::vector<std::vector<xferBenchIOV>> &iov_lists) {
    (void)iov_lists;
    if (xferBenchConfig::backend != XFERBENCH_BACKEND_MARVELL_ODM ||
        xferBenchConfig::op_type != XFERBENCH_OP_WRITE) {
        return;
    }
    active = true;
}

bool
OdmConsistencyContext::fetchWriteBuffer(const xferBenchIOV &iov,
                                        void **addr_out,
                                        bool *allocated_out) {
    *addr_out = nullptr;
    *allocated_out = false;
    if (!active) {
        return false;
    }
    return odmHostReadDevice(iov, addr_out, allocated_out);
}
