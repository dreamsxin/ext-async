# Libuv Integration

LIBUV VERSION: 1.22.0
MINIMUM VERSION: 1.19.0 (due to handle data void ptr)

## Static linking

Use static linking of libuv to avoid dependency on externally installed libuv.

### Linux

Add `CC_CHECK_CFLAGS_APPEND([-fPIC])` in `configure.ac`.

### Windows

Lib files are build using MSVC dev shell. Need to compile both x86 and x64 versions. Libuv's build system requires Python 2 to be installed (Python 3 does not work!).

## Installation on Linux

Compiling libuv might fail on ubuntu 14 depending on libtool / autoconf version. Workaround: copy libuv files from thirdparty to a tmp folder outside of the project. Build libuv as usual. Provide the path to the diretory containing the generated `include` and `lib` folder to the async extension via `--with-uv={PATH_TO_FOLDER}`.
