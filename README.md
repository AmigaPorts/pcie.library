# pcie.library
[![CMake](https://github.com/amigaports/pcie.library/actions/workflows/cmake.yml/badge.svg)](https://github.com/amigaports/pcie.library/actions/workflows/cmake.yml)

Native PCI support for AmigaOS 3.x running on Emu68 PCs. Discovers host bridges
through `devicetree.resource` and provides PCI configuration, resource ownership,
address translation, DMA allocation and interrupt setup through `interrupt.library`.

## Compatibility

Implements the public API of rondoval's
[bcmpcie.library](https://github.com/rondoval/emu68-pcie-library).
`bcmpcie.library` is a runtime alias sharing the same devices, allocations and
interrupt state. New applications should use `pcie.library` and `<proto/pcie.h>`.
IOMMU-managed DMA is not yet supported; incompatible mappings are rejected.

## Building

Requires CMake, Git, `xxd`, and an `m68k-amigaos` cross-toolchain with AmigaOS
headers and `sfdc`. From a configured cross-toolchain environment:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DM68K_CPU=custom -DM68K_FPU=custom -DM68K_CRT=custom
cmake --build build --parallel
```

CPM fetches the pinned build helpers and public device-tree and interrupt SDKs.
No sibling projects are required. Outputs include `build/pcie.library`, generated
SDK bindings and `build/pcie.library.h` for ROM embedding.

## License

[Mozilla Public License 2.0](LICENSE). The implementation is independent;
no GPL implementation from the API reference is included.
