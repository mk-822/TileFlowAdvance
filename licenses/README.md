# Dependency notices

Tile Flow Advance uses Butano 21.3.0 and its Maxmod audio backend. The licenses below cover the engine and the bundled components used by this build. [Butano 21.3.0 source](https://github.com/GValiente/butano/tree/21.3.0)

| Component | Notice |
| --- | --- |
| Butano | [zlib](Butano.txt) |
| Maxmod | [Maxmod license](Maxmod.txt) |
| libtonc | [MIT](libtonc.txt) |
| libugba | [MIT](libugba.txt) |
| agbabi | [zlib](agbabi.md) |
| gba-modern | [MIT](gba-modern.md) |
| gba-link-connection | [MIT](gba-link-connection.txt) |
| Cult of GBA BIOS routines | [MIT](cult-of-gba-bios.txt) |
| posprintf | [Public Domain notice](posprintf.txt) |

## ROM toolchain

- The devkitARM build includes devkitPro's GBA startup code under MPL-2.0. [Source for devkitarm-crtls 1.2.6](https://github.com/devkitPro/devkitarm-crtls/blob/v1.2.6/gba_crt0.s) · [MPL-2.0](https://www.mozilla.org/en-US/MPL/2.0/)
- GCC runtime code is covered by the GCC Runtime Library Exception 3.1. The exception permits distribution of target code produced by an eligible compilation process under the terms of the independent modules. [GCC runtime license and exception](https://gcc.gnu.org/onlinedocs/libstdc%2B%2B/manual/license.html)
