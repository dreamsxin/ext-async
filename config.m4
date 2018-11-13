PHP_ARG_ENABLE(async, Whether to enable "async" support,
[ --enable-async          Enable "async" support], no)

PHP_ARG_WITH(openssl-dir, OpenSSL dir for "async",
[ --with-openssl-dir[=DIR] Openssl install prefix], no, no)

if test "$PHP_ASYNC" != "no"; then
  AC_DEFINE(HAVE_ASYNC, 1, [ ])
  
  DIR="${srcdir}/thirdparty"

  AC_MSG_CHECKING(for static libuv)

  if test ! -s "${DIR}/lib/libuv.a"; then
    AC_MSG_RESULT(no)
    
    TMP=$(mktemp -d)
    cp -r ${DIR}/libuv $TMP
    pushd ${TMP}/libuv
    ./autogen.sh
    ./configure --prefix=${TMP}/build CFLAGS="$(CFLAGS) -fPIC -DPIC -g -O2"
    make install
    popd
    mv ${TMP}/build/lib/libuv.a ${DIR}/lib
    rm -rf $TMP
  else
    AC_MSG_RESULT(yes)
  fi

  ASYNC_CFLAGS="-Wall -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1"
  LDFLAGS="$LDFLAGS -lpthread"
  
  case $host in
    *linux*)
      LDFLAGS="$LDFLAGS -z now"
  esac
  
  async_use_asm="yes"
  async_use_ucontext="no"
  
  AC_CHECK_HEADER(ucontext.h, [
    async_use_ucontext="yes"
  ])

  async_source_files="php_async.c \
    src/awaitable.c \
    src/context.c \
    src/deferred.c \
    src/dns.c \
    src/fiber.c \
    src/fiber_stack.c \
    src/filesystem.c \
    src/process.c \
    src/signal_watcher.c \
    src/socket.c \
    src/ssl.c \
    src/stream.c \
    src/stream_watcher.c \
    src/task.c \
    src/task_scheduler.c \
    src/tcp.c \
    src/timer.c \
    src/udp.c \
    src/xp/socket.c \
    src/xp/tcp.c \
    src/xp/udp.c
  "
  
  AS_CASE([$host_cpu],
    [x86_64*], [async_cpu="x86_64"],
    [x86*], [async_cpu="x86"],
    [arm*], [async_cpu="arm"],
    [arm64*], [async_cpu="arm64"],
    [async_cpu="unknown"]
  )
  
  AS_CASE([$host_os],
    [darwin*], [async_os="MAC"],
    [cygwin*], [async_os="WIN"],
    [mingw*], [async_os="WIN"],
    [async_os="LINUX"]
  )
  
  if test "$async_cpu" = 'x86_64'; then
    if test "$async_os" = 'LINUX'; then
      async_asm_file="x86_64_sysv_elf_gas.S"
    elif test "$async_os" = 'MAC'; then
      async_asm_file="x86_64_sysv_macho_gas.S"
    else
      async_use_asm="no"
    fi
  elif test "$async_cpu" = 'x86'; then
    if test "$async_os" = 'LINUX'; then
      async_asm_file="i386_sysv_elf_gas.S"
    elif test "$async_os" = 'MAC'; then
      async_asm_file="i386_sysv_macho_gas.S"
    else
      async_use_asm="no"
    fi
  elif test "$async_cpu" = 'arm'; then
    if test "$async_os" = 'LINUX'; then
      async_asm_file="arm_aapcs_elf_gas.S"
    elif test "$async_os" = 'MAC'; then
      async_asm_file="arm_aapcs_macho_gas.S"
    else
      async_use_asm="no"
    fi
  else
    async_use_asm="no"
  fi
  
  if test "$async_use_asm" = 'yes'; then
    async_source_files="$async_source_files \
      src/fiber_asm.c \
      thirdparty/boost/asm/make_${async_asm_file} \
      thirdparty/boost/asm/jump_${async_asm_file}"
  elif test "$async_use_ucontext" = 'yes'; then
    async_source_files="$async_source_files \
      src/fiber_ucontext.c"
  fi
  
  PHP_ADD_INCLUDE("$srcdir/thirdparty/libuv/include")
  
  if test "$TRAVIS" = ""; then
    ASYNC_SHARED_LIBADD="$ASYNC_SHARED_LIBADD -L${srcdir}/thirdparty/lib -luv"
  else
    dnl Stupid workaround for travis build, for some reason the linker refuses to use a subdirectory of the project?!
    
    TMP=$(mktemp -d)
    cp ${srcdir}/thirdparty/lib/libuv.a ${TMP}/libuv.a
    ASYNC_SHARED_LIBADD="$ASYNC_SHARED_LIBADD -L${TMP} -luv"
  fi
  
  if test "$PHP_OPENSSL" = ""; then
    AC_CHECK_HEADER(openssl/evp.h, [
      PHP_OPENSSL='yes'
    ], [
      PHP_OPENSSL='no'
    ])
  fi

  if test "$PHP_OPENSSL" != "no" || test "$PHP_OPENSSL_DIR" != "no"; then
    PHP_SETUP_OPENSSL(ASYNC_SHARED_LIBADD, [
      AC_MSG_CHECKING(for SSL support)
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_ASYNC_SSL, 1, [ ])
    ], [
      AC_MSG_CHECKING(for SSL support)
      AC_MSG_RESULT(no)
    ])
  fi
  
  PHP_NEW_EXTENSION(async, $async_source_files, $ext_shared,, \\$(ASYNC_CFLAGS))
  PHP_SUBST(ASYNC_CFLAGS)
  PHP_SUBST(ASYNC_SHARED_LIBADD)
  PHP_ADD_MAKEFILE_FRAGMENT
  
  PHP_INSTALL_HEADERS([ext/async], [config.h thirdparty/libuv/include/*.h])
  
fi
