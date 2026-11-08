# macOS 性能、资源与网络优化记录

## 目标与基线

本轮针对正在运行的 Moonlight V+，来源为 `qiin2333/moonlight-qt`。已安装版本为
`6.2.92.0`；fork 为 <https://github.com/Weicoz/moonlight-qt-plus>，源码基点为 `9cf1d77`，
版本推导为 `6.5.4`。另一套 SkyHua 原生客户端不属于本次改动范围。

现场为 Apple M4、16 GiB 内存，1080p60、约 20 Mbps 串流。日志确认使用 VideoToolbox
硬件解码和 Vulkan/libplacebo 渲染。主进程瞬时 CPU 约 50%，一次采样的物理内存占用
为 488.3 MiB；RSS 与物理内存占用口径不同，不得混用。

网络日志记录了多次音频数据丢失。12 次 ICMP 探测没有丢包，最小/平均/最大延迟为
4.674/6.519/13.187 ms。短 ICMP 探测不能排除 UDP 串流丢包或长时间突发抖动。
原始日志与采样保留在本机临时目录，不进入 Git；本文不包含配对材料、主机地址或用户设置。

## 改动及边界

| 改动 | 来源 | 可核查的变化 |
|---|---|---|
| 主机监控等待 | `app/backend/computermanager.cpp`、`app/backend/computerpollingentry.h` | 保留三秒探测间隔；取消三十次 100 ms 分段睡眠，改用可立即中断的条件等待。减少的是定时唤醒，不代表总 CPU 降低同等比例。 |
| 渲染临时容器 | `app/streaming/video/ffmpeg-renderers/plvk.cpp` | 两个每帧分配的 vector 改为固定容量数组；保留旧纹理与新暂存纹理同时被回收的容量。每帧取消两次基础堆分配，实际帧时间收益未验证。 |
| 虚拟网卡识别 | `app/backend/nvcomputer.cpp`、`app/backend/networkinterfaceutils.h` | macOS 的 `feth<数字>` 使用已有 VPN 策略，自动视频包长由 1392 改为 1024 字节；用户手工包长仍优先。不修改系统路由、MTU、代理、AWDL 或 EasyTier。 |

`feth` 在现场被 Qt 识别为 Ethernet，接口 MTU 为 2800。这不能证明底层传输具备同样的
路径 MTU，原有局域网判断会绕过 VPN 的保守包长。新判断属于保守启发式；较小包长会
增加每秒包数，实际吞吐、CPU 与音频丢包改善必须通过串流实测确认。

主机轮询类从原头文件搬出，以便测试生产线程管理代码；中断、回收和析构等待均保留。
网络识别复用原有策略，不增加用户开关或配置迁移。

## 验证

新增 `tests/computer_polling_entry/`，覆盖三秒等待、进入等待前取消、等待中取消、
连续三十次启停和旧线程退出前启动替代线程。扩充 `tests/shared_helpers/`，覆盖
`feth0`、现场网卡名称和无关名称的识别，同时保留证书、版本比较与兼容性检查。
两组测试均已在 macOS arm64、Qt 6.11.1 上通过；Windows CI 已接入，尚未执行。

构建与测试目录均位于 Git 忽略的 `build/`。可复查命令：

```bash
make -C build/tests/computer_polling_entry -j2
build/tests/computer_polling_entry/computer_polling_entry
make -C build/tests/shared_helpers -j2
build/tests/shared_helpers/shared_helpers
git diff --check
```

完整客户端构建使用 Qt 6.11.1、Xcode 与 arm64 Release。SDK 为 macOS 27；Qt 会提示
其已测试 SDK 为 26，测试通过不消除该兼容性边界。构建日志为 `build/native/build.log`。
`make -j6 release` 已通过，CLI `--help` 启动检查已通过，版本推导的五项既有测试已通过。
CLI 检查使用构建产物内的 `portable.dat` 隔离配置，未使用原客户端的配对材料。
该产物是源码构建，尚未打包为自包含安装包，也未替换当前安装。

