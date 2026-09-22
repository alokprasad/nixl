/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-FileCopyrightText: Copyright (c) 2026 Marvell Technology, Inc. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "worker/nixl/nixl_worker_odm.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <strings.h>

#include "odm_ioctl.h"
#include "utils/utils.h"

namespace {

bool
odmIoctlSizeOk(size_t size) {
    if (size > UINT32_MAX) {
        std::cerr << "ODM: size " << size << " exceeds 32-bit ioctl field limit" << std::endl;
        return false;
    }
    return true;
}

} // namespace

namespace xferBenchOdm {

void
configureBackend(const std::vector<std::string> &devices,
                 State &state,
                 nixl_b_params_t &backend_params) {
    const std::string odm_device = (devices.empty() || devices[0] == "all") ? "odm0" : devices[0];
    backend_params["dmadev_param"] = odm_device;
    state.device_path_ = (odm_device[0] == '/') ? odm_device : ("/dev/" + odm_device);
    backend_params["odm_qid"] = std::to_string(xferBenchConfig::odm_qid_start);
    backend_params["odm_qid_start"] = std::to_string(xferBenchConfig::odm_qid_start);
    backend_params["odm_qid_end"] = std::to_string(xferBenchConfig::odm_qid_end);
    backend_params["num_threads"] = std::to_string(xferBenchConfig::num_threads);
    bool use_io_uring = xferBenchConfig::odm_use_io_uring;
    if (!use_io_uring) {
        if (const char *uring_env = getenv("ODM_USE_IO_URING")) {
            if (uring_env[0] == '1' || strcasecmp(uring_env, "true") == 0 ||
                strcasecmp(uring_env, "yes") == 0) {
                use_io_uring = true;
            }
        }
    }
    if (use_io_uring) {
        backend_params["odm_use_io_uring"] = "1";
    }
    state.explicit_base_addr_ = explicitBaseAddrFromEnv();
    std::cout << "MARVELL_ODM backend: dma_device=" << odm_device
              << " qid=" << xferBenchConfig::odm_qid_start
              << " qid_range=" << xferBenchConfig::odm_qid_start << ".."
              << xferBenchConfig::odm_qid_end << " threads=" << xferBenchConfig::num_threads
              << " io_uring=" << (use_io_uring ? "on" : "off")
              << " engine=ODM/dma-buf (both directions)"
              << " device_iova=" << (state.explicit_base_addr_ != 0 ? "ODM_ADDR override" :
                                                                    "auto (plugin GET_IOVA)")
              << std::endl;
}

uint64_t
explicitBaseAddrFromEnv() {
    if (const char *e = getenv("ODM_ADDR")) {
        const uint64_t v = strtoull(e, nullptr, 0);
        if (v != 0) {
            std::cout << "ODM: using explicit base 0x" << std::hex << v << std::dec
                      << " (ODM_ADDR env)" << std::endl;
            return v;
        }
    }
    return 0;
}

void
State::resolveDeviceIovas(nixlAgent &agent,
                          nixlBackendH *backend,
                          std::vector<xferBenchIOV> &iovs) {
    if (iovs.empty() || backend == nullptr) {
        return;
    }
    nixl_reg_dlist_t desc_list = iovListToNixlRegDlist(iovs, DRAM_SEG);
    nixl_opt_args_t opt_args;
    opt_args.backends.push_back(backend);
    std::vector<nixl_query_resp_t> resp;
    const nixl_status_t rc = agent.queryMem(desc_list, resp, &opt_args);
    if (rc != NIXL_SUCCESS) {
        std::cerr << "ODM: queryMem failed after registerMem" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (resp.size() != iovs.size()) {
        std::cerr << "ODM: queryMem returned unexpected response count" << std::endl;
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < iovs.size(); ++i) {
        if (iovs[i].addr != 0) {
            continue;
        }
        if (!resp[i].has_value() || resp[i]->count("device_iova") == 0) {
            std::cerr << "ODM: missing device IOVA for auto-allocated registration" << std::endl;
            exit(EXIT_FAILURE);
        }
        const uint64_t device_iova = std::stoull((*resp[i])["device_iova"], nullptr, 0);
        iovs[i].handle = static_cast<unsigned long long>(device_iova);
    }
}

void
State::seedViaHostWrite(uint64_t device_iova, size_t size, uint8_t pattern) {
    if (size == 0 || device_iova == 0) {
        return;
    }
    const std::string &dev = device_path_.empty() ? xferBenchConfig::odm_device_path : device_path_;
    static constexpr size_t kSeedChunk = 0x400000ULL; /* 4 MiB, matches ODM ioctl u32 limit */

    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "ODM: host seed: open(" << dev << ") failed: " << strerror(errno) << std::endl;
        return;
    }

    size_t offset = 0;
    while (offset < size) {
        const size_t chunk = std::min(size - offset, kSeedChunk);
        if (!odmIoctlSizeOk(chunk)) {
            close(fd);
            return;
        }
        void *host = nullptr;
        if (posix_memalign(&host, xferBenchConfig::page_size, chunk) != 0) {
            std::cerr << "ODM: host seed: allocation failed for chunk " << chunk << std::endl;
            close(fd);
            return;
        }
        memset(host, pattern, chunk);

        struct mrvl_dma_xfer_commands cmd{};
        cmd.host_va_addr = reinterpret_cast<uint64_t>(host);
        cmd.target_iova_addr = device_iova + offset;
        cmd.tranfer_size = static_cast<uint32_t>(chunk);
        cmd.tranfer_type = ODM_XTYPE_INBOUND;
        cmd.qid = 0;
        if (ioctl(fd, MRVL_CXL_DMA_WRITE_COMMAND, &cmd) < 0) {
            std::cerr << "ODM: host seed: WRITE ioctl at IOVA 0x" << std::hex
                      << (device_iova + offset) << std::dec << " failed: " << strerror(errno)
                      << std::endl;
            free(host);
            close(fd);
            return;
        }
        free(host);
        offset += chunk;
    }
    close(fd);
}

void
State::seedRegisteredBuffers(const std::vector<NixlMemRegion> &remote_regs,
                             size_t total_size,
                             uint8_t pattern) {
    size_t seeded = 0;
    for (const auto &reg : remote_regs) {
        for (const auto &iov : reg.iovs()) {
            if (seeded >= total_size) {
                return;
            }
            const size_t chunk = std::min(iov.len, total_size - seeded);
            const uint64_t device_iova = iov.handle ? iov.handle : iov.addr;
            seedViaHostWrite(device_iova, chunk, pattern);
            seeded += chunk;
        }
    }
    std::cout << "ODM: seeded " << seeded << " bytes with 0x" << std::hex
              << static_cast<unsigned>(pattern) << std::dec << " (host WRITE)" << std::endl;
}

void
State::seedDramForRead(const std::vector<NixlMemRegion> &remote_regs, size_t total_size) {
    /*
     * Same pattern as POSIX/GDS: pre-fill the remote storage with the expected
     * byte (0xaa) before a READ benchmark via host WRITE ioctl.
     */
    seedRegisteredBuffers(remote_regs, total_size, XFERBENCH_TARGET_BUFFER_ELEMENT);
}

} // namespace xferBenchOdm
