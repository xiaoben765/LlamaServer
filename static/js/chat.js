/**
 * Kama Chat UI 主脚本
 * 基于 Open WebUI 设计理念，简化版
 */

document.addEventListener('DOMContentLoaded', function() {
    // DOM 元素
    const chatContainer = document.getElementById('chat-container');
    const messageForm = document.getElementById('message-form');
    const messageInput = document.getElementById('message-input');
    const sendButton = document.getElementById('send-button');
    const toggleSidebarBtn = document.getElementById('toggle-sidebar');
    const sidebar = document.getElementById('sidebar');
    const clearChatBtn = document.getElementById('clear-chat');
    const statusIndicator = document.getElementById('status-indicator');
    const statusText = document.getElementById('status-text');
    
    // 状态变量
    let isProcessing = false;
    let connectionStatus = 'unknown'; // unknown, online, offline
    
    // 检查LLaMA服务连接状态
    function checkConnectionStatus() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                if (data.llama_service === 'running') {
                    connectionStatus = 'online';
                    statusIndicator.className = 'status-indicator status-online';
                    statusText.innerText = '已连接';
                } else {
                    connectionStatus = 'offline';
                    statusIndicator.className = 'status-indicator status-offline';
                    statusText.innerText = '未连接';
                }
            })
            .catch(() => {
                connectionStatus = 'offline';
                statusIndicator.className = 'status-indicator status-offline';
                statusText.innerText = '连接错误';
            });
    }
    
    // 初始化时检查连接状态
    checkConnectionStatus();
    // 每30秒检查一次连接状态
    setInterval(checkConnectionStatus, 30000);
    
    // 添加消息到聊天容器
    function addMessage(content, isUser) {
        const messageDiv = document.createElement('div');
        messageDiv.className = `message ${isUser ? 'user-message' : 'bot-message'}`;
        
        const avatarDiv = document.createElement('div');
        avatarDiv.className = `message-avatar ${isUser ? 'user-avatar' : 'bot-avatar'}`;
        avatarDiv.innerText = isUser ? '用' : 'AI';
        
        const contentDiv = document.createElement('div');
        contentDiv.className = 'message-content';
        contentDiv.innerHTML = formatMessage(content);
        
        if (!isUser) {
            messageDiv.appendChild(avatarDiv);
            messageDiv.appendChild(contentDiv);
        } else {
            messageDiv.appendChild(contentDiv);
            messageDiv.appendChild(avatarDiv);
        }
        
        chatContainer.appendChild(messageDiv);
        
        // 滚动到底部
        chatContainer.scrollTop = chatContainer.scrollHeight;
    }
    
    // 格式化消息（支持简单的markdown）
    function formatMessage(text) {
        // 将换行符转换为<br>
        let formatted = text.replace(/\n/g, '<br>');
        
        // 支持代码块
        formatted = formatted.replace(/```([^`]+)```/g, '<pre><code>$1</code></pre>');
        
        // 支持行内代码
        formatted = formatted.replace(/`([^`]+)`/g, '<code>$1</code>');
        
        // 支持粗体
        formatted = formatted.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
        
        // 支持斜体
        formatted = formatted.replace(/\*([^*]+)\*/g, '<em>$1</em>');
        
        return formatted;
    }
    
    // 显示机器人正在输入的指示器
    function showTypingIndicator() {
        const typingDiv = document.createElement('div');
        typingDiv.id = 'typing-indicator';
        typingDiv.className = 'message bot-message';
        
        const avatarDiv = document.createElement('div');
        avatarDiv.className = 'message-avatar bot-avatar';
        avatarDiv.innerText = 'AI';
        
        const contentDiv = document.createElement('div');
        contentDiv.className = 'message-content typing-indicator';
        
        for (let i = 0; i < 3; i++) {
            const dot = document.createElement('span');
            dot.className = 'typing-dot';
            contentDiv.appendChild(dot);
        }
        
        typingDiv.appendChild(avatarDiv);
        typingDiv.appendChild(contentDiv);
        
        chatContainer.appendChild(typingDiv);
        chatContainer.scrollTop = chatContainer.scrollHeight;
        
        return typingDiv;
    }
    
    // 移除输入指示器
    function removeTypingIndicator() {
        const indicator = document.getElementById('typing-indicator');
        if (indicator) {
            indicator.remove();
        }
    }
    
    // 发送消息到服务器
    async function sendMessageToServer(message) {
        try {
            const response = await fetch('/api/chat', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ message })
            });
            
            if (!response.ok) {
                throw new Error(`服务器错误: ${response.status}`);
            }
            
            const data = await response.json();
            return data.response;
        } catch (error) {
            console.error('发送消息时出错:', error);
            throw error;
        }
    }
    
    // 处理消息发送
    async function handleSendMessage(e) {
        e.preventDefault();
        
        const message = messageInput.value.trim();
        if (!message || isProcessing) return;
        
        // 显示用户消息
        addMessage(message, true);
        
        // 重置输入框
        messageInput.value = '';
        
        // 显示处理状态
        isProcessing = true;
        sendButton.disabled = true;
        
        const typingIndicator = showTypingIndicator();
        
        try {
            // 发送到服务器
            const response = await sendMessageToServer(message);
            
            // 移除输入指示器
            removeTypingIndicator();
            
            // 显示回复
            addMessage(response, false);
        } catch (error) {
            removeTypingIndicator();
            addMessage(`错误: ${error.message}`, false);
        } finally {
            isProcessing = false;
            sendButton.disabled = false;
            messageInput.focus();
        }
    }
    
    // 切换侧边栏
    function toggleSidebar() {
        sidebar.classList.toggle('collapsed');
    }
    
    // 清除聊天记录
    function clearChat() {
        if (confirm('确定要清除所有聊天记录吗？')) {
            while (chatContainer.firstChild) {
                chatContainer.removeChild(chatContainer.firstChild);
            }
            
            // 可以添加一条欢迎消息
            addMessage('你好！我是Kama AI助手，有什么我可以帮你的吗？', false);
        }
    }
    
    // 事件监听
    messageForm.addEventListener('submit', handleSendMessage);
    
    if (toggleSidebarBtn) {
        toggleSidebarBtn.addEventListener('click', toggleSidebar);
    }
    
    if (clearChatBtn) {
        clearChatBtn.addEventListener('click', clearChat);
    }
    
    // 输入框自适应高度
    messageInput.addEventListener('input', function() {
        // 重置高度
        this.style.height = 'auto';
        
        // 计算新的高度
        const newHeight = Math.min(this.scrollHeight, 200);
        this.style.height = newHeight + 'px';
    });
    
    // 快捷键发送
    messageInput.addEventListener('keydown', function(e) {
        // Ctrl+Enter 发送消息
        if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
            e.preventDefault();
            messageForm.dispatchEvent(new Event('submit'));
        }
    });
    
    // 添加初始欢迎消息
    addMessage('你好！我是Kama AI助手，有什么我可以帮你的吗？', false);
});
