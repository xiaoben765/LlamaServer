// Open WebUI 风格的应用逻辑
class KamaAI {
    constructor() {
        this.currentChatId = null;
        this.chatHistory = [];
        this.models = [];
        this.selectedModel = '';
        this.isConnected = false;
        this.isLoading = false;
        this.socket = null;
        this.isDarkMode = this.getStoredTheme();
        
        this.init();
    }
    
    init() {
        this.setupEventListeners();
        this.loadChatHistory();
        this.loadModels();
        this.initTheme();
        this.checkServerConnection(); // 使用HTTP检查替代WebSocket
        this.setupResizeHandler();
        this.setupKeyboardShortcuts();
    }
    
    // 主题管理
    getStoredTheme() {
        const stored = localStorage.getItem('theme');
        if (stored) return stored === 'dark';
        return window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
    
    initTheme() {
        this.applyTheme();
        
        // 监听系统主题变化
        window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
            if (!localStorage.getItem('theme')) {
                this.isDarkMode = e.matches;
                this.applyTheme();
            }
        });
    }
    
    applyTheme() {
        document.body.classList.toggle('dark', this.isDarkMode);
        localStorage.setItem('theme', this.isDarkMode ? 'dark' : 'light');
    }
    
    toggleTheme() {
        this.isDarkMode = !this.isDarkMode;
        this.applyTheme();
    }
    
    // 事件监听器设置
    setupEventListeners() {
        // 侧边栏切换
        document.getElementById('sidebar-toggle').addEventListener('click', () => {
            this.toggleSidebar();
        });
        
        document.getElementById('sidebar-toggle-btn').addEventListener('click', () => {
            this.toggleSidebar();
        });
        
        document.getElementById('mobile-sidebar-toggle').addEventListener('click', () => {
            this.toggleSidebar();
        });
        
        // 新建对话
        document.getElementById('new-chat-btn').addEventListener('click', () => {
            this.createNewChat();
        });
        
        // 主题切换
        document.getElementById('theme-toggle').addEventListener('click', () => {
            this.toggleTheme();
        });
        
        // 设置按钮
        document.getElementById('settings-btn').addEventListener('click', () => {
            this.openSettings();
        });
        
        // 消息输入
        const messageInput = document.getElementById('message-input');
        messageInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                this.sendMessage();
            }
        });
        
        messageInput.addEventListener('input', () => {
            this.autoResizeInput();
        });
        
        // 发送按钮
        document.getElementById('send-btn').addEventListener('click', () => {
            this.sendMessage();
        });
        
        // 附件按钮
        document.getElementById('attach-btn').addEventListener('click', () => {
            this.handleAttachment();
        });
        
        // 模型选择
        document.getElementById('model-selector').addEventListener('change', (e) => {
            this.selectedModel = e.target.value;
            localStorage.setItem('selectedModel', this.selectedModel);
        });
        
        // 搜索
        document.getElementById('search-input').addEventListener('input', (e) => {
            this.searchChats(e.target.value);
        });
        
        // 侧边栏遮罩点击
        document.getElementById('sidebar-overlay').addEventListener('click', () => {
            this.closeSidebar();
        });
    }
    
    // 键盘快捷键
    setupKeyboardShortcuts() {
        document.addEventListener('keydown', (e) => {
            // Ctrl/Cmd + K - 搜索
            if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
                e.preventDefault();
                document.getElementById('search-input').focus();
            }
            
            // Ctrl/Cmd + N - 新建对话
            if ((e.ctrlKey || e.metaKey) && e.key === 'n') {
                e.preventDefault();
                this.createNewChat();
            }
            
            // Ctrl/Cmd + / - 切换侧边栏
            if ((e.ctrlKey || e.metaKey) && e.key === '/') {
                e.preventDefault();
                this.toggleSidebar();
            }
            
            // Escape - 关闭移动端侧边栏
            if (e.key === 'Escape') {
                this.closeSidebar();
            }
        });
    }
    
    // 窗口大小调整处理
    setupResizeHandler() {
        window.addEventListener('resize', () => {
            if (window.innerWidth > 768) {
                this.closeSidebar();
            }
        });
    }
    
    // 侧边栏管理
    toggleSidebar() {
        const sidebar = document.getElementById('sidebar');
        const overlay = document.getElementById('sidebar-overlay');
        const toggleBtn = document.getElementById('sidebar-toggle-btn');
        
        if (window.innerWidth <= 768) {
            // 移动端
            sidebar.classList.toggle('open');
            overlay.style.display = sidebar.classList.contains('open') ? 'block' : 'none';
        } else {
            // 桌面端
            sidebar.classList.toggle('collapsed');
            // 控制切换按钮的显示
            if (sidebar.classList.contains('collapsed')) {
                toggleBtn.style.display = 'block';
            } else {
                toggleBtn.style.display = 'none';
            }
        }
    }
    
    closeSidebar() {
        const sidebar = document.getElementById('sidebar');
        const overlay = document.getElementById('sidebar-overlay');
        
        sidebar.classList.remove('open');
        overlay.style.display = 'none';
    }
    
    // 输入框自动调整高度
    autoResizeInput() {
        const input = document.getElementById('message-input');
        input.style.height = 'auto';
        input.style.height = Math.min(input.scrollHeight, 160) + 'px';
    }
    
    // 检查服务器连接状态
    async checkServerConnection() {
        try {
            const response = await fetch('/api/status');
            if (response.ok) {
                console.log('服务器连接正常');
                this.updateConnectionStatus('connected', '已连接');
                this.isConnected = true;
            } else {
                throw new Error('服务器响应异常');
            }
        } catch (error) {
            console.error('服务器连接检查失败:', error);
            this.updateConnectionStatus('disconnected', '连接失败');
            this.isConnected = false;
            
            // 重试连接
            setTimeout(() => {
                this.checkServerConnection();
            }, 5000);
        }
    }

    // WebSocket 连接 (暂时禁用，使用REST API)
    connectWebSocket() {
        // 暂时禁用WebSocket，直接标记为已连接
        console.log('使用REST API模式，跳过WebSocket连接');
        this.updateConnectionStatus('connected', '已连接 (REST)');
        this.isConnected = true;
        
        /* 原WebSocket代码保留备用
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        
        try {
            this.socket = new WebSocket(wsUrl);
            
            this.socket.onopen = () => {
                console.log('WebSocket 连接已建立');
                this.updateConnectionStatus('connected', '已连接');
                this.isConnected = true;
            };
            
            this.socket.onmessage = (event) => {
                this.handleWebSocketMessage(event);
            };
            
            this.socket.onclose = () => {
                console.log('WebSocket 连接已关闭');
                this.updateConnectionStatus('disconnected', '连接断开');
                this.isConnected = false;
                
                // 重连逻辑
                setTimeout(() => {
                    if (!this.isConnected) {
                        this.connectWebSocket();
                    }
                }, 3000);
            };
            
            this.socket.onerror = (error) => {
                console.error('WebSocket 错误:', error);
                this.updateConnectionStatus('disconnected', '连接错误');
            };
            
        } catch (error) {
            console.error('WebSocket 连接失败:', error);
            this.updateConnectionStatus('disconnected', '连接失败');
        }
        */
    }
    
    // 处理 WebSocket 消息
    handleWebSocketMessage(event) {
        try {
            const data = JSON.parse(event.data);
            
            switch (data.type) {
                case 'message':
                    this.addMessage('assistant', data.content, false);
                    break;
                case 'stream':
                    this.updateStreamingMessage(data.content);
                    break;
                case 'complete':
                    this.handleMessageComplete();
                    break;
                case 'error':
                    this.handleError(data.error);
                    break;
                default:
                    console.log('未知消息类型:', data.type);
            }
        } catch (error) {
            console.error('解析 WebSocket 消息失败:', error);
        }
    }
    
    // 更新连接状态
    updateConnectionStatus(status, text) {
        const indicator = document.getElementById('status-indicator');
        const statusText = document.getElementById('status-text');
        
        indicator.className = `status-indicator ${status}`;
        statusText.textContent = text;
    }
    
    // 加载模型列表
    async loadModels() {
        try {
            const response = await fetch('/api/models');
            const data = await response.json();
            
            if (data.success) {
                this.models = data.models;
                this.populateModelSelector();
            }
        } catch (error) {
            console.error('加载模型失败:', error);
        }
    }
    
    populateModelSelector() {
        const selector = document.getElementById('model-selector');
        selector.innerHTML = '<option value="">选择模型...</option>';
        
        this.models.forEach(model => {
            const option = document.createElement('option');
            option.value = model.id;
            option.textContent = model.name;
            selector.appendChild(option);
        });
        
        // 恢复上次选择的模型
        const savedModel = localStorage.getItem('selectedModel');
        if (savedModel && this.models.find(m => m.id === savedModel)) {
            selector.value = savedModel;
            this.selectedModel = savedModel;
        }
    }
    
    // 对话管理
    createNewChat() {
        this.currentChatId = this.generateChatId();
        this.clearMessages();
        this.showPlaceholder();
        this.addChatToHistory('新建对话', this.currentChatId);
        document.getElementById('message-input').focus();
    }
    
    generateChatId() {
        return 'chat_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
    }
    
    addChatToHistory(title, chatId) {
        const chat = {
            id: chatId,
            title: title,
            timestamp: Date.now(),
            messages: []
        };
        
        this.chatHistory.unshift(chat);
        this.saveChatHistory();
        this.renderChatHistory();
    }
    
    updateChatTitle(chatId, title) {
        const chat = this.chatHistory.find(c => c.id === chatId);
        if (chat) {
            chat.title = title;
            this.saveChatHistory();
            this.renderChatHistory();
        }
    }
    
    deleteChat(chatId) {
        this.chatHistory = this.chatHistory.filter(c => c.id !== chatId);
        this.saveChatHistory();
        this.renderChatHistory();
        
        if (this.currentChatId === chatId) {
            this.createNewChat();
        }
    }
    
    loadChat(chatId) {
        const chat = this.chatHistory.find(c => c.id === chatId);
        if (chat) {
            this.currentChatId = chatId;
            this.clearMessages();
            
            chat.messages.forEach(msg => {
                this.addMessage(msg.role, msg.content, false);
            });
            
            this.hidePlaceholder();
            this.updateChatSelection();
        }
    }
    
    // 消息管理
    sendMessage() {
        const input = document.getElementById('message-input');
        const message = input.value.trim();
        
        if (!message || this.isLoading) return;
        
        if (!this.selectedModel) {
            this.showError('请先选择一个模型');
            return;
        }
        
        if (!this.isConnected) {
            this.showError('连接断开，请稍后重试');
            return;
        }
        
        // 添加用户消息
        this.addMessage('user', message);
        
        // 清空输入框
        input.value = '';
        this.autoResizeInput();
        
        // 发送到服务器
        this.sendToServer(message);
        
        // 隐藏占位内容
        this.hidePlaceholder();
        
        // 添加到对话历史
        if (!this.currentChatId) {
            this.currentChatId = this.generateChatId();
            const title = message.length > 30 ? message.substring(0, 30) + '...' : message;
            this.addChatToHistory(title, this.currentChatId);
        }
    }
    
    sendSuggestion(suggestion) {
        const input = document.getElementById('message-input');
        input.value = suggestion;
        this.sendMessage();
    }
    
    async sendToServer(message) {
        if (!this.isConnected) {
            this.showError('连接断开，无法发送消息');
            return;
        }
        
        this.isLoading = true;
        this.showTypingIndicator();
        
        try {
            const response = await fetch('/api/llama/query', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    query: message,
                    model: this.selectedModel || 'llama-7b',
                    stream: false // 暂时使用非流式响应
                })
            });
            
            if (!response.ok) {
                throw new Error(`HTTP错误: ${response.status}`);
            }
            
            const data = await response.json();
            
            // 隐藏加载指示器
            this.hideTypingIndicator();
            this.isLoading = false;
            
            // 添加AI回复
            if (data.response) {
                this.addMessage('assistant', data.response, true);
                
                // 如果是缓存响应，显示提示
                if (data.cached) {
                    console.log('回复来自缓存');
                }
            } else {
                this.showError('服务器返回空响应');
            }
            
        } catch (error) {
            console.error('发送消息失败:', error);
            this.hideTypingIndicator();
            this.isLoading = false;
            this.showError('发送消息失败: ' + error.message);
        }
    }
    
    addMessage(role, content, save = true) {
        const messagesList = document.getElementById('messages-list');
        const messageDiv = this.createMessageElement(role, content);
        messagesList.appendChild(messageDiv);
        
        // 滚动到底部
        this.scrollToBottom();
        
        // 保存到对话历史
        if (save && this.currentChatId) {
            const chat = this.chatHistory.find(c => c.id === this.currentChatId);
            if (chat) {
                chat.messages.push({ role, content, timestamp: Date.now() });
                this.saveChatHistory();
            }
        }
    }
    
    createMessageElement(role, content) {
        const messageDiv = document.createElement('div');
        messageDiv.className = `message ${role}`;
        
        const avatar = document.createElement('div');
        avatar.className = 'message-avatar';
        avatar.textContent = role === 'user' ? 'U' : 'K';
        
        const contentDiv = document.createElement('div');
        contentDiv.className = 'message-content';
        
        const bubble = document.createElement('div');
        bubble.className = 'message-bubble';
        bubble.textContent = content;
        
        contentDiv.appendChild(bubble);
        messageDiv.appendChild(avatar);
        messageDiv.appendChild(contentDiv);
        
        return messageDiv;
    }
    
    showTypingIndicator() {
        const indicator = document.createElement('div');
        indicator.className = 'message assistant typing-indicator';
        indicator.id = 'typing-indicator';
        
        const avatar = document.createElement('div');
        avatar.className = 'message-avatar';
        avatar.textContent = 'K';
        
        const content = document.createElement('div');
        content.className = 'message-content';
        
        const bubble = document.createElement('div');
        bubble.className = 'message-bubble loading-indicator';
        bubble.innerHTML = `
            <span>正在思考</span>
            <div class="loading-dots">
                <div class="loading-dot"></div>
                <div class="loading-dot"></div>
                <div class="loading-dot"></div>
            </div>
        `;
        
        content.appendChild(bubble);
        indicator.appendChild(avatar);
        indicator.appendChild(content);
        
        document.getElementById('messages-list').appendChild(indicator);
        this.scrollToBottom();
    }
    
    hideTypingIndicator() {
        const indicator = document.getElementById('typing-indicator');
        if (indicator) {
            indicator.remove();
        }
    }
    
    updateStreamingMessage(content) {
        // 移除打字指示器
        this.hideTypingIndicator();
        
        // 查找或创建流式消息元素
        let streamElement = document.getElementById('streaming-message');
        if (!streamElement) {
            streamElement = this.createMessageElement('assistant', '');
            streamElement.id = 'streaming-message';
            document.getElementById('messages-list').appendChild(streamElement);
        }
        
        const bubble = streamElement.querySelector('.message-bubble');
        bubble.textContent += content;
        
        this.scrollToBottom();
    }
    
    handleMessageComplete() {
        this.isLoading = false;
        this.hideTypingIndicator();
        
        const streamElement = document.getElementById('streaming-message');
        if (streamElement) {
            streamElement.removeAttribute('id');
            
            // 保存完整消息
            const content = streamElement.querySelector('.message-bubble').textContent;
            if (this.currentChatId) {
                const chat = this.chatHistory.find(c => c.id === this.currentChatId);
                if (chat) {
                    chat.messages.push({ 
                        role: 'assistant', 
                        content: content, 
                        timestamp: Date.now() 
                    });
                    this.saveChatHistory();
                }
            }
        }
    }
    
    clearMessages() {
        document.getElementById('messages-list').innerHTML = '';
    }
    
    scrollToBottom() {
        const container = document.getElementById('messages-container');
        container.scrollTop = container.scrollHeight;
    }
    
    // 占位内容管理
    showPlaceholder() {
        document.getElementById('placeholder-content').style.display = 'flex';
    }
    
    hidePlaceholder() {
        document.getElementById('placeholder-content').style.display = 'none';
    }
    
    // 对话历史渲染
    renderChatHistory() {
        const container = document.getElementById('chat-history');
        container.innerHTML = '';
        
        if (this.chatHistory.length === 0) {
            return;
        }
        
        // 按时间分组
        const groups = this.groupChatsByTime();
        
        Object.entries(groups).forEach(([timeGroup, chats]) => {
            // 添加时间分组标题
            const groupTitle = document.createElement('div');
            groupTitle.className = 'time-group';
            groupTitle.textContent = timeGroup;
            container.appendChild(groupTitle);
            
            // 添加对话项
            chats.forEach(chat => {
                const chatItem = this.createChatItem(chat);
                container.appendChild(chatItem);
            });
        });
    }
    
    groupChatsByTime() {
        const groups = {};
        const now = new Date();
        
        this.chatHistory.forEach(chat => {
            const chatDate = new Date(chat.timestamp);
            const diffTime = now - chatDate;
            const diffDays = Math.floor(diffTime / (1000 * 60 * 60 * 24));
            
            let group;
            if (diffDays === 0) {
                group = '今天';
            } else if (diffDays === 1) {
                group = '昨天';
            } else if (diffDays <= 7) {
                group = '最近 7 天';
            } else if (diffDays <= 30) {
                group = '最近 30 天';
            } else {
                const month = chatDate.getMonth() + 1;
                group = `${chatDate.getFullYear()} 年 ${month} 月`;
            }
            
            if (!groups[group]) {
                groups[group] = [];
            }
            groups[group].push(chat);
        });
        
        return groups;
    }
    
    createChatItem(chat) {
        const item = document.createElement('div');
        item.className = 'chat-item';
        if (chat.id === this.currentChatId) {
            item.classList.add('active');
        }
        
        item.innerHTML = `
            <div class="chat-item-title" title="${chat.title}">${chat.title}</div>
            <div class="chat-item-actions">
                <button class="chat-item-action" onclick="app.editChatTitle('${chat.id}')" title="重命名">
                    <svg width="12" height="12" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16.862 4.487l1.687-1.688a1.875 1.875 0 112.652 2.652L6.832 19.82a4.5 4.5 0 01-1.897 1.13l-2.685.8.8-2.685a4.5 4.5 0 011.13-1.897L16.863 4.487zm0 0L19.5 7.125" />
                    </svg>
                </button>
                <button class="chat-item-action" onclick="app.deleteChat('${chat.id}')" title="删除">
                    <svg width="12" height="12" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                        <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14.74 9l-.346 9m-4.788 0L9.26 9m9.968-3.21c.342.052.682.107 1.022.166m-1.022-.165L18.16 19.673a2.25 2.25 0 01-2.244 2.077H8.084a2.25 2.25 0 01-2.244-2.077L4.772 5.79m14.456 0a48.108 48.108 0 00-3.478-.397m-12 .562c.34-.059.68-.114 1.022-.165m0 0a48.11 48.11 0 013.478-.397m7.5 0v-.916c0-1.18-.91-2.164-2.09-2.201a51.964 51.964 0 00-3.32 0c-1.18.037-2.09 1.022-2.09 2.201v.916m7.5 0a48.667 48.667 0 00-7.5 0" />
                    </svg>
                </button>
            </div>
        `;
        
        // 点击加载对话
        item.addEventListener('click', (e) => {
            if (!e.target.closest('.chat-item-actions')) {
                this.loadChat(chat.id);
            }
        });
        
        return item;
    }
    
    updateChatSelection() {
        document.querySelectorAll('.chat-item').forEach(item => {
            item.classList.remove('active');
        });
        
        const activeItem = document.querySelector(`[onclick*="${this.currentChatId}"]`)?.closest('.chat-item');
        if (activeItem) {
            activeItem.classList.add('active');
        }
    }
    
    editChatTitle(chatId) {
        const chat = this.chatHistory.find(c => c.id === chatId);
        if (!chat) return;
        
        const newTitle = prompt('请输入新的对话标题:', chat.title);
        if (newTitle && newTitle.trim() !== chat.title) {
            this.updateChatTitle(chatId, newTitle.trim());
        }
    }
    
    // 搜索功能
    searchChats(query) {
        const chatItems = document.querySelectorAll('.chat-item');
        
        if (!query.trim()) {
            chatItems.forEach(item => item.style.display = 'flex');
            return;
        }
        
        const searchTerm = query.toLowerCase();
        chatItems.forEach(item => {
            const title = item.querySelector('.chat-item-title').textContent.toLowerCase();
            item.style.display = title.includes(searchTerm) ? 'flex' : 'none';
        });
    }
    
    // 数据持久化
    saveChatHistory() {
        localStorage.setItem('chatHistory', JSON.stringify(this.chatHistory));
    }
    
    loadChatHistory() {
        const saved = localStorage.getItem('chatHistory');
        if (saved) {
            try {
                this.chatHistory = JSON.parse(saved);
                this.renderChatHistory();
            } catch (error) {
                console.error('加载对话历史失败:', error);
                this.chatHistory = [];
            }
        }
    }
    
    // 错误处理
    handleError(error) {
        this.isLoading = false;
        this.hideTypingIndicator();
        this.showError(error || '发生未知错误');
    }
    
    showError(message) {
        // 简单的错误提示，可以替换为更好的 UI
        const errorDiv = document.createElement('div');
        errorDiv.className = 'error-message';
        errorDiv.textContent = message;
        errorDiv.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: #ef4444;
            color: white;
            padding: 12px 16px;
            border-radius: 8px;
            z-index: 1000;
            animation: slideIn 0.3s ease;
        `;
        
        document.body.appendChild(errorDiv);
        
        setTimeout(() => {
            errorDiv.remove();
        }, 3000);
    }
    
    // 附件处理
    handleAttachment() {
        // 创建文件输入元素
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = 'image/*,text/*,.pdf,.doc,.docx';
        input.style.display = 'none';
        
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (file) {
                this.uploadFile(file);
            }
        };
        
        document.body.appendChild(input);
        input.click();
        document.body.removeChild(input);
    }
    
    async uploadFile(file) {
        const formData = new FormData();
        formData.append('file', file);
        
        try {
            const response = await fetch('/api/upload', {
                method: 'POST',
                body: formData
            });
            
            const result = await response.json();
            if (result.success) {
                this.addMessage('user', `[已上传文件: ${file.name}]`);
            } else {
                this.showError('文件上传失败');
            }
        } catch (error) {
            console.error('文件上传错误:', error);
            this.showError('文件上传失败');
        }
    }
    
    // 设置
    openSettings() {
        // 触发管理控制台弹窗显示
        const adminModal = document.getElementById('admin-modal');
        if (adminModal) {
            // 如果存在管理控制台，显示它
            const event = new CustomEvent('show-admin-console');
            document.dispatchEvent(event);
        } else {
            // 回退到原来的行为
            window.open('/admin.html', '_blank');
        }
    }
}

// 全局变量和函数
let app;

// 页面加载完成后初始化应用
document.addEventListener('DOMContentLoaded', () => {
    app = new KamaAI();
});

// 全局函数供 HTML 调用
function sendSuggestion(text) {
    if (app) {
        app.sendSuggestion(text);
    }
}

// 添加 CSS 动画
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }
    
    .error-message {
        animation: slideIn 0.3s ease;
    }
    
    .typing-indicator .loading-dots {
        display: inline-flex;
        margin-left: 8px;
    }
    
    .typing-indicator .loading-dot {
        width: 4px;
        height: 4px;
        border-radius: 50%;
        background-color: currentColor;
        margin: 0 1px;
        animation: typing 1.4s ease-in-out infinite both;
    }
    
    .typing-indicator .loading-dot:nth-child(1) { animation-delay: -0.32s; }
    .typing-indicator .loading-dot:nth-child(2) { animation-delay: -0.16s; }
    .typing-indicator .loading-dot:nth-child(3) { animation-delay: 0s; }
    
    @keyframes typing {
        0%, 80%, 100% { transform: scale(0); opacity: 0.5; }
        40% { transform: scale(1); opacity: 1; }
    }
    
    .sidebar-overlay {
        position: fixed;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background: rgba(0, 0, 0, 0.5);
        z-index: 99;
    }
`;
document.head.appendChild(style);
