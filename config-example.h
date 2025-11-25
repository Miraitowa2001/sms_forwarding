/*
 * 配置文件 - 存放敏感信息
 * 
 * 重要提示：
 * 1. 请将此文件添加到 .gitignore 中，避免提交到代码仓库
 * 2. 请根据实际情况修改以下配置
 * 3. 可以复制此文件为 config.h.example 作为模板（去除敏感信息）
 */

#ifndef CONFIG_H
#define CONFIG_H

// ==================== WiFi 配置 ====================
#define WIFI_SSID "xx"        // WiFi 名称
#define WIFI_PASS "xxx"    // WiFi 密码

// ==================== 企业微信机器人配置 ====================
// 替换成你自己的完整 Webhook URL
const char* WECHAT_WEBHOOK_URL = "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=xxxxxxxxxxxxxxxx";

// ==================== SIM 卡信息 ====================
// 当前设备使用的接收手机号（用来区分多张卡）
const char* LOCAL_SIM_NUMBER = "xxx";

// ==================== SMTP 邮件配置 ====================
#define SMTP_SERVER "smtp.qq.com"//smtp服务器
#define SMTP_SERVER_PORT 465//smtp服务器端口
#define SMTP_USER "xxx@qq.com"//登陆邮箱号
#define SMTP_PASS "xxx"//登录密码，注意qq邮箱需要去生成专用授权码
#define SMTP_SEND_TO "xxxx@qq.com"//收邮件的邮箱号

// ==================== HTTP 服务器配置 ====================
#define HTTP_SERVER_URL "http://your-server.com/api/sms"  // 自定义服务器 URL

// ==================== 推送方式开关 ====================
// 设置为 1 启用，设置为 0 禁用，可同时启用多种推送方式
#define ENABLE_WECOM_BOT    1   // 企业微信机器人推送
#define ENABLE_EMAIL        1   // 邮件推送
#define ENABLE_HTTP_SERVER  0   // HTTP 服务器推送

#endif // CONFIG_H
