# ESP32 短信转发器

基于 ESP32 的短信转发解决方案，可将收到的短信自动转发到企业微信、邮箱或自定义 HTTP 服务器。

## 功能特性

- 📱 **多渠道推送**：支持企业微信机器人、邮件、HTTP 服务器三种推送方式
- 🔄 **失败重试**：发送失败自动入队重试，最多重试 5 次
- 📶 **WiFi 自动重连**：断线自动重连，指数退避策略
- 🐕 **看门狗保护**：防止程序死锁
- 🔁 **定时重启**：每 7 天自动重启，保持系统稳定
- 📊 **健康监控**：定期检查堆内存，碎片化严重时自动重启

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

复制 `config.h.example` 为 `config.h`，并填入你的配置：

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

## 企业微信机器人配置

1. 在企业微信中创建群聊
2. 右键群聊 → 添加群机器人
3. 复制 Webhook URL 到配置文件

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

程序运行时会通过 USB 串口（115200 波特率）输出调试信息，同时支持透传 AT 指令到 4G 模组。

## 常见问题

### Q: AT 指令无响应？
A: 检查接线是否正确，确认 4G 模组供电充足（建议独立供电）。

### Q: WiFi 连接失败？
A: 确认 WiFi 名称和密码正确，ESP32 仅支持 2.4GHz WiFi。

### Q: 短信收不到？
A: 确认 SIM 卡已激活、有信号，可通过串口发送 `AT+CSQ` 查看信号强度。

### Q: 邮件发送失败？
A: QQ 邮箱需要使用授权码而非登录密码，请在 QQ 邮箱设置中开启 SMTP 并获取授权码。

## 许可证

MIT License

## 致谢

- [pdulib](https://github.com/mgaman/PDUlib)
- [ReadyMail](https://github.com/mobizt/ReadyMail)
