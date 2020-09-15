# Changelog

## [0.5.2] — 2020-08-03

 - Fixed: Fix compilation with MSVC/C++17.
   [#154](https://github.com/chfast/ethash/issues/154)

## [0.5.1] — 2020-01-30

 - Added: Experimental Python bindings — [ethash][pypi-ethash] package.
   [#123](https://github.com/chfast/ethash/pull/123)
   [#138](https://github.com/chfast/ethash/pull/138)
 - Added: More functions exposed in C API.
   [#136](https://github.com/chfast/ethash/pull/136)
 - Change: ProgPoW implementation updated to revision [0.9.3][ProgPoW-changelog].
   [#151](https://github.com/chfast/ethash/pull/151)

## [0.5.0] — 2019-06-07

 - Changed:
   The Keccak implementation has been moved to separate library "keccak", 
   available as ethash::keccak target in the ethash CMake package.
   [#131](https://github.com/chfast/ethash/pull/131)

## [0.4.4] — 2019-02-26

 - Fixed:
   Fix compilation on PowerPC architectures (-mtune=generic not supported there).
   [#125](https://github.com/chfast/ethash/pull/125)

## [0.4.3] — 2019-02-19

 - Added:
   The public `version.h` header with information about the ethash library version.
   [#121](https://github.com/chfast/ethash/pull/121)
 - Added:
   Ethash and ProgPoW revisions exposed as `{ethash,progpow}::revision` constants.
   [#121](https://github.com/chfast/ethash/pull/121)

## [0.4.2] — 2019-01-24

 - Fixed: The `progpow.cpp` file encoding changed from utf-8 to ascii.

## [0.4.1] — 2018-12-14

 - Added: [KISS99 PRNG](https://en.wikipedia.org/wiki/KISS_(algorithm) distribution tester tool.
 - Changed: ProgPoW implementation updated to revision [0.9.2][ProgPoW-changelog].
 - Changed: ProgPoW implementation optimizations.

## [0.4.0] — 2018-12-04

 - Added: Experimental support for [ProgPoW] [0.9.1][ProgPoW-changelog].


[0.5.2]: https://github.com/chfast/ethash/releases/tag/v0.5.2
[0.5.1]: https://github.com/chfast/ethash/releases/tag/v0.5.1
[0.5.0]: https://github.com/chfast/ethash/releases/tag/v0.5.0
[0.4.4]: https://github.com/chfast/ethash/releases/tag/v0.4.4
[0.4.3]: https://github.com/chfast/ethash/releases/tag/v0.4.3
[0.4.2]: https://github.com/chfast/ethash/releases/tag/v0.4.2
[0.4.1]: https://github.com/chfast/ethash/releases/tag/v0.4.1
[0.4.0]: https://github.com/chfast/ethash/releases/tag/v0.4.0

[ProgPoW]: https://github.com/ifdefelse/ProgPOW/blob/master/README.md
[ProgPoW-changelog]: https://github.com/ifdefelse/ProgPOW#change-history
[pypi-ethash]: https://pypi.org/project/ethash/
