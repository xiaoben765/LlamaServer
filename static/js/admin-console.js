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

        // 数据库管理按钮
        this.bindButton('database-stats-btn', () => this.showDatabaseStats());
        this.bindButton('backup-db-btn', () => this.confirmAction('备份数据库', '确定要备份数据库吗？', () => this.backupDatabase()));
        this.bindButton('optimize-db-btn', () => this.confirmAction('优化数据库', '确定要优化数据库吗？', () => this.optimizeDatabase()));

        // 用户管理按钮
        this.bindButton('clear-users-btn', () => this.confirmAction('清空用户', '确定要清空所有用户数据吗？此操作不可恢复！', () => this.clearUsers()));
        this.bindButton('refresh-users-btn', () => this.updateUserStatus());
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
            // 这里可以添加用户状态API调用
            console.log('更新用户状态');
        } catch (error) {
            console.error('更新用户状态失败:', error);
        }
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
}

// 初始化管理控制台
document.addEventListener('DOMContentLoaded', () => {
    // 等待一小段时间确保其他脚本加载完成
    setTimeout(() => {
        new AdminConsole();
    }, 100);
});