工作分支为 `optimize/macos-resources-network`；提交与构建状态以 Git 和 Actions 为准。

## 第二轮：未响应与退出崩溃

发现旧版 `6.2.92.0` 的真实报告 `Moonlight-2026-11-08-232834.ips`：主线程停在
`QThreadPool::waitForDone()`，后台线程在 Qt 信号激活区域发生无效地址访问。
报告缺少后台 Moonlight 函数的调试符号，不能只凭该栈确认具体 C++ 函数。
原始报告留在本机 DiagnosticReports，不进入 Git。

`app/main.cpp` 在 GUI 事件循环结束后同步等待线程池；旧版及源码基点的
`DeferredSessionCleanupTask::run()` 却使用 `BlockingQueuedConnection` 等待主线程
停止麦克风，形成循环等待。后台任务还会提前发送 `sessionFinished`，并在任务析构时
通过原始 Session 指针发送信号，存在界面退出与对象回收交错的风险。
Qt 对线程池退出等待和对象生命周期的约定见
[QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html)、
[QObject](https://doc.qt.io/qt-6/qobject.html)。

本轮修复位于 `app/streaming/session.cpp`、`app/streaming/sessioncleanupcompletion.h`：

- 三条会话清理入口统一先在所属线程停止麦克风，后台任务不再同步等待 GUI。
- 清理期间明确由应用持有 Session，避免 QML 引擎先销毁对象。
- 清理完成后通过应用线程队列通知界面；通知后用 `deleteLater` 释放 Session。
- 在线程池任务析构时不再发送 Session 信号。结束通知前释放会话信号量，避免新会话等待旧清理。
- 需要退出主机应用时保存主机信息快照，后台工作不再依赖界面持有的主机对象。

`tests/session_cleanup_completion/` 直接调用生产完成回调与对象保留代码。
先用同步回调复现失败：`FAIL: cleanup worker depends on the stopped GUI event loop`；
改为异步队列后通过。覆盖 GUI 已停止时后台任务仍可退出、回调位于主线程、对象销毁后
不执行回调、QML 引擎退出时保留清理对象，以及完成后释放对象。
修复后的 arm64 Release 完整构建、CLI 启动、原有轮询与共享辅助逻辑回归均已通过。
二轮构建日志为 `build/native/build-hang-fix.log`，CLI 日志为
`build/native/cli-smoke-hang-fix.log`。

该验证确认了退出清理的死锁机制及修复，不等于所有运行中卡顿均已复现。剪贴板、
GPU/窗口等待、睡眠恢复、网络重连和长时间串流仍需对应场景的采样及运行验收。
本轮未修改输入焦点、键盘锁定、系统网络或内存资料采集逻辑。

## 待验证与验收办法

本轮未主动中断旧客户端串流，已安装客户端与设置未被替换。编译和辅助逻辑测试不能证明最终
画面、声音、输入或网络已改善。

性能对比必须使用同一源码基点 `9cf1d77` 的原版与本次改动版，而非直接把已安装的
`6.2.92.0` 与新版本比较后把全部收益归因于本次改动。固定同一主机、场景、窗口、
1080p60、编码和码率，每组重复三次，分别记录 CPU、物理内存占用、视频/音频丢包、
解码和呈现耗时。网络与渲染参数一次只改一个。

源码已有 Vulkan、Metal 和 AVSampleBufferDisplayLayer 三条 macOS 呈现路径，自动模式
当前优先 Vulkan，见 `app/streaming/video/ffmpeg.cpp` 的 `createHwAccelRenderer()`。
原生路径是下一轮 CPU/功耗对比的候选；没有实测前不更改默认选择，也不保证所有
HDR、色彩和叠加显示行为一致。

降低码率、切换直连或关闭 AWDL 均不属于已验证结果。本轮没有修改主机 Sunshine、
远端设备、系统网络配置或用户原有设置。
