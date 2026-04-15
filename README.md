# EIPDevice

基于 Qt 和 OpENer 的 EtherNet/IP 通信框架，实现了 EtherNet/IP 协议的 Scanner（主站）和 Adapter（从站）两种角色。

## 项目结构

### EIPOriginator — EtherNet/IP Scanner（主站/客户端）

用于发现、连接和操作 EIP 设备，主要功能：

- **设备发现**：通过 UDP 广播（ListIdentity）扫描网络中的 EIP 设备
- **显式消息**：读写 Assembly 对象属性（Get/Set Attribute Single）
- **隐式 I/O**：通过 Forward Open 建立 I/O 连接，进行周期性数据交换
- **轮询**：支持定时自动读取 Assembly 数据

### EIPTarget — EtherNet/IP Adapter（从站/服务器）

基于 OpENer 协议栈实现 EIP 从设备，主要功能：

- **Assembly 对象**：Input (#100)、Output (#150)、Config (#151) 及心跳 Assembly
- **I/O 连接**：支持 Exclusive Owner、Input-Only、Listen-Only 三种连接模式
- **EDS 文件生成**：可导出用于工业控制器配置的 EDS 文件
- **无界面模式**：支持 `--headless --iface N` 命令行运行
- **可配置数据大小**：I/O Assembly 大小可在 1–512 字节范围内设置

### OpENer

ODVA 开源 EtherNet/IP 协议栈（C 语言），作为 EIPTarget 的底层协议实现。

## 技术栈

- **Qt 5**（Widgets、Network）— GUI 及网络通信
- **OpENer**（C）— EtherNet/IP/CIP 协议栈
- **C++ / C** — 应用层 / 协议层
- **QThread** — 后台运行 OpENer 事件循环，线程安全的 I/O 数据访问

## 构建

使用 Qt Creator 打开 `.pro` 文件，选择 MSVC2019 64-bit 套件进行构建。

## 许可证

MIT License
