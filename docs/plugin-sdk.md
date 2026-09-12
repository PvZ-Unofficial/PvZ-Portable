# 原生插件 SDK

Portable 的游戏、修改器、事件默认动作和 DLL 加载器均由本仓库构建，不需要
Rust 工具链或 RustVsZombies 源码。Windows x64 支持一个活动插件；其他平台
保留普通游戏路径，不提供 Windows DLL 加载。

## 构建与使用

沿用本项目正常 CMake 的 MSVC/vcpkg 或 GNU UCRT64 构建方式。游戏构建同时生成
`<build>/sdk/<config>/`，包含原生头文件、布局清单、导入库、构建配置及
`pvzp-sdk.cmake`。C++ 消费者可以 `include` 该文件并链接 `pvzp-sdk` target。
`PVZP_BUILD_PLUGIN_TESTS=ON` 另外构建不依赖 rsvz 的最小 C++ 插件。

启动游戏前把 `PVZP_PLUGIN` 设置为 DLL 完整路径；不设置时正常运行。只在启动
读取一次，换插件需要关闭并重启游戏。游戏不解析脚本配置，也不使用外部注入器。

固定导出只有三个 C ABI 入口：

```cpp
uint32_t pvzp_plugin_abi_version(); // 当前 5
int32_t pvzp_plugin_initialize();  // 0 成功
int32_t pvzp_plugin_shutdown();    // 0 表示资源已清理，可以卸载
```

在有效 App 建立后、loader lock 外执行初始化。插件必须先把 SDK 的 `Layout`
交给 `PvzpPlugin::ValidateLayout`，成功后才能访问对象或登记回调。全局构造器
不得提前操作游戏对象。App 持有插件句柄、回调和装卸状态；战斗回调还须在每次
ExitFight 撤销、EnterFight 按当前兴趣重新登记，Board 销毁前宿主也会撤销。
生存换轮可能复用 Board，不能把战斗回调寿命等同于 Board 寿命。

插件通过 `RequestStop()` 请求停止。宿主等最外层回调返回后停止派发、执行
shutdown、撤销入口，再卸载 DLL。初始化失败也清理部分登记并调用 shutdown。
shutdown 非零或抛出异常时保留 DLL 和未释放资源，报告失败并禁止继续加载；
重复停止请求不会重复释放。运行报告写入失败不等同于资源清理失败。

## ABI 与导出清单

SDK 必须与 EXE 来自同一构建。MSVC 游戏配 MSVC ABI 的 clang-cl；GNU 游戏配
UCRT64/libstdc++ ABI 的 Clang。必须匹配 x64、C++20、STL、运行库、Debug/Release、
PVZ_DEBUG 和 LOW_MEMORY；MSVC Release 使用 /MD，Debug 使用 /MDd。
不承诺跨版本、MSVC/GNU 混用或不同 STL/运行库组合。

`PluginLayoutFields.inc` 由 `tools/generate-plugin-layout.py` 维护；EXE 和插件
各自用自己的编译器计算大小、对齐、非虚基类偏移和字段偏移/大小，EXE 比较
实际值。范围包括 App、Board、实体、选卡/光标、档案、花园及六种对象池，
包括池的私有存储指针、ID 数组、元素步长和对齐。GNU x64 的 GameObject
尾填充是游戏自身的 ABI 规则。导出仅列实际需要的原生符号，清单位于
`CMake/native-msvc.def` 和 `CMake/native-gnu.def`，并由声明上的 `PVZP_API` 保证
LTO 不删除导出函数。Debug 断言入口和原生数据也按实际引用标记；游戏没有
`pvzp_rs_*` 包装。`tests/plugin/check_exports.py` 检查全部导出 RVA 有效。

## 同步输入

原生输入可能同步进入商店或图鉴的等待循环。此时继续原生 UI 更新，普通插件
脚本派发暂停；点击调用直到窗口关闭才返回，不能依靠下一条脚本操作关闭窗口。
嵌套更新使用正常原生更新次数；停止/卸载在最外层回调结束后执行。

## 验证

`tests/plugin/run_matrix.py` 使用指定 SDK，以 Clang 构建独立 DLL 并启动真实
游戏，覆盖无插件、正常装卸、重启再加载、版本错误、App/Board/池布局错误、
部分初始化失败和 shutdown 非零。每次使用临时档案并保留 JSON 结果；示例：

```powershell
python tests/plugin/run_matrix.py --abi msvc --sdk <sdk/Release> --game <game.exe> --resources <resources> --profile <test-profile> --output <results>
```

GNU 使用 `--abi gnu`。资源与档案均由调用者提供，不自动获取或修改用户常用档案。
画面、声音和商店/图鉴的人工操作步骤由 rsvz 的本轮验证记录单独列出。
