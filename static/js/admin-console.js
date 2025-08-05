// 管理控制台功能
class AdminConsole {
    constructor() {
        this.modal = null;
        this.currentTab = 'status';
        this.statusUpdateInterval = null;
        this.init();
    }

    init() {
        // 监听来自主应用的显示事件
        document.addEventListener('show-admin-console', () => {
            this.show();
        });
        
        // 绑定设置按钮点击事件，并阻止默认行为
        const settingsBtn = document.getElementById('settings-btn');
        if (settingsBtn) {
            // 移除所有现有的事件监听器
            settingsBtn.replaceWith(settingsBtn.cloneNode(true));
            // 重新获取元素引用
            const newSettingsBtn = document.getElementById('settings-btn');
            
            newSettingsBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.show();
            });
        }
    }

    show() {
        this.modal = document.getElementById('admin-modal');
        if (this.modal) {
            this.modal.style.display = 'flex';
            this.bindEvents();
            this.switchTab('status');
            this.startStatusUpdates();
        }
    }

    hide() {
        if (this.modal) {
            this.modal.style.display = 'none';
            this.stopStatusUpdates();
            this.unbindEvents();
        }
    }

    bindEvents() {
        // 关闭按钮
        const closeBtn = this.modal.querySelector('.admin-modal-close');
        if (closeBtn) {
            closeBtn.addEventListener('click', () => this.hide());
        }

        // 点击背景关闭
        const overlay = this.modal.querySelector('.admin-modal-overlay');
        if (overlay) {
            overlay.addEventListener('click', () => this.hide());
        }

        // 标签页切换
        const tabs = this.modal.querySelectorAll('.admin-tab');
        tabs.forEach(tab => {
            tab.addEventListener('click', () => {
                const tabId = tab.dataset.tab;
                this.switchTab(tabId);
            });
        });

        // 绑定按钮事件
        this.bindButtonEvents();

        // ESC键关闭
        document.addEventListener('keydown', this.handleKeydown.bind(this));
    }

    unbindEvents() {
        document.removeEventListener('keydown', this.handleKeydown.bind(this));
    }

    handleKeydown(e) {
        if (e.key === 'Escape') {
            this.hide();
        }
    }

    switchTab(tabId) {
        this.currentTab = tabId;

        // 更新标签页样式
        const tabs = this.modal.querySelectorAll('.admin-tab');
        tabs.forEach(tab => {
            if (tab.dataset.tab === tabId) {
                tab.classList.add('active');
            } else {
                tab.classList.remove('active');
            }
        });

        // 显示对应面板
        const panels = this.modal.querySelectorAll('.admin-panel');
        panels.forEach(panel => {
            if (panel.id === `${tabId}-panel`) {
                panel.classList.add('active');
            } else {
                panel.classList.remove('active');
            }
        });

        // 执行特定标签页的初始化
        switch (tabId) {
            case 'status':
                this.updateSystemStatus();
                break;
            case 'cache':
                this.updateCacheStatus();
                break;
            case 'database':
                this.updateDatabaseStatus();
                break;
            case 'users':
                this.updateUserStatus();
                break;
        }
    }

    bindButtonEvents() {
        // 系统状态按钮
        this.bindButton('refresh-status-btn', () => this.updateSystemStatus());

        // 缓存管理按钮
        this.bindButton('clear-cache-btn', () => this.confirmAction('清空缓存', '确定要清空所有缓存吗？', () => this.clearCache()));
        this.bindButton('refresh-cache-btn', () => this.updateCacheStatus());
        this.bindButton('view-cache-btn', () => this.viewCacheContent());

        // 数据库管理按钮
        this.bindButton('database-stats-btn', () => this.showDatabaseStats());
        this.bindButton('backup-db-btn', () => this.confirmAction('备份数据库', '确定要备份数据库吗？', () => this.backupDatabase()));
        this.bindButton('optimize-db-btn', () => this.confirmAction('优化数据库', '确定要优化数据库吗？', () => this.optimizeDatabase()));

        // 用户管理按钮
        this.bindButton('clear-users-btn', () => this.confirmAction('清空用户', '确定要清空所有用户数据吗？此操作不可恢复！', () => this.clearUsers()));
        this.bindButton('refresh-users-btn', () => this.updateUserStatus());
        this.bindButton('delete-user-btn', () => this.showDeleteUserDialog());
    }

    bindButton(buttonId, callback) {
        const button = this.modal.querySelector(`#${buttonId}`);
        if (button) {
            button.addEventListener('click', callback);
        }
    }

    // 状态更新相关方法
    startStatusUpdates() {
        this.updateSystemStatus();
        this.statusUpdateInterval = setInterval(() => {
            if (this.currentTab === 'status') {
                this.updateSystemStatus();
            }
        }, 5000); // 每5秒更新一次
    }

    stopStatusUpdates() {
        if (this.statusUpdateInterval) {
            clearInterval(this.statusUpdateInterval);
            this.statusUpdateInterval = null;
        }
    }

    async updateSystemStatus() {
        try {
            const response = await fetch('/api/status');
            const data = await response.json();
            this.updateStatusDisplay(data);
        } catch (error) {
            console.error('更新系统状态失败:', error);
            this.showError('获取系统状态失败');
        }
    }

    updateStatusDisplay(data) {
        console.log('Status data received:', data); // 调试日志
        
        // 更新运行时间 (使用服务器启动时间戳)
        const uptimeEl = this.modal.querySelector('#status-uptime');
        if (uptimeEl && data.timestamp) {
            // timestamp 应该是服务器启动时间，我们计算运行时间
            const currentTime = Math.floor(Date.now() / 1000);
            const serverStartTime = data.timestamp;
            
            // 检查时间戳是否合理（不超过当前时间）
            if (serverStartTime <= currentTime) {
                const uptime = currentTime - serverStartTime;
                uptimeEl.textContent = this.formatUptime(uptime);
            } else {
                // 如果时间戳不合理，显示原始数据
                uptimeEl.textContent = `时间戳: ${data.timestamp}`;
            }
        }

        // 更新LLaMA服务状态
        const llamaStatusEl = this.modal.querySelector('#status-requests');
        if (llamaStatusEl) {
            llamaStatusEl.textContent = data.llama_available ? '✅ 可用' : '❌ 不可用';
        }

        // 更新数据库状态
        const dbStatusEl = this.modal.querySelector('#status-connections');
        if (dbStatusEl) {
            const dbStatus = data.db_available ? '已连接' : '断开';
            dbStatusEl.textContent = `${data.db_type.toUpperCase()} (${dbStatus})`;
        }

        // 更新状态指示器颜色
        const indicators = this.modal.querySelectorAll('.status-indicator');
        indicators.forEach((indicator, index) => {
            switch(index) {
                case 0: // 运行时间
                    indicator.className = 'status-indicator info';
                    break;
                case 1: // LLaMA服务
                    indicator.className = data.llama_available ? 'status-indicator online' : 'status-indicator offline';
                    break;
                case 2: // 数据库
                    indicator.className = data.db_available ? 'status-indicator online' : 'status-indicator offline';
                    break;
                case 3: // 最后更新
                    indicator.className = 'status-indicator info';
                    break;
            }
        });

        // 更新最后更新时间
        const lastUpdateEl = this.modal.querySelector('#status-last-update');
        if (lastUpdateEl) {
            lastUpdateEl.textContent = new Date().toLocaleTimeString('zh-CN');
        }
    }

    formatUptime(seconds) {
        const days = Math.floor(seconds / 86400);
        const hours = Math.floor((seconds % 86400) / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const secs = Math.floor(seconds % 60);

        if (days > 0) {
            return `${days}天 ${hours}小时 ${minutes}分钟`;
        } else if (hours > 0) {
            return `${hours}小时 ${minutes}分钟`;
        } else if (minutes > 0) {
            return `${minutes}分钟 ${secs}秒`;
        } else {
            return `${secs}秒`;
        }
    }

    async updateCacheStatus() {
        // 实现缓存状态更新逻辑
        try {
            // 这里可以添加缓存状态API调用
            console.log('更新缓存状态');
        } catch (error) {
            console.error('更新缓存状态失败:', error);
        }
    }

    async updateDatabaseStatus() {
        // 实现数据库状态更新逻辑
        try {
            // 这里可以添加数据库状态API调用
            console.log('更新数据库状态');
        } catch (error) {
            console.error('更新数据库状态失败:', error);
        }
    }

    async updateUserStatus() {
        // 实现用户状态更新逻辑
        try {
            const response = await fetch('/api/admin/users');
            const data = await response.json();
            
            if (response.ok) {
                this.displayUserStats(data);
            } else {
                this.showError('获取用户信息失败: ' + (data.error || '未知错误'));
            }
        } catch (error) {
            console.error('更新用户状态失败:', error);
            this.showError('获取用户信息时发生网络错误');
        }
    }

    displayUserStats(data) {
        const usersPanel = this.modal.querySelector('#users-panel');
        if (!usersPanel) return;

        let content = `
            <div class="status-card">
                <h3>👥 用户统计</h3>
                <p><strong>总用户数:</strong> ${data.total}</p>
            </div>
        `;

        if (data.users && data.users.length > 0) {
            content += `
                <div class="status-card">
                    <h3>📋 用户列表</h3>
                    <div class="users-table">
                        <table style="width: 100%; border-collapse: collapse;">
                            <thead>
                                <tr style="background: #f3f4f6;">
                                    <th style="padding: 8px; border: 1px solid #e5e7eb;">用户名</th>
                                    <th style="padding: 8px; border: 1px solid #e5e7eb;">邮箱</th>
                                    <th style="padding: 8px; border: 1px solid #e5e7eb;">注册时间</th>
                                    <th style="padding: 8px; border: 1px solid #e5e7eb;">最后登录</th>
                                    <th style="padding: 8px; border: 1px solid #e5e7eb; width: 80px;">操作</th>
                                </tr>
                            </thead>
                            <tbody>
            `;

            data.users.forEach(user => {
                content += `
                    <tr>
                        <td style="padding: 8px; border: 1px solid #e5e7eb;">${user.username}</td>
                        <td style="padding: 8px; border: 1px solid #e5e7eb;">${user.email}</td>
                        <td style="padding: 8px; border: 1px solid #e5e7eb;">${user.created_at || 'N/A'}</td>
                        <td style="padding: 8px; border: 1px solid #e5e7eb;">${user.last_login || '从未登录'}</td>
                        <td style="padding: 4px; border: 1px solid #e5e7eb; text-align: center;">
                            <button onclick="adminConsole.confirmDeleteUser('${user.username}')" 
                                    style="padding: 4px 8px; background: #ef4444; color: white; border: none; border-radius: 4px; font-size: 12px; cursor: pointer;"
                                    title="删除用户">🗑️</button>
                        </td>
                    </tr>
                `;
            });

            content += `
                            </tbody>
                        </table>
                    </div>
                </div>
            `;
        } else {
            content += `
                <div class="status-card">
                    <h3>📋 用户列表</h3>
                    <p>暂无注册用户</p>
                </div>
            `;
        }

        usersPanel.innerHTML = content;
    }

    // API操作方法
    async clearCache() {
        return this.apiCall('/api/admin/clear-cache', '清空缓存', 'cache-panel');
    }

    async showDatabaseStats() {
        try {
            const response = await fetch('/api/admin/database-stats');
            const data = await response.json();
            this.showInfo('数据库统计', JSON.stringify(data, null, 2));
        } catch (error) {
            this.showError('获取数据库统计失败');
        }
    }

    async backupDatabase() {
        return this.apiCall('/api/admin/backup-database', '备份数据库', 'database-panel');
    }

    async optimizeDatabase() {
        return this.apiCall('/api/admin/optimize-database', '优化数据库', 'database-panel');
    }

    async clearUsers() {
        return this.apiCall('/api/admin/clear-users', '清空用户', 'users-panel');
    }

    async viewCacheContent() {
        try {
            const response = await fetch('/api/admin/cache-content');
            const data = await response.json();
            
            if (response.ok) {
                this.displayCacheContent(data);
            } else {
                this.showError('获取缓存内容失败: ' + (data.error || '未知错误'));
            }
        } catch (error) {
            console.error('获取缓存内容失败:', error);
            this.showError('获取缓存内容时发生网络错误');
        }
    }

    displayCacheContent(data) {
        const cachePanel = this.modal.querySelector('#cache-panel');
        if (!cachePanel) return;

        let content = `
            <div class="status-card">
                <h3>💾 缓存统计</h3>
                <p><strong>缓存项数量:</strong> ${data.cache_count}</p>
            </div>
        `;

        if (data.cache_items && data.cache_items.length > 0) {
            content += `
                <div class="status-card">
                    <h3>📋 缓存内容</h3>
                    <div class="cache-table" style="max-height: 400px; overflow-y: auto;">
                        <table style="width: 100%; border-collapse: collapse; font-size: 12px;">
                            <thead>
                                <tr style="background: #f3f4f6; position: sticky; top: 0;">
                                    <th style="padding: 6px; border: 1px solid #e5e7eb; width: 25%;">查询</th>
                                    <th style="padding: 6px; border: 1px solid #e5e7eb; width: 45%;">响应</th>
                                    <th style="padding: 6px; border: 1px solid #e5e7eb; width: 15%;">时间戳</th>
                                    <th style="padding: 6px; border: 1px solid #e5e7eb; width: 15%;">访问次数</th>
                                </tr>
                            </thead>
                            <tbody>
            `;

            data.cache_items.forEach(item => {
                const query = item.query.length > 50 ? item.query.substring(0, 50) + '...' : item.query;
                const response = item.response.length > 100 ? item.response.substring(0, 100) + '...' : item.response;
                const timestamp = new Date(item.timestamp * 1000).toLocaleString();

                content += `
                    <tr>
                        <td style="padding: 6px; border: 1px solid #e5e7eb; word-break: break-word;" title="${item.query}">${query}</td>
                        <td style="padding: 6px; border: 1px solid #e5e7eb; word-break: break-word;" title="${item.response}">${response}</td>
                        <td style="padding: 6px; border: 1px solid #e5e7eb;">${timestamp}</td>
                        <td style="padding: 6px; border: 1px solid #e5e7eb; text-align: center;">${item.access_count}</td>
                    </tr>
                `;
            });

            content += `
                            </tbody>
                        </table>
                    </div>
                </div>
            `;
        } else {
            content += `
                <div class="status-card">
                    <h3>📋 缓存内容</h3>
                    <p>暂无缓存数据</p>
                </div>
            `;
        }

        cachePanel.innerHTML = content;
    }

    async apiCall(endpoint, actionName, panelId) {
        const button = event.target;
        this.setButtonLoading(button, true);

        try {
            const response = await fetch(endpoint, { method: 'POST' });
            const data = await response.json();
            
            if (response.ok) {
                this.showResult(panelId, JSON.stringify(data, null, 2), 'success');
                this.setButtonState(button, 'success');
            } else {
                this.showResult(panelId, `错误: ${data.error || '操作失败'}`, 'error');
                this.setButtonState(button, 'error');
            }
        } catch (error) {
            this.showResult(panelId, `网络错误: ${error.message}`, 'error');
            this.setButtonState(button, 'error');
        } finally {
            this.setButtonLoading(button, false);
            setTimeout(() => this.resetButtonState(button), 2000);
        }
    }

    setButtonLoading(button, loading) {
        if (loading) {
            button.classList.add('loading');
            button.disabled = true;
        } else {
            button.classList.remove('loading');
            button.disabled = false;
        }
    }

    setButtonState(button, state) {
        button.classList.remove('success', 'error');
        if (state) {
            button.classList.add(state);
        }
    }

    resetButtonState(button) {
        button.classList.remove('success', 'error', 'loading');
        button.disabled = false;
    }

    showResult(panelId, content, type = 'success') {
        const panel = this.modal.querySelector(`#${panelId}`);
        if (!panel) return;

        let resultEl = panel.querySelector('.admin-result');
        if (!resultEl) {
            resultEl = document.createElement('div');
            resultEl.className = 'admin-result';
            panel.appendChild(resultEl);
        }

        resultEl.textContent = content;
        resultEl.className = `admin-result show ${type}`;

        // 自动隐藏结果
        setTimeout(() => {
            resultEl.classList.remove('show');
        }, 5000);
    }

    showError(message) {
        this.showResult(this.currentTab + '-panel', message, 'error');
    }

    // 确认对话框
    confirmAction(title, message, callback) {
        const confirmModal = document.getElementById('admin-confirm-modal');
        if (!confirmModal) return;

        const titleEl = confirmModal.querySelector('#confirm-title');
        const messageEl = confirmModal.querySelector('#confirm-message');
        const confirmBtn = confirmModal.querySelector('#confirm-btn');
        const cancelBtn = confirmModal.querySelector('#cancel-btn');

        if (titleEl) titleEl.textContent = title;
        if (messageEl) messageEl.textContent = message;

        const handleConfirm = () => {
            callback();
            this.hideConfirm();
        };

        const handleCancel = () => {
            this.hideConfirm();
        };

        if (confirmBtn) {
            confirmBtn.onclick = handleConfirm;
        }
        if (cancelBtn) {
            cancelBtn.onclick = handleCancel;
        }

        // 点击背景关闭
        const overlay = confirmModal.querySelector('.admin-confirm-overlay');
        if (overlay) {
            overlay.onclick = handleCancel;
        }

        confirmModal.style.display = 'flex';
    }

    hideConfirm() {
        const confirmModal = document.getElementById('admin-confirm-modal');
        if (confirmModal) {
            confirmModal.style.display = 'none';
        }
    }

    // 信息对话框
    showInfo(title, content) {
        const infoModal = document.getElementById('admin-info-modal');
        if (!infoModal) return;

        const titleEl = infoModal.querySelector('#info-title');
        const contentEl = infoModal.querySelector('#info-text');
        const closeBtn = infoModal.querySelector('#info-close-btn');
        const closeFooterBtn = infoModal.querySelector('#info-close-btn-footer');

        if (titleEl) titleEl.textContent = title;
        if (contentEl) contentEl.textContent = content;

        const handleClose = () => {
            this.hideInfo();
        };

        if (closeBtn) {
            closeBtn.onclick = handleClose;
        }
        
        if (closeFooterBtn) {
            closeFooterBtn.onclick = handleClose;
        }

        // 点击背景关闭
        const overlay = infoModal.querySelector('.admin-info-overlay');
        if (overlay) {
            overlay.onclick = handleClose;
        }

        infoModal.style.display = 'flex';
    }

    hideInfo() {
        const infoModal = document.getElementById('admin-info-modal');
        if (infoModal) {
            infoModal.style.display = 'none';
        }
    }

    showDeleteUserDialog() {
        const usersPanel = this.modal.querySelector('#users-panel');
        if (!usersPanel) return;

        // 创建删除用户的输入对话框
        const dialogHTML = `
            <div class="delete-user-dialog" style="margin-top: 20px; padding: 20px; background: #f9fafb; border-radius: 8px; border: 1px solid #e5e7eb;">
                <h4 style="margin: 0 0 15px 0; color: #374151;">删除指定用户</h4>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; margin-bottom: 5px; font-weight: 500; color: #374151;">用户名:</label>
                    <input type="text" id="delete-username-input" placeholder="请输入要删除的用户名" 
                           style="width: 100%; padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 6px; font-size: 14px;">
                </div>
                <div style="display: flex; gap: 10px;">
                    <button id="confirm-delete-user" style="flex: 1; padding: 8px 16px; background: #ef4444; color: white; border: none; border-radius: 6px; font-weight: 500; cursor: pointer;">
                        删除用户
                    </button>
                    <button id="cancel-delete-user" style="flex: 1; padding: 8px 16px; background: #6b7280; color: white; border: none; border-radius: 6px; font-weight: 500; cursor: pointer;">
                        取消
                    </button>
                </div>
            </div>
        `;

        // 如果已经存在对话框，先移除
        const existingDialog = usersPanel.querySelector('.delete-user-dialog');
        if (existingDialog) {
            existingDialog.remove();
        }

        // 添加对话框
        usersPanel.insertAdjacentHTML('beforeend', dialogHTML);

        // 绑定事件
        const confirmBtn = usersPanel.querySelector('#confirm-delete-user');
        const cancelBtn = usersPanel.querySelector('#cancel-delete-user');
        const usernameInput = usersPanel.querySelector('#delete-username-input');

        if (confirmBtn) {
            confirmBtn.addEventListener('click', () => {
                const username = usernameInput?.value.trim();
                if (username) {
                    this.deleteUser(username);
                } else {
                    this.showError('请输入用户名');
                }
            });
        }

        if (cancelBtn) {
            cancelBtn.addEventListener('click', () => {
                const dialog = usersPanel.querySelector('.delete-user-dialog');
                if (dialog) {
                    dialog.remove();
                }
            });
        }

        // 聚焦到输入框
        if (usernameInput) {
            usernameInput.focus();
        }
    }

    confirmDeleteUser(username) {
        if (confirm(`确定要删除用户 "${username}" 吗？此操作不可恢复！`)) {
            this.deleteUser(username);
        }
    }

    async deleteUser(username) {
        try {
            const response = await fetch('/api/admin/delete-user', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username
                })
            });

            const data = await response.json();

            if (response.ok && data.success) {
                this.showInfo('删除成功', `用户 "${username}" 已成功删除`);
                
                // 移除对话框
                const dialog = this.modal.querySelector('.delete-user-dialog');
                if (dialog) {
                    dialog.remove();
                }
                
                // 刷新用户列表
                setTimeout(() => {
                    this.updateUserStatus();
                }, 1000);
            } else {
                this.showError(data.error || data.message || '删除用户失败');
            }
        } catch (error) {
            console.error('删除用户失败:', error);
            this.showError('删除用户时发生网络错误');
        }
    }
}

// 初始化管理控制台
document.addEventListener('DOMContentLoaded', () => {
    // 等待一小段时间确保其他脚本加载完成
    setTimeout(() => {
        window.adminConsole = new AdminConsole();
    }, 100);
});
