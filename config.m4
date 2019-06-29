PHP_ARG_ENABLE(async, Whether to enable "async" support,
[ --enable-async          Enable "async" support], no)

PHP_ARG_WITH(openssl, OpenSSL dir for "async",
[ --with-openssl[=DIR] Openssl dev lib directory], yes, no)

PHP_ARG_WITH(valgrind, Whether to enable "valgrind" support,
[ --with-valgrind[=DIR] Valgrind dev lib directory], yes, no)

if test "$PHP_ASYNC" != "no"; then
  AC_DEFINE(HAVE_ASYNC, 1, [ ])
  
  AS_CASE([$host_cpu],
    [x86_64*], [async_cpu="x86_64"],
    [x86*], [async_cpu="x86"],
    [arm*], [async_cpu="arm"],
    [arm64*], [async_cpu="arm64"],
    [async_cpu="unknown"]
  )
  
  AS_CASE([$host_os],
    [darwin*], [async_os="MAC"],
    [async_os="LINUX"]
  )

  ASYNC_CFLAGS="-Wall -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1"
  LDFLAGS="$LDFLAGS -lpthread"
  
  case $host in
    *linux*)
      LDFLAGS="$LDFLAGS -z now"
  esac
  
  # Setup source files.
  
  async_source_files=" \
    php_async.c \
    src/channel.c \
    src/console.c \
    src/context.c \
    src/deferred.c \
    src/dns.c \
    src/event.c \
    src/fiber/stack.c \
    src/filesystem.c \
    src/helper.c \
    src/pipe.c \
    src/process/builder.c \
    src/process/env.c \
    src/process/runner.c \
    src/socket.c \
    src/ssl/api.c \
    src/ssl/bio.c \
    src/ssl/engine.c \
    src/stream.c \
    src/sync.c \
    src/task.c \
    src/tcp.c \
    src/thread.c \
    src/udp.c \
    src/watcher/monitor.c \
    src/watcher/poll.c \
    src/watcher/signal.c \
    src/watcher/timer.c \
    src/xp/socket.c \
    src/xp/tcp.c \
    src/xp/udp.c
  "
  
  PHP_ADD_BUILD_DIR($ext_builddir/src)
  PHP_ADD_BUILD_DIR($ext_builddir/src/fiber)
  PHP_ADD_BUILD_DIR($ext_builddir/src/process)
  PHP_ADD_BUILD_DIR($ext_builddir/src/ssl)
  PHP_ADD_BUILD_DIR($ext_builddir/src/watcher)
  PHP_ADD_BUILD_DIR($ext_builddir/src/xp)
  
  # Check for valgrind if support is requested.
  
  if test "$PHP_VALGRIND" != "no"; then
    AC_MSG_CHECKING([for valgrind header])

    if test "$PHP_VALGRIND" = "yes"; then
      SEARCH_PATH="/usr/local /usr"
    else
      SEARCH_PATH="$PHP_VALGRIND"
    fi

    SEARCH_FOR="/include/valgrind/valgrind.h"
    
    for i in $SEARCH_PATH ; do
      if test -r $i/$SEARCH_FOR; then
        VALGRIND_DIR=$i
      fi
    done

    if test -z "$VALGRIND_DIR"; then
      AC_MSG_RESULT([not found])
    else
      AC_MSG_RESULT(found in $VALGRIND_DIR)
      AC_DEFINE(HAVE_VALGRIND, 1, [ ])
    fi
  fi
  
  # Fiber backend selection.
  
  async_use_asm="yes"
  async_use_ucontext="no"
  
  AC_CHECK_HEADER(ucontext.h, [
    async_use_ucontext="yes"
  ])
  
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
      src/fiber/asm.c \
      thirdparty/boost/asm/make_${async_asm_file} \
      thirdparty/boost/asm/jump_${async_asm_file}"
  elif test "$async_use_ucontext" = 'yes'; then
    async_source_files="$async_source_files \
      src/fiber/ucontext.c"
  fi
  
  # Check SSL lib support.

  if test "$PHP_OPENSSL" != "no"; then
    PHP_SETUP_OPENSSL(ASYNC_SHARED_LIBADD, [
      AC_MSG_CHECKING(for SSL support)
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_ASYNC_SSL, 1, [ ])
    ], [
      AC_MSG_CHECKING(for SSL support)
      AC_MSG_RESULT(no)
    ])
  fi
  
  # Embedded build of libuv because most OS package managers provide obsolete uv packages only.
  # Code is based on "configure.ac" and "Makefile.am" shipped with libuv.
  
  AC_CHECK_LIB([dl], [dlopen])
  AC_CHECK_LIB([kstat], [kstat_lookup])
  AC_CHECK_LIB([nsl], [gethostbyname])
  AC_CHECK_LIB([perfstat], [perfstat_cpu])
  AC_CHECK_LIB([pthread], [pthread_mutex_init])
  AC_CHECK_LIB([rt], [clock_gettime])
  AC_CHECK_LIB([sendfile], [sendfile])
  AC_CHECK_LIB([socket], [socket])
  
  AC_CHECK_HEADERS([sys/ahafs_evProds.h])
  
  AS_CASE([$host_os],
    [darwin*], [uv_os="MAC"],
    [openbsd*], [uv_os="OPENBSD"],
    [*freebsd*], [uv_os="FREEBSD"],
    [netbsd*], [uv_os="NETBSD"],
    [dragonfly*], [uv_os="DRAGONFLY"],
    [solaris*], [uv_os="SUNOS"],
    [uv_os="LINUX"]
  )
  
  UV_DIR="thirdparty/libuv"
  
  PHP_ADD_INCLUDE("${UV_DIR}/include")
  PHP_ADD_INCLUDE("${UV_DIR}/src")
  
  # Base files
  UV_SRC=" \
    ${UV_DIR}/src/fs-poll.c \
    ${UV_DIR}/src/idna.c \
    ${UV_DIR}/src/inet.c \
    ${UV_DIR}/src/strscpy.c \
    ${UV_DIR}/src/threadpool.c \
    ${UV_DIR}/src/timer.c \
    ${UV_DIR}/src/uv-data-getter-setters.c \
    ${UV_DIR}/src/uv-common.c \
    ${UV_DIR}/src/version.c
  "
  # UNIX files
  UV_SRC="$UV_SRC \
    ${UV_DIR}/src/unix/async.c \
    ${UV_DIR}/src/unix/core.c \
    ${UV_DIR}/src/unix/dl.c \
    ${UV_DIR}/src/unix/fs.c \
    ${UV_DIR}/src/unix/getaddrinfo.c \
    ${UV_DIR}/src/unix/getnameinfo.c \
    ${UV_DIR}/src/unix/loop-watcher.c \
    ${UV_DIR}/src/unix/loop.c \
    ${UV_DIR}/src/unix/pipe.c \
    ${UV_DIR}/src/unix/poll.c \
    ${UV_DIR}/src/unix/process.c \
    ${UV_DIR}/src/unix/signal.c \
    ${UV_DIR}/src/unix/stream.c \
    ${UV_DIR}/src/unix/tcp.c \
    ${UV_DIR}/src/unix/thread.c \
    ${UV_DIR}/src/unix/tty.c \
    ${UV_DIR}/src/unix/udp.c
  "
  
  # Mac OS
  if test "$uv_os" = 'MAC'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/bsd-ifaddrs.c \
      ${UV_DIR}/src/unix/darwin.c \
      ${UV_DIR}/src/unix/darwin-proctitle.c \
      ${UV_DIR}/src/unix/fsevents.c \
      ${UV_DIR}/src/unix/kqueue.c \
      ${UV_DIR}/src/unix/proctitle.c
    "
    LDFLAGS="$LDFLAGS -lutil"
    
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D_DARWIN_USE_64_BIT_INODE=1"
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D_DARWIN_UNLIMITED_SELECT=1"
  fi
  
  # DragonFly BSD
  if test "$uv_os" = 'DRAGONFLY'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/bsd-ifaddrs.c \
      ${UV_DIR}/src/unix/bsd-proctitle.c \
      ${UV_DIR}/src/unix/freebsd.c \
      ${UV_DIR}/src/unix/kqueue.c \
      ${UV_DIR}/src/unix/posix-hrtime.c
    "
    LDFLAGS="$LDFLAGS -lutil"
  fi
  
  # FreeBSD
  if test "$uv_os" = 'FREEBSD'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/bsd-ifaddrs.c \
      ${UV_DIR}/src/unix/bsd-proctitle.c \
      ${UV_DIR}/src/unix/freebsd.c \
      ${UV_DIR}/src/unix/kqueue.c \
      ${UV_DIR}/src/unix/posix-hrtime.c
    "
    LDFLAGS="$LDFLAGS -lutil"
  fi
  
  # Linux
  if test "$uv_os" = 'LINUX'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/linux-core.c \
      ${UV_DIR}/src/unix/linux-inotify.c \
      ${UV_DIR}/src/unix/linux-syscalls.c \
      ${UV_DIR}/src/unix/procfs-exepath.c \
      ${UV_DIR}/src/unix/proctitle.c \
      ${UV_DIR}/src/unix/sysinfo-loadavg.c
    "
    LDFLAGS="$LDFLAGS -lutil"
    
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D_GNU_SOURCE"
  fi
  
  # NetBSD
  if test "$uv_os" = 'NETBSD'; then
  	AC_CHECK_LIB([kvm], [kvm_open])
  	
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/bsd-ifaddrs.c \
      ${UV_DIR}/src/unix/bsd-proctitle.c \
      ${UV_DIR}/src/unix/kqueue.c \
      ${UV_DIR}/src/unix/netbsd.c \
      ${UV_DIR}/src/unix/posix-hrtime.c
    "
    LDFLAGS="$LDFLAGS -lutil"
  fi
  
  # OpenBSD
  if test "$uv_os" = 'OPENBSD'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/bsd-ifaddrs.c \
      ${UV_DIR}/src/unix/bsd-proctitle.c \
      ${UV_DIR}/src/unix/kqueue.c \
      ${UV_DIR}/src/unix/openbsd.c \
      ${UV_DIR}/src/unix/posix-hrtime.c
    "
    LDFLAGS="$LDFLAGS -lutil"
  fi
  
  # Solaris
  if test "$uv_os" = 'SUNOS'; then
    UV_SRC="$UV_SRC \
      ${UV_DIR}/src/unix/no-proctitle.c \
      ${UV_DIR}/src/unix/sunos.c
    "
    
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D__EXTENSIONS__"
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D_XOPEN_SOURCE=500"
    ASYNC_CFLAGS="$ASYNC_CFLAGS -D_REENTRANT"
  fi
  
  AS_CASE([$host_os], [kfreebsd*], [
    LDFLAGS="$LDFLAGS -lfreebsd-glue"
  ])
  
  PHP_ADD_BUILD_DIR($ext_builddir/thirdparty/libuv/src)
  PHP_ADD_BUILD_DIR($ext_builddir/thirdparty/libuv/src/unix)
  
  async_source_files="$async_source_files $UV_SRC"
  
  # Build async extension.
  
  PHP_NEW_EXTENSION(async, $async_source_files, $ext_shared,, \\$(ASYNC_CFLAGS))
  PHP_SUBST(ASYNC_CFLAGS)
  PHP_SUBST(ASYNC_SHARED_LIBADD)
  PHP_ADD_MAKEFILE_FRAGMENT
  
  PHP_INSTALL_HEADERS([ext/async], [config.h thirdparty/libuv/include/*.h])
  
fi
