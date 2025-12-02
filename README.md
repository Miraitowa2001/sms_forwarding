# ESP32 短信转发器

基于 ESP32 的短信转发解决方案，可将收到的短信自动转发到企业微信、邮箱或自定义 HTTP 服务器，并提供 Web 管理界面。

## 功能特性

- 📱 **多渠道推送**：支持企业微信机器人、邮件、HTTP 服务器三种推送方式
- 🌐 **Web 管理界面**：内置 Web 服务器，支持在线配置、查看队列、发送短信
- 💾 **配置持久化**：配置保存到 NVS，重启后自动恢复
- 🔄 **失败重试**：发送失败自动入队重试，最多重试 5 次，60 秒间隔
- 📶 **WiFi 自动重连**：断线自动重连，指数退避策略（最大 60 秒）
- 🐕 **看门狗保护**：60 秒超时，防止程序死锁
- 🔁 **定时重启**：每 7 天自动重启，保持系统稳定
- 📊 **健康监控**：每 12 小时检查堆内存，碎片化严重时自动重启
- 📤 **主动发送短信**：通过 Web 界面或 AT 指令主动发送短信

## 硬件需求

- ESP32 开发板
- 4G/LTE 模组（支持 AT 指令，如 SIM7600、EC20 等）
- SIM 卡

## 接线说明

| ESP32 | 4G 模组 |
|-------|---------|
| GPIO3 (TXD) | RXD |
| GPIO4 (RXD) | TXD |
| GND | GND |
| 5V/VIN | VCC |

## 快速开始

### 1. 安装依赖库

在 Arduino IDE 中安装以下库：

- [pdulib](https://github.com/mgaman/PDUlib) - PDU 短信解析
- [ReadyMail](https://github.com/mobizt/ReadyMail) - SMTP 邮件发送

### 2. 配置

复制 `config-example.h` 为 `config.h`，并填入你的配置：

```cpp
// WiFi 配置
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASS "你的WiFi密码"

// 本机 SIM 卡号码（用于区分多设备）
const char* LOCAL_SIM_NUMBER = "你的手机号";

// 推送方式开关（1 启用，0 禁用）
#define ENABLE_WECOM_BOT    1   // 企业微信机器人
#define ENABLE_EMAIL        0   // 邮件
#define ENABLE_HTTP_SERVER  0   // HTTP 服务器

// 企业微信机器人 Webhook URL
const char* WECHAT_WEBHOOK_URL = "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=你的key";

// SMTP 邮件配置
#define SMTP_SERVER "smtp.qq.com"
#define SMTP_SERVER_PORT 465
#define SMTP_USER "你的邮箱"
#define SMTP_PASS "你的授权码"
#define SMTP_SEND_TO "接收邮箱"

// HTTP 服务器配置
#define HTTP_SERVER_URL "http://your-server.com/api/sms"
```

### 3. 上传程序

1. 选择开发板：`ESP32 Dev Module`
2. 选择正确的串口
3. 点击上传

## Web 管理界面

程序启动后会在 80 端口提供 Web 管理界面，通过 `http://ESP32的IP地址` 访问。

### 默认账号密码

- 用户名：`admin`
- 密码：`admin123`

> 可在 Web 界面或 `config.h` 中修改默认账号密码

### 功能页面

| 页面 | 路径 | 功能 |
|------|------|------|
| 首页 | `/` | 发送短信表单 |
| 配置 | `/config` | 修改推送渠道、SMTP、Webhook 等配置 |
| 队列 | `/queue` | 查看待重试的短信队列 |

### 可配置项

通过 Web 界面可在线修改以下配置（修改后自动保存到 NVS）：

- 企业微信 Webhook URL
- 接收号码（本机 SIM 卡号）
- SMTP 服务器、端口、用户名、密码、收件人
- HTTP 服务器 URL
- 各推送渠道的启用/禁用开关
- Web 管理账号密码

## 企业微信机器人配置

1. 在企业微信中创建群聊
2. 右键群聊 → 添加群机器人
3. 复制 Webhook URL 到配置文件或 Web 配置页面

## HTTP 服务器接口

如果启用 HTTP 服务器推送，服务器需要接收以下 JSON 格式：

```json
POST /api/sms
Content-Type: application/json

{
  "sender": "发送者号码",
  "message": "短信内容",
  "timestamp": "时间戳"
}
```

## 串口调试

程序运行时会通过 USB 串口（115200 波特率）输出调试信息：

- 短信接收与解析日志
- 各推送渠道发送状态
- WiFi 连接状态
- 堆内存健康检查报告
- 支持透传 AT 指令到 4G 模组

## 技术参数

| 参数 | 值 |
|------|-----|
| 看门狗超时 | 60 秒 |
| HTTP 请求超时 | 15 秒 |
| 短信队列大小 | 20 条 |
| 最大重试次数 | 5 次 |
| 重试间隔 | 60 秒 |
| WiFi 重连间隔 | 5-60 秒（指数退避） |
| 定时重启周期 | 7 天 |
| 健康检查间隔 | 12 小时 |

## 常见问题

### Q: AT 指令无响应？
A: 检查接线是否正确，确认 4G 模组供电充足（建议独立供电）。

### Q: WiFi 连接失败？
A: 确认 WiFi 名称和密码正确，ESP32 仅支持 2.4GHz WiFi。

### Q: 短信收不到？
A: 确认 SIM 卡已激活、有信号，可通过串口发送 `AT+CSQ` 查看信号强度。

### Q: 邮件发送失败？
A: QQ 邮箱需要使用授权码而非登录密码，请在 QQ 邮箱设置中开启 SMTP 并获取授权码。

### Q: Web 界面无法访问？
A: 确认 ESP32 已连接 WiFi，查看串口输出的 IP 地址，确保访问设备与 ESP32 在同一局域网。

### Q: 配置修改后不生效？
A: 配置修改后会自动保存到 NVS，部分配置（如 WiFi）可能需要重启后生效。


## 致谢

- [pdulib](https://github.com/mgaman/PDUlib)
- [ReadyMail](https://github.com/mobizt/ReadyMail)

