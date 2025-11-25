#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <pdulib.h>
#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

// 看门狗超时（秒）
#define WDT_TIMEOUT_SEC 60

// HTTP 超时（毫秒）
#define HTTP_TIMEOUT_MS 15000

// 定时重启间隔（毫秒）- 默认每周重启一次
#define SCHEDULED_RESTART_INTERVAL_MS (7UL * 24UL * 60UL * 60UL * 1000UL) // 7天

// 引入配置文件（敏感信息在此文件中定义）
#include "config.h"

//串口映射
#define TXD 3
#define RXD 4

// JSON 字符串转义函数，防止特殊字符破坏 JSON 格式
String escapeJson(const char* str) {
  String result = "";
  while (*str) {
    char c = *str++;
    switch (c) {
      case '"':  result += "\\\""; break;   // 双引号
      case '\\': result += "\\\\"; break;   // 反斜杠
      case '\n': result += "\\n"; break;    // 换行
      case '\r': result += "\\r"; break;    // 回车
      case '\t': result += "\\t"; break;    // 制表符
      case '\b': result += "\\b"; break;    // 退格
      case '\f': result += "\\f"; break;    // 换页
      default:
        // 控制字符使用 Unicode 转义
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          result += buf;
        } else {
          result += c;
        }
    }
  }
  return result;
}


WiFiMulti WiFiMulti;
PDU pdu = PDU(4096);
WiFiClientSecure ssl_client;
SMTPClient smtp(ssl_client);

#define SERIAL_BUFFER_SIZE 500
#define MAX_PDU_LENGTH 300
char serialBuf[SERIAL_BUFFER_SIZE];
int serialBufLen = 0;

// 队列与重试配置
#define SMS_QUEUE_SIZE 20
#define SMS_MAX_RETRIES 5
#define SMS_RETRY_INTERVAL_MS 60000UL // 60s

// 固定长度缓冲区（避免堆碎片化）
#define SMS_SENDER_LEN 32
#define SMS_TEXT_LEN 320      // 支持长短信（约2条拼接）
#define SMS_TIMESTAMP_LEN 32

struct SMSItem {
  char sender[SMS_SENDER_LEN];
  char text[SMS_TEXT_LEN];
  char timestamp[SMS_TIMESTAMP_LEN];
  uint8_t retries;
  unsigned long lastAttempt;
  bool valid;  // 标记该槽位是否有效
};

SMSItem smsQueue[SMS_QUEUE_SIZE];
int sms_q_head = 0; // index of oldest
int sms_q_count = 0; // number of items

// WiFi 重连控制
unsigned long lastWifiAttempt = 0;
unsigned long wifiReconnectInterval = 5000; // 初始重连间隔 ms

// 系统启动时间（用于定时重启）
unsigned long bootTime = 0;


