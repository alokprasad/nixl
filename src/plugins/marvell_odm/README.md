# MARVELL_ODM NIXL Plugin

Marvell ODM moves data between GPU VRAM and Iliad/Structera device memory using
the ODM DMA controller with GPU VRAM exported as a dma-buf (`VRAM_SEG <->
ODM_MEM_SEG`).

## Build

```bash
meson setup build -Denable_plugins=MARVELL_ODM
ninja -C build
```

Disable with `-Ddisable_odm_backend=true`.

## Requirements

- CUDA with GPUDirect dma-buf export (`CU_DEVICE_ATTRIBUTE_DMA_BUF_SUPPORTED`)
- ODM kernel character device (for example `/dev/odm0`)
- VRAM allocated with dma-buf-exportable memory (CUDA VMM or `cudaMalloc`)
- Kernel module `mrvl_cxl_pcie` with ODM support

## Addressing

DMA targets use mailbox-allocated IOVA from `GET_IOVA` on `/dev/odm0`. Override
with the `ODM_ADDR` environment variable when `GET_IOVA` is unavailable:

```bash
export ODM_ADDR=0x800000000
```

## io_uring / SQE128

The plugin supports an optional io_uring `uring_cmd` submit path when the kernel
module parameter `odm_use_io_uring=1` is set:

```bash
echo 1 > /sys/module/mrvl_cxl_pcie/parameters/odm_use_io_uring
export ODM_USE_IO_URING=1
```

SQE128 rings are used when supported by the kernel and liburing.

## Plugin Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `dmadev_param` | ODM device name or path | `odm0` |
| `odm_qid` | Primary queue ID | `0` |
| `odm_qid_start` | First queue in range | `0` |
| `odm_qid_end` | Last queue in range | `7` |
| `odm_use_io_uring` | Enable io_uring submit | off |
| `num_threads` | Worker thread count | `1` |

## nixlbench

```bash
export LD_LIBRARY_PATH=build/src/core:build/src/plugins/marvell_odm
export NIXL_PLUGIN_DIR=build/src/plugins/marvell_odm
export ODM_ADDR=0x800000000

./nixlbench --backend MARVELL_ODM \
  --initiator_seg_type VRAM --target_seg_type DRAM \
  --device_list odm0 --op_type WRITE
```

See `benchmark/nixlbench/marvell_odm/README.md` for sweep examples and TOML config.

## Tests

```bash
ninja -C build test/unit/plugins/marvell_odm/marvell_odm_nixl_test

# Hardware round-trip (requires /dev/odm0 and CUDA GPU):
./build/test/unit/plugins/marvell_odm/marvell_odm_nixl_test \
  --device odm0 --odm-addr 0x800000000

# With io_uring:
./build/test/unit/plugins/marvell_odm/marvell_odm_nixl_test \
  --device odm0 --odm-addr 0x800000000 --io-uring
```

Register in meson test suite with `-Denable_odm_hw_tests=true` when hardware is
present.

## See Also

- nixlbench examples: `benchmark/nixlbench/marvell_odm/`
- Main nixlbench README: `benchmark/nixlbench/README.md`
