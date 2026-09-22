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

#ifndef NIXL_BENCHMARK_NIXLBENCH_SRC_WORKER_NIXL_NIXL_WORKER_ODM_H
#define NIXL_BENCHMARK_NIXLBENCH_SRC_WORKER_NIXL_NIXL_WORKER_ODM_H

#include <cstdint>
#include <string>
#include <vector>

#include <nixl.h>

#include "worker/nixl/nixl_mem_region.h"

namespace xferBenchOdm {

struct State {
    std::string device_path_;
    uint64_t explicit_base_addr_ = 0; /* non-zero when ODM_ADDR override is set */

    void
    seedViaHostWrite(uint64_t device_iova, size_t size, uint8_t pattern);
    void
    seedRegisteredBuffers(const std::vector<NixlMemRegion> &remote_regs,
                          size_t total_size,
                          uint8_t pattern);
    void
    seedDramForRead(const std::vector<NixlMemRegion> &remote_regs, size_t total_size);
    void
    resolveDeviceIovas(nixlAgent &agent, nixlBackendH *backend, std::vector<xferBenchIOV> &iovs);
};

void
configureBackend(const std::vector<std::string> &devices,
                 State &state,
                 nixl_b_params_t &backend_params);

uint64_t
explicitBaseAddrFromEnv();

} // namespace xferBenchOdm

#endif // NIXL_BENCHMARK_NIXLBENCH_SRC_WORKER_NIXL_NIXL_WORKER_ODM_H