// 发送短信数据到服务器，按需修改，返回是否成功
bool sendSMSToServer(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("sendSMSToServer: WiFi 未连接");
    return false;
  }
  HTTPClient http;
  Serial.println("\n发送短信数据到服务器...");
  http.begin(HTTP_SERVER_URL);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  
  // 构造JSON（使用转义函数防止特殊字符破坏JSON格式）
  String jsonData = "{";
  jsonData += "\"sender\":\"" + escapeJson(sender) + "\",";
  jsonData += "\"message\":\"" + escapeJson(message) + "\",";
  jsonData += "\"timestamp\":\"" + escapeJson(timestamp) + "\"";
  jsonData += "}";
  Serial.println("发送数据: " + jsonData);
  int httpCode = http.POST(jsonData);
  bool ok = false;
  if (httpCode > 0) {
    Serial.printf("服务器响应码: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
      String response = http.getString();
      Serial.println("服务器响应: " + response);
      ok = true;
    }
  } else {
    Serial.printf("HTTP请求失败: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  return ok;
}

// 通过企业微信机器人发送短信内容
// 发送到企业微信机器人，返回是否成功
bool sendSMSToWeComBot(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("sendSMSToWeComBot: WiFi 未连接");
    return false;
  }

  HTTPClient http;
  http.begin(WECHAT_WEBHOOK_URL);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json; charset=utf-8");

  String content = "";
  content += "📩 【新短信提醒】\n";
  content += "📱 接收号码："; content += LOCAL_SIM_NUMBER; content += "\n";
  content += "👤 发送者："; content += sender; content += "\n";
  content += "⏰ 时间："; content += timestamp; content += "\n";
  content += "📝 内容："; content += message;

  String escapedContent = escapeJson(content.c_str());

  String jsonData = "{";
  jsonData += "\"msgtype\":\"text\",";
  jsonData += "\"text\":{";
  jsonData += "\"content\":\"" + escapedContent + "\"";
  jsonData += "}";
  jsonData += "}";

  Serial.println("发送到企业微信机器人: " + jsonData);

  int httpCode = http.POST(jsonData);
  bool ok = false;
  if (httpCode > 0) {
    Serial.printf("WeCom HTTP 响应码: %d\n", httpCode);
    String resp = http.getString();
    Serial.println("WeCom 响应: " + resp);
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) ok = true;
  } else {
    Serial.printf("WeCom HTTP 请求失败: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  return ok;
}

// 发送邮件通知，返回是否成功
bool sendSMSToEmail(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("sendSMSToEmail: WiFi 未连接");
    return false;
  }
  auto statusCallback = [](SMTPStatus status) {
    Serial.println(status.text);
  };
  smtp.connect(SMTP_SERVER, SMTP_SERVER_PORT, statusCallback);
  if (!smtp.isConnected()) {
    Serial.println("sendSMSToEmail: SMTP 连接失败");
    return false;
  }
  smtp.authenticate(SMTP_USER, SMTP_PASS, readymail_auth_password);

  SMTPMessage msg;
  String from = "sms notify <"; from+=SMTP_USER; from+=">"; 
  msg.headers.add(rfc822_from, from.c_str());
  String to = "your_email <"; to+=SMTP_SEND_TO; to+=">"; 
  msg.headers.add(rfc822_to, to.c_str());
  String subject = "短信";
  subject += sender;
  subject += ",";
  subject += message;
  msg.headers.add(rfc822_subject, subject.c_str());
  String body = "来自："; body+=sender; body+="，时间："; body+=timestamp; body+="，内容："; body+=message;
  msg.text.body(body.c_str());
  
  // NTP 同步（带超时保护，最多等待 10 秒）
  configTime(0, 0, "ntp.ntsc.ac.cn");
  unsigned long ntpStart = millis();
  while (time(nullptr) < 100000) {
    if (millis() - ntpStart > 10000) {
      Serial.println("sendSMSToEmail: NTP 同步超时");
      smtp.disconnect();
      return false;
    }
    delay(100);
    esp_task_wdt_reset(); // 喂狗防止超时
  }
  msg.timestamp = time(nullptr);
  bool res = smtp.send(msg);
  smtp.disconnect(); // 关闭连接，释放资源
  if (!res) Serial.println("sendSMSToEmail: 发送失败");
  return res;
}

// 读取串口一行（含回车换行），返回行字符串，无新行时返回空
String readSerialLine(HardwareSerial& port) {
  static char lineBuf[SERIAL_BUFFER_SIZE];
  static int linePos = 0;

  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      lineBuf[linePos] = 0;
      String res = String(lineBuf);
      linePos = 0;
      return res;
    } else if (c != '\r') {  // 跳过\r
      if (linePos < SERIAL_BUFFER_SIZE - 1)
        lineBuf[linePos++] = c;
      else
        linePos = 0;  //超长报错保护，重头计
    }
  }
  return "";
}

// 检查字符串是否为有效的十六进制PDU数据
bool isHexString(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// 处理URC和PDU
void checkSerial1URC() {
  static enum { IDLE,
                WAIT_PDU } state = IDLE;

  String line = readSerialLine(Serial1);
  if (line.length() == 0) return;

  // 打印到调试串口
  Serial.println("Debug> " + line);

  if (state == IDLE) {
    // 检测到短信上报URC头
    if (line.startsWith("+CMT:")) {
      Serial.println("检测到+CMT，等待PDU数据...");
      state = WAIT_PDU;
    }
  } else if (state == WAIT_PDU) {
    // 跳过空行
    if (line.length() == 0) {
      return;
    }
    
    // 如果是十六进制字符串，认为是PDU数据
    if (isHexString(line)) {
      Serial.println("收到PDU数据: " + line);
      Serial.println("PDU长度: " + String(line.length()) + " 字符");
      
      // 解析PDU
      if (!pdu.decodePDU(line.c_str())) {
        Serial.println("❌ PDU解析失败！");
      } else {
        Serial.println("✓ PDU解析成功");
        Serial.println("=== 短信内容 ===");
        Serial.println("发送者: " + String(pdu.getSender()));
        Serial.println("时间戳: " + String(pdu.getTimeStamp()));
        Serial.println("内容: " + String(pdu.getText()));
        Serial.println("===============");

        // 根据配置开关执行各推送方式：先尝试立即发送，失败则入队重试
        const char* senderPtr = pdu.getSender();
        const char* textPtr = pdu.getText();
        const char* timestampPtr = pdu.getTimeStamp();

        bool allOk = true;
      #if ENABLE_WECOM_BOT
        if (!sendSMSToWeComBot(senderPtr, textPtr, timestampPtr)) allOk = false;
      #endif
      #if ENABLE_HTTP_SERVER
        if (!sendSMSToServer(senderPtr, textPtr, timestampPtr)) allOk = false;
      #endif
      #if ENABLE_EMAIL
        if (!sendSMSToEmail(senderPtr, textPtr, timestampPtr)) allOk = false;
      #endif
        if (!allOk) {
          Serial.println("部分或全部发送失败，入队以便重试");
          enqueueSMS(senderPtr, textPtr, timestampPtr);
        }
      }
      
      // 返回IDLE状态
      state = IDLE;
    } 
    // 如果是其他内容（OK、ERROR等），也返回IDLE
    else {
      Serial.println("收到非PDU数据，返回IDLE状态");
      state = IDLE;
    }
  }
}

void blink_short(unsigned long gap_time = 500) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap_time);
}

bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
  }
  return false;
}

bool waitCGATT1() {
  Serial1.println("AT+CGATT?");
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 2000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("+CGATT: 1") >= 0) return true;
      if (resp.indexOf("+CGATT: 0") >= 0) return false;
    }
  }
  return false;
}

void setup() {
  // 记录启动时间
  bootTime = millis();
  
  // 初始化看门狗（防止死锁）
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
  Serial1.setRxBufferSize(SERIAL_BUFFER_SIZE);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASS);
  Serial.println("连接wifi");
  while (WiFiMulti.run() != WL_CONNECTED) blink_short();
  Serial.println("wifi已连接");
  ssl_client.setInsecure();
  while (!sendATandWaitOK("AT", 1000)) {
    Serial.println("AT未响应，重试...");
    blink_short();
  }
  Serial.println("模组AT响应正常");
  while (!sendATandWaitOK("AT+CNMI=2,2,0,0,0", 1000)) {
    Serial.println("设置CNMI失败，重试...");
    blink_short();
  }
  Serial.println("CNMI参数设置完成");
  while (!waitCGATT1()) {
    Serial.println("等待CGATT附着...");
    blink_short();
  }
  Serial.println("CGATT已附着");
  digitalWrite(LED_BUILTIN, LOW);
}

// 堆内存监控间隔
static unsigned long lastHeapCheck = 0;
#define HEAP_CHECK_INTERVAL_MS 300000UL // 5 分钟

// 计算运行时间（天）
unsigned long getUptimeDays() {
  return (millis() - bootTime) / (24UL * 60UL * 60UL * 1000UL);
}

void loop() {
  // 喂狗
  esp_task_wdt_reset();
  
  // 定时重启检查（仅在队列为空时重启，避免丢失数据）
  if ((millis() - bootTime) >= SCHEDULED_RESTART_INTERVAL_MS && sms_q_count == 0) {
    Serial.println("\n🔄 已运行7天，执行计划重启以保持系统稳定...");
    Serial.flush();
    delay(100);
    ESP.restart();
  }
  
  // 本地透传
  if (Serial.available()) Serial1.write(Serial.read());
  // 尝试保持 WiFi 连接
  ensureWiFiConnected();
  // 处理队列中的待重试短信
  processSMSQueue();
  // 检查URC和解析
  checkSerial1URC();
  
  // 定期检查堆内存（监控碎片化）
  unsigned long now = millis();
  if ((now - lastHeapCheck) > HEAP_CHECK_INTERVAL_MS) {
    lastHeapCheck = now;
    Serial.printf("[健康检查] 运行: %lu 天, 空闲堆: %u 字节, 最大连续块: %u 字节, 队列: %d 条\n",
                  getUptimeDays(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), sms_q_count);
    // 如果碎片化严重（最大块 < 空闲的 30%），提前重启
    if (ESP.getMaxAllocHeap() < ESP.getFreeHeap() / 3 && sms_q_count == 0) {
      Serial.println("⚠️ 堆碎片化严重，提前重启...");
      Serial.flush();
      delay(100);
      ESP.restart();
    }
  }
}

