# qmake + nmake 批量推导规则导致 main.cpp 被 WIN32/main.c 覆盖的问题

## 问题现象

EIPTarget 项目编译时链接报错：

```
my_application.obj : error LNK2005: g_end_stack 已经在 main.obj 中定义
debug\EIPTarget.exe : fatal error LNK1169: 找到一个或多个多重定义的符号
```

实际现象：项目中 `src/main.cpp`（Qt 入口）没有被编译，反而是 OpENer 库中的
`OpENer/source/src/ports/WIN32/main.c`（包含 `g_end_stack` 定义和独立的 `main()` 函数）
被编译成了 `debug\main.obj`，与 `my_application.obj` 中的 `g_end_stack` 产生了重复定义。

## 根因分析

### qmake 的批量推导规则（Batch Inference Rules）

qmake 为 MSVC/nmake 生成 Makefile 时，会为 SOURCES 中涉及的**每个源目录**自动创建
批量推导规则（inference rules），形如：

```makefile
{..\..\..\..\OpENer\source\src\ports\WIN32}.c{debug\}.obj::
    $(CC) -c $(CFLAGS) $(INCPATH) -Fodebug\ @<<
    $<
<<
```

这条规则的含义是：WIN32 目录下的**任何** `.c` 文件都可以被自动编译为 `debug\` 下的
同名 `.obj`。

### 冲突触发过程

1. **EIPTarget.pro** 的 SOURCES 中包含了 `WIN32/networkhandler.c` 等文件
2. qmake 为 `WIN32` 目录生成了上述 `.c → .obj` 推导规则
3. 同时，项目自身的 `src/main.cpp` 在 SOURCES 中，需要编译为 `debug\main.obj`
4. nmake 在构建 `debug\main.obj` 时，发现 WIN32 目录下有 `main.c`
5. **nmake 的 `.c` 规则优先级高于 `.cpp` 规则**，于是使用 WIN32 的推导规则编译了
   `WIN32/main.c` → `debug\main.obj`
6. 结果 `debug\main.obj` 包含了 OpENer 的 `main()` 和 `g_end_stack`（而非 Qt 入口）
7. `my_application.c` 中也定义了 `g_end_stack` → LNK2005 重复定义

### 关键点

- **不是 INCLUDEPATH 的问题**——INCLUDEPATH 只影响头文件搜索，不编译源文件
- **不是 SOURCES 显式包含了 WIN32/main.c**——.pro 中只列了三个 WIN32 源文件
- **是 nmake 推导规则的隐式匹配**——只要目录路径出现在推导规则中，该目录下
  所有同名文件都可能被错误地"吸入"编译

## 解决方案

### 方案一：重命名 main.cpp（旧方案）

将 `src/main.cpp` 重命名为 `src/eiptarget_main.cpp`，消除与 `WIN32/main.c`
的文件名冲突。

**修改内容：**
- `src/main.cpp` → `src/eiptarget_main.cpp`（mv 重命名）
- `EIPTarget.pro` 中 SOURCES 对应行改为 `src/eiptarget_main.cpp`

**优点：** 改动最小，最简单可靠
**缺点：** 入口文件名不再是常规的 main.cpp

### 方案二：CONFIG += object_parallel_to_source（不适用 ❌）

qmake 提供 `object_parallel_to_source` 选项，让 .obj 文件保留源文件的目录层级结构，
例如：
- `src/main.cpp` → `debug/src/main.obj`
- `WIN32/main.c` → `debug/.../WIN32/main.obj`

**实测结果：** 不可用。当源文件通过 `$$PWD` 或 `../..` 引用时，qmake 将相对路径
解析为**绝对路径**再拼接到 obj 目录下，导致输出路径类似：
```
debug\Users\Administrator\Desktop\EipTest\OpENer\source\src\ports\WIN32\networkhandler.obj
```
路径过长（超过 260 字符限制），且目录不会自动创建，最终链接失败。
此方案仅适用于所有源文件都在项目目录内部的情况。

### 方案三：将 OpENer 编译为静态库（备选方案）

创建一个 subdirs 项目，先将 OpENer 源文件编译为 `opener.lib` 静态库，再在
EIPTarget 中链接该库。由于 OpENer 源文件在独立子项目中编译，EIPTarget 的
Makefile 不再包含 WIN32 目录的推导规则，`main.cpp` 可恢复原名。

**项目结构：**
```
EIPTarget/
  EIPTarget_all.pro          # subdirs 顶层项目（构建入口）
  EIPTarget.pro              # 应用项目（链接 opener.lib）
  opener_lib/
    opener_lib.pro           # 静态库项目（编译所有 OpENer C 源文件）
  src/
    main.cpp                 # Qt 入口
    ...
