# pthread-win32（Windows POSIX Threads 头文件）

Vendoring 自 pthread-win32 / pthreads4w，仅公共头文件。

```
pthread.h
sched.h
semaphore.h
_ptw32.h
config.h
```

Windows 下由 `core/CMakeLists.txt` 将本目录加入 `xprobe` 的 PUBLIC include，
从而可直接 `#include <pthread.h>`。非 Windows 不加入，避免覆盖系统头。

当前仅头文件；链接实现需另行提供 `pthreadVC*.lib` / DLL（静态链接时定义
`PTW32_STATIC_LIB`）。