// 队列操作函数
void enqueueSMS(const char* sender, const char* text, const char* timestamp) {
  int insertIdx = (sms_q_head + sms_q_count) % SMS_QUEUE_SIZE;
  if (sms_q_count == SMS_QUEUE_SIZE) {
    // 队列已满，丢弃最老一条以腾出空间
    Serial.println("短信队列已满，丢弃最老一条条目");
    smsQueue[sms_q_head].valid = false;
    sms_q_head = (sms_q_head + 1) % SMS_QUEUE_SIZE;
    sms_q_count--;
    insertIdx = (sms_q_head + sms_q_count) % SMS_QUEUE_SIZE;
  }
  // 使用 strncpy 安全拷贝，确保 null 结尾
  strncpy(smsQueue[insertIdx].sender, sender, SMS_SENDER_LEN - 1);
  smsQueue[insertIdx].sender[SMS_SENDER_LEN - 1] = '\0';
  strncpy(smsQueue[insertIdx].text, text, SMS_TEXT_LEN - 1);
  smsQueue[insertIdx].text[SMS_TEXT_LEN - 1] = '\0';
  strncpy(smsQueue[insertIdx].timestamp, timestamp, SMS_TIMESTAMP_LEN - 1);
  smsQueue[insertIdx].timestamp[SMS_TIMESTAMP_LEN - 1] = '\0';
  smsQueue[insertIdx].retries = 0;
  smsQueue[insertIdx].lastAttempt = 0;
  smsQueue[insertIdx].valid = true;
  sms_q_count++;
  Serial.printf("已入队，队列长度=%d\n", sms_q_count);
}

void removeHeadSMS() {
  if (sms_q_count == 0) return;
  smsQueue[sms_q_head].valid = false;
  sms_q_head = (sms_q_head + 1) % SMS_QUEUE_SIZE;
  sms_q_count--;
}

// 尝试发送单条短信到所有开启的渠道，返回是否全部成功
bool trySendChannels(const SMSItem &item) {
  bool allOk = true;
#if ENABLE_WECOM_BOT
  bool okWeCom = sendSMSToWeComBot(item.sender, item.text, item.timestamp);
  if (!okWeCom) allOk = false;
#endif
#if ENABLE_HTTP_SERVER
  bool okHttp = sendSMSToServer(item.sender, item.text, item.timestamp);
  if (!okHttp) allOk = false;
#endif
#if ENABLE_EMAIL
  bool okEmail = sendSMSToEmail(item.sender, item.text, item.timestamp);
  if (!okEmail) allOk = false;
#endif
  return allOk;
}

// 处理队列：在 WiFi 已连接时重试队列中的短信
void processSMSQueue() {
  if (sms_q_count == 0) return;
  if (WiFi.status() != WL_CONNECTED) return; // 未连接时不处理（要等重连）

  int checked = 0;
  // 逐个检查队列条目（注意：可能在循环中移除head）
  while (checked < sms_q_count) {
    int idx = (sms_q_head + checked) % SMS_QUEUE_SIZE;
    SMSItem &it = smsQueue[idx];
    unsigned long now = millis();
    if (it.lastAttempt == 0 || (now - it.lastAttempt) >= SMS_RETRY_INTERVAL_MS) {
      Serial.printf("尝试重发队列第%d项，已重试%d次\n", checked + 1, it.retries);
      bool ok = trySendChannels(it);
      it.lastAttempt = now;
      if (ok) {
        // 成功发送，移除该项（如果是head则直接移除，否则需要移动元素）
        if (idx == sms_q_head) {
          removeHeadSMS();
        } else {
          // 将后面的元素前移一位
          int cur = idx;
          while (cur != (sms_q_head + sms_q_count - 1) % SMS_QUEUE_SIZE) {
            int next = (cur + 1) % SMS_QUEUE_SIZE;
            smsQueue[cur] = smsQueue[next];
            cur = next;
          }
          // 删除尾部
          sms_q_count--;
        }
        // 不增加 checked，因为队列缩短了，继续检查同位置
        continue;
      } else {
        it.retries++;
        if (it.retries >= SMS_MAX_RETRIES) {
          Serial.println("队列项达到最大重试次数，丢弃");
          if (idx == sms_q_head) removeHeadSMS();
          else {
            int cur = idx;
            while (cur != (sms_q_head + sms_q_count - 1) % SMS_QUEUE_SIZE) {
              int next = (cur + 1) % SMS_QUEUE_SIZE;
              smsQueue[cur] = smsQueue[next];
              cur = next;
            }
            sms_q_count--;
          }
          continue; // 继续，不增加 checked
        }
      }
    }
    checked++;
  }
}

// WiFi 自动重连（指数回退，处理 millis 溢出）
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  // 使用无符号减法自动处理溢出
  if ((now - lastWifiAttempt) < wifiReconnectInterval) return;
  lastWifiAttempt = now;
  Serial.println("尝试重连 WiFi...");
  if (WiFiMulti.run() == WL_CONNECTED) {
    Serial.println("WiFi 已重连");
    // reset interval
    wifiReconnectInterval = 5000;
  } else {
    // 增长间隔，最大 60s
    wifiReconnectInterval = min(wifiReconnectInterval * 2, 60000UL);
    Serial.printf("WiFi 重连失败，下次间隔 %lu ms\n", wifiReconnectInterval);
  }
}