```

**构建方式：** 在 Qt Creator 中打开 `EIPTarget_all.pro`（而非 `EIPTarget.pro`），
选择 Kit 后构建。subdirs 会先编译 `opener_lib` 生成 `opener.lib`，再编译链接
`EIPTarget`。

**优点：** 彻底隔离 OpENer 和 Qt 的编译，无任何文件名冲突风险，main.cpp 恢复原名
**缺点：** 项目结构增加一层 subdirs，需要修改上游 OpENer 代码

> 注：`opener_lib/` 和 `EIPTarget_all.pro` 已创建在项目中可直接使用。

### 方案四：修改 OpENer 源码中的 main.c（不推荐 ❌）

在 `WIN32/main.c` 中加条件编译宏保护：
```c
#ifndef OPENER_NO_MAIN
int main(int argc, char *argv[]) { ... }
#endif
```

**缺点：** 侵入上游 OpENer 代码，后续更新 OpENer 时需要重新 patch。
且 `#ifndef` 无法阻止 nmake 推导规则将空的 main.c 编译为 `debug\main.obj`，
src/main.cpp 仍然不会被编译。**此方案不可行。**

### 方案五：重命名 OpENer 的 main.c（已采用 ✅）

将 `OpENer/source/src/ports/WIN32/main.c` 重命名为 `opener_main.c`。
文件名不再与 `src/main.cpp` 相同，nmake 推导规则不会再产生 `debug\main.obj`
来覆盖 Qt 入口。

**修改内容（仅 2 处）：**
- `OpENer/source/src/ports/WIN32/main.c` → `opener_main.c`（文件重命名）
- `OpENer/source/src/ports/WIN32/CMakeLists.txt` 第 20 行：
  `add_executable(OpENer main.c)` → `add_executable(OpENer opener_main.c)`

**优点：**
- 改动最小（1 次 rename + 1 行 CMakeLists）
- git 自动跟踪 rename，上游 merge 时对该文件的改动会自动合入
- EIPTarget.pro 恢复为最简单的直接编译方式，无需 subdirs / 静态库
- `src/main.cpp` 保持常规命名

**缺点：** 需修改 fork 仓库的 OpENer 源码（但改动极小且 merge-friendly）

## 注意事项

- Qt Creator 的 "Clean" 操作只清理当前 Makefile 中定义的目标文件，
  不会删除旧版本遗留的 .obj（如之前的 `eiptarget_main.obj`）。
  如果怀疑有残留文件干扰，建议手动删除整个 `debug/` 目录后再重新构建。
- 此问题只在 **MSVC + nmake** 下出现。MinGW/GCC 的 Makefile 使用不同的
  推导规则机制，可能不受影响。

## 文件变更记录

| 文件 | 变更 |
|------|------|
| `OpENer/source/src/ports/WIN32/main.c` → `opener_main.c` | **重命名** — 消除与 src/main.cpp 的文件名冲突 |
| `OpENer/source/src/ports/WIN32/CMakeLists.txt` | `add_executable(OpENer main.c)` → `opener_main.c` |
| `EIPTarget.pro` | 直接编译 OpENer 源码，不通过静态库 |

> 备选的静态库方案文件（`opener_lib/opener_lib.pro` 和 `EIPTarget_all.pro`）
> 保留在项目中，如需切换方案可直接使用。

---

*初次记录：2026-04-10*
*方案五实施：2026-04-10*
