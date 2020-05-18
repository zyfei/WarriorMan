PHP_ARG_ENABLE(workerman, whether to enable workerman support,
Make sure that the comment is aligned:
[  --enable-workerman           Enable workerman support])

# AC_CANONICAL_HOST

if test "$PHP_WM" != "no"; then
    PHP_ADD_LIBRARY(pthread)
    WM_ASM_DIR="thirdparty/boost/asm/"
    CFLAGS="-Wall -pthread $CFLAGS"

    dnl 这段是用来判断我们机器所使用的操作系统是什么类型的，然后把操作系统的类型赋值给变量WM_OS。
    dnl 因为，我们的这个扩展只打算支持Linux，所以，我这个里面只写了Linux。
    AS_CASE([$host_os],
      [linux*], [WM_OS="LINUX"],
      []
    )
    dnl 这段是判断CPU的类型，然后赋值给变量WM_CPU。
    AS_CASE([$host_cpu],
      [x86_64*], [WM_CPU="x86_64"],
      [x86*], [WM_CPU="x86"],
      [i?86*], [WM_CPU="x86"],
      [arm*], [WM_CPU="arm"],
      [aarch64*], [WM_CPU="arm64"],
      [arm64*], [WM_CPU="arm64"],
      []
    )

    dnl 这段是判断应该用什么类型的汇编文件
    if test "$WM_CPU" = "x86_64"; then
        if test "$WM_OS" = "LINUX"; then
            WM_CONTEXT_ASM_FILE="x86_64_sysv_elf_gas.S"
        fi
    elif test "$WM_CPU" = "x86"; then
        if test "$WM_OS" = "LINUX"; then
            WM_CONTEXT_ASM_FILE="i386_sysv_elf_gas.S"
        fi
    elif test "$WM_CPU" = "arm"; then
        if test "$WM_OS" = "LINUX"; then
            WM_CONTEXT_ASM_FILE="arm_aapcs_elf_gas.S"
        fi
    elif test "$WM_CPU" = "arm64"; then
        if test "$WM_OS" = "LINUX"; then
            WM_CONTEXT_ASM_FILE="arm64_aapcs_elf_gas.S"
        fi
    elif test "$WM_CPU" = "mips32"; then
        if test "$WM_OS" = "LINUX"; then
            WM_CONTEXT_ASM_FILE="mips32_o32_elf_gas.S"
        fi
    fi

    dnl 把我们需要编译的所有文件已字符串的方式存入到变量workerman_source_file里面。
    workerman_source_file="\
    	src/core/base.cc \
    	src/core/log.cc \
    	src/core/socket.cc \
    	src/core/hashmap.cc \
    	src/core/error.cc \
    	src/core/timer.cc \
    	src/core/wm_string.cc \
    	src/core/channel.cc \
    	src/core/coroutine_socket.cc \
    	src/model/Context.cc \
    	src/model/Coroutine.cc \
    	src/WorkerCoroutine.cc \
    	php_coroutine.cc \
        php_workerman.cc \
        php_socket.cc \
        php_channel.cc \
        ${WM_ASM_DIR}make_${WM_CONTEXT_ASM_FILE} \
        ${WM_ASM_DIR}jump_${WM_CONTEXT_ASM_FILE}
    "

    dnl 声明这个扩展的名称、需要的源文件名、此扩展的编译形式。其中$ext_shared代表此扩展是动态库，使用cxx的原因是，我们的这个扩展使用C++来编写。
    PHP_NEW_EXTENSION(workerman, $workerman_source_file, $ext_shared,,, cxx)

    dnl 用来添加额外的包含头文件的目录。
    PHP_ADD_INCLUDE([$ext_srcdir])
    PHP_ADD_INCLUDE([$ext_srcdir/include])

    dnl 把我们的workerman扩展目录里面的*.h、config.h、include/*.h、thirdparty/*.h复制到： php-config --include-dir
    dnl 下的ext/workerman里面。这个是在执行make install的时候会进行复制。我们待会会看到。
    PHP_INSTALL_HEADERS([ext/workerman], [*.h config.h include/*.h thirdparty/*.h])

    dnl 我们使用了C++，所以我们需要指明一下。（没有这句会编译出错）
    PHP_REQUIRE_CXX()

    dnl 编译C++时候，用到的编译选项。
    CXXFLAGS="$CXXFLAGS -Wall -Wno-unused-function -Wno-deprecated -Wno-deprecated-declarations"
    dnl 编译C++时候，用到的编译选项。
    if test "$WM_OS" = "CYGWIN" || test "$WM_OS" = "MINGW"; then
        CXXFLAGS="$CXXFLAGS -std=gnu++11"
    else
        CXXFLAGS="$CXXFLAGS -std=c++11"
    fi

    dnl 指定这个扩展需要被编译到的文件的目录。因为我们需要编译boost提供的代码，所以需要进行指定。
    PHP_ADD_BUILD_DIR($ext_builddir/thirdparty/boost)
    PHP_ADD_BUILD_DIR($ext_builddir/thirdparty/boost/asm)
fi