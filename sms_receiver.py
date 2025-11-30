#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SMS Forwarder 服务端
接收 ESP32 推送的短信数据，存储到本地 JSON 文件
支持 API Key 验证、Web 登录、短信管理功能
"""

from flask import Flask, request, jsonify, render_template_string, Response, session, redirect, url_for
from datetime import datetime, timezone, timedelta
from functools import wraps
import json
import os
import logging
import hashlib
import secrets

# ==================== 配置区 ====================
SMS_LOG_FILE = "sms_log.json"       # 短信存储文件
MAX_LOG_ENTRIES = 1000              # 最多保留条数，防止文件过大
API_KEY = "your-api-key-here"       # API密钥（ESP32 推送时使用）
WEB_USER = "admin"                  # Web 登录用户名
WEB_PASS = "change-me"              # Web 登录密码
HOST = "0.0.0.0"                    # 监听地址
PORT = 32000                        # 监听端口
SECRET_KEY = secrets.token_hex(32)  # Session 密钥
# ================================================

# 中国时区 (UTC+8)
CHINA_TZ = timezone(timedelta(hours=8))

app = Flask(__name__)
app.secret_key = SECRET_KEY

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)


def get_china_time():
    """获取中国时间"""
    return datetime.now(CHINA_TZ).strftime('%Y-%m-%d %H:%M:%S')


def verify_api_key():
    """验证 API Key（用于 ESP32 推送）"""
    if not API_KEY:
        return True
    key = request.headers.get('X-API-Key') or request.args.get('api_key')
    return key == API_KEY


def login_required(f):
    """Web 页面登录验证装饰器"""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('logged_in'):
            return redirect(url_for('login'))
        return f(*args, **kwargs)
    return decorated_function


def load_logs():
    """加载短信记录"""
    if os.path.exists(SMS_LOG_FILE):
        try:
            with open(SMS_LOG_FILE, 'r', encoding='utf-8') as f:
                return json.load(f)
        except (json.JSONDecodeError, IOError):
            return []
    return []


def save_logs(logs):
    """保存短信记录"""
    if len(logs) > MAX_LOG_ENTRIES:
        logs = logs[-MAX_LOG_ENTRIES:]
    with open(SMS_LOG_FILE, 'w', encoding='utf-8') as f:
        json.dump(logs, f, ensure_ascii=False, indent=2)


# ==================== 登录相关 ====================

@app.route('/login', methods=['GET', 'POST'])
def login():
    """登录页面"""
    error = None
    if request.method == 'POST':
        username = request.form.get('username', '')
        password = request.form.get('password', '')
        if username == WEB_USER and password == WEB_PASS:
            session['logged_in'] = True
            session['username'] = username
            logger.info(f"用户 {username} 登录成功，IP: {request.remote_addr}")
            return redirect(url_for('web_index'))
        else:
            error = "用户名或密码错误"
            logger.warning(f"登录失败，IP: {request.remote_addr}")
    
    return render_template_string(LOGIN_TEMPLATE, error=error)


@app.route('/logout')
def logout():
    """退出登录"""
    session.clear()
    return redirect(url_for('login'))


# ==================== API 接口 ====================

@app.route('/sms', methods=['POST'])
def receive_sms():
    """接收短信推送（ESP32 调用）"""
    if not verify_api_key():
        logger.warning(f"API Key 验证失败，来源 IP: {request.remote_addr}")
        return jsonify({"status": "error", "message": "Unauthorized"}), 401
    
    try:
        data = request.get_json()
        if not data:
            return jsonify({"status": "error", "message": "Invalid JSON"}), 400
        
        sender = data.get('sender', 'unknown')
        message = data.get('message', '')
        timestamp = data.get('timestamp', '')
        
        record = {
            "id": int(datetime.now().timestamp() * 1000),
            "sender": sender,
            "message": message,
            "pdu_timestamp": timestamp,
            "received_at": get_china_time(),  # 使用中国时间
            "client_ip": request.remote_addr
        }
        
        logs = load_logs()
        logs.append(record)
        save_logs(logs)
        
        logger.info(f"收到短信 | 发送者: {sender} | 内容: {message[:50]}{'...' if len(message) > 50 else ''}")
        
        return jsonify({"status": "ok", "id": record["id"]}), 200
        
    except Exception as e:
        logger.error(f"处理短信失败: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route('/sms', methods=['GET'])
def list_sms():
    """查询短信记录（API）"""
    if not verify_api_key():
        return jsonify({"status": "error", "message": "Unauthorized"}), 401
    
    logs = load_logs()
    sender_filter = request.args.get('sender')
    if sender_filter:
        logs = [l for l in logs if sender_filter in l.get('sender', '')]
    
    try:
        limit = int(request.args.get('limit', 50))
        offset = int(request.args.get('offset', 0))
    except ValueError:
        limit, offset = 50, 0
    
    logs = logs[::-1]
    total = len(logs)
    logs = logs[offset:offset + limit]
    
    return jsonify({
        "status": "ok",
        "total": total,
        "limit": limit,
        "offset": offset,
        "data": logs
    })


@app.route('/api/sms/delete', methods=['POST'])
@login_required
def delete_sms_batch():
    """批量删除短信"""
    try:
        data = request.get_json()
        ids = data.get('ids', [])
        if not ids:
            return jsonify({"status": "error", "message": "No IDs provided"}), 400
        
        logs = load_logs()
        original_len = len(logs)
        logs = [l for l in logs if l.get('id') not in ids]
        deleted = original_len - len(logs)
        
        save_logs(logs)
        logger.info(f"批量删除 {deleted} 条短信")
        return jsonify({"status": "ok", "deleted": deleted})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route('/api/sms/export', methods=['POST'])
@login_required
def export_sms():
    """导出选中的短信为 JSON"""
    try:
        data = request.get_json()
        ids = data.get('ids', [])
        
        logs = load_logs()
        if ids:
            export_data = [l for l in logs if l.get('id') in ids]
        else:
            export_data = logs
        
        response = Response(
            json.dumps(export_data, ensure_ascii=False, indent=2),
            mimetype='application/json',
            headers={'Content-Disposition': f'attachment;filename=sms_export_{get_china_time().replace(":", "-").replace(" ", "_")}.json'}
        )
        return response
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route('/api/sms/clear', methods=['POST'])
@login_required
def clear_sms():
    """清空所有短信"""
    save_logs([])
    logger.info("已清空所有短信记录")
    return jsonify({"status": "ok"})


@app.route('/health', methods=['GET'])
def health_check():
    """健康检查"""
    logs = load_logs()
    return jsonify({
        "status": "healthy",
        "sms_count": len(logs),
        "server_time": get_china_time()
    })


# ==================== HTML 模板 ====================

LOGIN_TEMPLATE = '''
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>登录 - SMS Receiver</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
               background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
               min-height: 100vh; display: flex; align-items: center; justify-content: center; margin: 0; }
        .login-box { background: #fff; padding: 40px; border-radius: 12px; box-shadow: 0 10px 40px rgba(0,0,0,0.2);
                     width: 100%; max-width: 360px; }
        h1 { margin: 0 0 30px; color: #333; text-align: center; font-size: 24px; }
        h1 span { font-size: 32px; }
        input { width: 100%; padding: 12px; margin-bottom: 16px; border: 1px solid #ddd;
                border-radius: 6px; box-sizing: border-box; font-size: 14px; }
        input:focus { outline: none; border-color: #667eea; }
        button { width: 100%; padding: 12px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                 color: #fff; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; font-weight: 600; }
        button:hover { opacity: 0.9; }
        .error { background: #fee; color: #c00; padding: 10px; border-radius: 6px; margin-bottom: 16px; text-align: center; }
    </style>
</head>
<body>
    <div class="login-box">
        <h1><span>📨</span><br>SMS Receiver</h1>
        {% if error %}<div class="error">{{ error }}</div>{% endif %}
        <form method="post">
            <input type="text" name="username" placeholder="用户名" required autofocus>
            <input type="password" name="password" placeholder="密码" required>
            <button type="submit">登 录</button>
        </form>
    </div>
</body>
</html>
'''

WEB_TEMPLATE = '''
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>SMS Receiver</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
               max-width: 1000px; margin: 0 auto; padding: 20px; background: #f5f5f5; }
        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }
        .header h1 { margin: 0; color: #1a73e8; }
        .user-info { color: #666; }
        .user-info a { color: #1a73e8; text-decoration: none; margin-left: 15px; }
        .card { background: #fff; padding: 20px; border-radius: 8px;
                box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }
        .stats { display: flex; gap: 20px; margin-bottom: 20px; flex-wrap: wrap; }
        .stat-item { background: #e3f2fd; padding: 15px 25px; border-radius: 8px; text-align: center; }
        .stat-value { font-size: 28px; font-weight: bold; color: #1a73e8; }
        .stat-label { color: #666; font-size: 12px; margin-top: 5px; }
        .toolbar { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; align-items: center; }
        .btn { padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; }
        .btn-primary { background: #1a73e8; color: #fff; }
        .btn-danger { background: #dc3545; color: #fff; }
        .btn-success { background: #28a745; color: #fff; }
        .btn-secondary { background: #6c757d; color: #fff; }
        .btn:hover { opacity: 0.85; }
        .btn:disabled { opacity: 0.5; cursor: not-allowed; }
        table { width: 100%; border-collapse: collapse; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #eee; }
        th { background: #f8f9fa; font-weight: 600; position: sticky; top: 0; }
        .sender { color: #1a73e8; font-weight: 600; white-space: nowrap; }
        .time { color: #666; font-size: 12px; white-space: nowrap; }
        .message { max-width: 400px; word-break: break-all; }
        .empty { text-align: center; color: #999; padding: 40px; }
        .checkbox { width: 18px; height: 18px; cursor: pointer; }
        .table-wrap { max-height: 600px; overflow-y: auto; }
        .search-box { padding: 8px 12px; border: 1px solid #ddd; border-radius: 4px; width: 200px; }
        .selected-count { color: #666; font-size: 14px; }
        .toast { position: fixed; top: 20px; right: 20px; padding: 12px 20px; background: #333; color: #fff;
                 border-radius: 6px; display: none; z-index: 1000; }
        .toast.show { display: block; animation: fadeIn 0.3s; }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(-10px); } to { opacity: 1; transform: translateY(0); } }

        @media (max-width: 768px) {
            body { padding: 10px; }
            .header { flex-direction: column; align-items: flex-start; gap: 10px; }
            .stats { gap: 10px; }
            .stat-item { flex: 1; padding: 10px; }
            
            /* Table to Card view */
            table, thead, tbody, th, td, tr { display: block; }
            thead tr { position: absolute; top: -9999px; left: -9999px; }
            tr { margin-bottom: 15px; border: 1px solid #e0e0e0; border-radius: 8px; padding: 12px; background: #fff; box-shadow: 0 1px 2px rgba(0,0,0,0.05); position: relative; }
            td { border: none; padding: 2px 0; }
            
            /* Checkbox - Top Right */
            td:nth-child(1) { position: absolute; top: 12px; right: 12px; width: auto; height: auto; padding: 0; }
            
            /* Sender */
            td:nth-child(2) { padding-right: 30px; margin-bottom: 5px; }
            .sender { white-space: normal; font-size: 16px; }
            
            /* Message */
            td:nth-child(3) { margin-bottom: 8px; }
            .message { max-width: none; font-size: 15px; line-height: 1.5; color: #333; }
            
            /* Time */
            td:nth-child(4) { margin-bottom: 10px; }
            .time { white-space: normal; color: #888; font-size: 12px; }
            
            /* Actions */
            td:nth-child(5) { border-top: 1px solid #f0f0f0; padding-top: 10px; text-align: right; }
            td:nth-child(5) button { width: auto; padding: 6px 12px; }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>📨 SMS Receiver</h1>
        <div class="user-info">
            欢迎, {{ username }}
            <a href="/logout">退出登录</a>
        </div>
    </div>

    <div class="card">
        <div class="stats">
            <div class="stat-item">
                <div class="stat-value" id="totalCount">{{ total }}</div>
                <div class="stat-label">总短信数</div>
            </div>
            <div class="stat-item">
                <div class="stat-value">{{ server_time }}</div>
                <div class="stat-label">服务器时间 (北京)</div>
            </div>
        </div>
    </div>

    <div class="card">
        <div class="toolbar">
            <input type="text" class="search-box" id="searchInput" placeholder="搜索发送者或内容...">
            <button class="btn btn-primary" onclick="location.reload()">🔄 刷新</button>
            <button class="btn btn-success" onclick="exportSelected()">📥 导出选中</button>
            <button class="btn btn-danger" onclick="deleteSelected()">🗑️ 删除选中</button>
            <button class="btn btn-secondary" onclick="selectAll()">☑️ 全选</button>
            <button class="btn btn-secondary" onclick="deselectAll()">⬜ 取消全选</button>
            <span class="selected-count">已选: <span id="selectedCount">0</span> 条</span>
        </div>
        
        <div class="table-wrap">
            {% if sms_list %}
            <table id="smsTable">
                <thead>
                    <tr>
                        <th style="width:40px"><input type="checkbox" class="checkbox" id="checkAll" onchange="toggleAll(this)"></th>
                        <th>发送者</th>
                        <th>内容</th>
                        <th>接收时间</th>
                        <th>操作</th>
                    </tr>
                </thead>
                <tbody>
                {% for sms in sms_list %}
                    <tr data-id="{{ sms.id }}" data-sender="{{ sms.sender }}" data-message="{{ sms.message }}">
                        <td><input type="checkbox" class="checkbox sms-check" value="{{ sms.id }}" onchange="updateCount()"></td>
                        <td class="sender">{{ sms.sender }}</td>
                        <td class="message">{{ sms.message }}</td>
                        <td class="time">{{ sms.received_at }}</td>
                        <td><button class="btn btn-danger" style="padding:4px 8px;font-size:12px" onclick="deleteOne({{ sms.id }})">删除</button></td>
                    </tr>
                {% endfor %}
                </tbody>
            </table>
            {% else %}
            <div class="empty">暂无短信记录</div>
            {% endif %}
        </div>
    </div>

    <div class="toast" id="toast"></div>

    <script>
        // 显示提示
        function showToast(msg, duration=2000) {
            const t = document.getElementById('toast');
            t.textContent = msg;
            t.classList.add('show');
            setTimeout(() => t.classList.remove('show'), duration);
        }

        // 更新选中计数
        function updateCount() {
            const checked = document.querySelectorAll('.sms-check:checked').length;
            document.getElementById('selectedCount').textContent = checked;
        }

        // 全选/取消
        function toggleAll(el) {
            document.querySelectorAll('.sms-check').forEach(cb => {
                if (cb.closest('tr').style.display !== 'none') cb.checked = el.checked;
            });
            updateCount();
        }
        function selectAll() {
            document.querySelectorAll('.sms-check').forEach(cb => {
                if (cb.closest('tr').style.display !== 'none') cb.checked = true;
            });
            document.getElementById('checkAll').checked = true;
            updateCount();
        }
        function deselectAll() {
            document.querySelectorAll('.sms-check').forEach(cb => cb.checked = false);
            document.getElementById('checkAll').checked = false;
            updateCount();
        }

        // 获取选中的 ID
        function getSelectedIds() {
            return Array.from(document.querySelectorAll('.sms-check:checked')).map(cb => parseInt(cb.value));
        }

        // 删除选中
        async function deleteSelected() {
            const ids = getSelectedIds();
            if (ids.length === 0) { showToast('请先选择短信'); return; }
            if (!confirm(`确定删除 ${ids.length} 条短信？`)) return;
            
            const res = await fetch('/api/sms/delete', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ids})
            });
            const data = await res.json();
            if (data.status === 'ok') {
                showToast(`已删除 ${data.deleted} 条`);
                setTimeout(() => location.reload(), 1000);
            } else {
                showToast('删除失败: ' + data.message);
            }
        }

        // 删除单条
        async function deleteOne(id) {
            if (!confirm('确定删除这条短信？')) return;
            const res = await fetch('/api/sms/delete', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ids: [id]})
            });
            const data = await res.json();
            if (data.status === 'ok') {
                showToast('已删除');
                setTimeout(() => location.reload(), 500);
            }
        }

        // 导出选中
        async function exportSelected() {
            const ids = getSelectedIds();
            if (ids.length === 0) { showToast('请先选择短信'); return; }
            
            const res = await fetch('/api/sms/export', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ids})
            });
            const blob = await res.blob();
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `sms_export_${new Date().toISOString().slice(0,10)}.json`;
            a.click();
            URL.revokeObjectURL(url);
            showToast(`已导出 ${ids.length} 条`);
        }

        // 搜索过滤
        document.getElementById('searchInput').addEventListener('input', function() {
            const keyword = this.value.toLowerCase();
            document.querySelectorAll('#smsTable tbody tr').forEach(tr => {
                const sender = tr.dataset.sender.toLowerCase();
                const message = tr.dataset.message.toLowerCase();
                tr.style.display = (sender.includes(keyword) || message.includes(keyword)) ? '' : 'none';
            });
        });
    </script>
</body>
</html>
'''


@app.route('/', methods=['GET'])
@login_required
def web_index():
    """Web 管理界面（需要登录）"""
    logs = load_logs()[::-1][:100]
    return render_template_string(
        WEB_TEMPLATE,
        sms_list=logs,
        total=len(load_logs()),
        server_time=get_china_time(),
        username=session.get('username', 'Guest')
    )


if __name__ == '__main__':
    logger.info(f"SMS Receiver 启动中... 监听 {HOST}:{PORT}")
    logger.info(f"API Key: {API_KEY[:8]}***" if API_KEY else "API Key: 未设置")
    logger.info(f"Web 登录: {WEB_USER}")
    app.run(host=HOST, port=PORT, debug=False)