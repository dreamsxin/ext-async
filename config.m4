PHP_ARG_ENABLE(task, whether to enable task support,
[  --enable-task          Enable task support], no)

if test "$PHP_TASK" != "no"; then
  AC_DEFINE(HAVE_TASK, 1, [ ])
  
  TASK_CFLAGS="-Wall -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1"
  
  task_use_asm="yes"
  task_use_ucontext="no"
  
  AC_CHECK_HEADER(ucontext.h, [
    task_use_ucontext="yes"
  ])

  task_source_files="php_task.c \
    src/fiber.c \
    src/fiber_stack.c \
    src/task.c"
  
  AS_CASE([$host_cpu],
    [x86_64*], [task_cpu="x86_64"],
    [x86*], [task_cpu="x86"],
    [arm*], [task_cpu="arm"],
    [arm64*], [task_cpu="arm64"],
    [task_cpu="unknown"]
  )
  
  AS_CASE([$host_os],
    [darwin*], [task_os="MAC"],
    [cygwin*], [task_os="WIN"],
    [mingw*], [task_os="WIN"],
    [task_os="LINUX"]
  )
  
  if test "$task_cpu" = 'x86_64'; then
    if test "$task_os" = 'LINUX'; then
      task_asm_file="x86_64_sysv_elf_gas.S"
    elif test "$task_os" = 'MAC'; then
      task_asm_file="x86_64_sysv_macho_gas.S"
    else
      task_use_asm="no"
    fi
  elif test "$task_cpu" = 'x86'; then
    if test "$task_os" = 'LINUX'; then
      task_asm_file="i386_sysv_elf_gas.S"
    elif test "$task_os" = 'MAC'; then
      task_asm_file="i386_sysv_macho_gas.S"
    else
      task_use_asm="no"
    fi
  elif test "$task_cpu" = 'arm'; then
    if test "$task_os" = 'LINUX'; then
      task_asm_file="arm_aapcs_elf_gas.S"
    elif test "$task_os" = 'MAC'; then
      task_asm_file="arm_aapcs_macho_gas.S"
    else
      task_use_asm="no"
    fi
  else
    task_use_asm="no"
  fi
  
  if test "$task_use_asm" = 'yes'; then
    task_source_files="$task_source_files \
      src/fiber_asm.c \
      boost/asm/make_${task_asm_file} \
      boost/asm/jump_${task_asm_file}"
  elif test "$task_use_ucontext" = 'yes'; then
    task_source_files="$task_source_files \
      src/fiber_ucontext.c" 
  fi
  
  PHP_NEW_EXTENSION(task, $task_source_files, $ext_shared,, \\$(TASK_CFLAGS))
  PHP_SUBST(TASK_CFLAGS)
  PHP_ADD_MAKEFILE_FRAGMENT
  
  PHP_INSTALL_HEADERS([ext/task], [config.h include/*.h])
fi
