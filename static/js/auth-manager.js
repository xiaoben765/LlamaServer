/**
 * 用户认证管理器
 * 处理用户登录、注册、退出等认证相关功能
 */
class AuthManager {
    constructor() {
        this.currentUser = null;
        this.sessionToken = null;
        this.initElements();
        this.bindEvents();
        this.checkLoginStatus();
    }

    initElements() {
        // 导航栏元素
        this.loginBtn = document.getElementById('login-btn');
        this.userStatus = document.getElementById('user-status');
        this.userNameDisplay = document.getElementById('current-user-name');
        this.userMenuBtn = document.getElementById('user-menu-btn');

        // 认证弹窗元素
        this.authModal = document.getElementById('auth-modal');
        this.authModalTitle = document.getElementById('auth-modal-title');
        this.authModalClose = this.authModal.querySelector('.user-modal-close');

        // 表单元素
        this.loginForm = document.getElementById('login-form');
        this.registerForm = document.getElementById('register-form');

        // 登录表单
        this.loginUsername = document.getElementById('login-username');
        this.loginPassword = document.getElementById('login-password');
        this.loginSubmitBtn = document.getElementById('login-submit-btn');
        this.switchToRegisterBtn = document.getElementById('switch-to-register');

        // 注册表单
        this.registerUsername = document.getElementById('register-username');
        this.registerPassword = document.getElementById('register-password');
        this.registerConfirmPassword = document.getElementById('register-confirm-password');
        this.registerSubmitBtn = document.getElementById('register-submit-btn');
        this.switchToLoginBtn = document.getElementById('switch-to-login');

        // 用户菜单
        this.userMenuDropdown = document.getElementById('user-menu-dropdown');
        this.userProfileBtn = document.getElementById('user-profile');
        this.userLogoutBtn = document.getElementById('user-logout');
    }

    bindEvents() {
        // 登录按钮
        this.loginBtn?.addEventListener('click', () => {
            this.showAuthModal('login');
        });

        // 用户菜单按钮
        this.userMenuBtn?.addEventListener('click', (e) => {
            e.stopPropagation();
            this.toggleUserMenu();
        });

        // 关闭弹窗
        this.authModalClose?.addEventListener('click', () => {
            this.hideAuthModal();
        });

        this.authModal?.querySelector('.user-modal-overlay')?.addEventListener('click', () => {
            this.hideAuthModal();
        });

        // 表单切换
        this.switchToRegisterBtn?.addEventListener('click', () => {
            this.switchToRegister();
        });

        this.switchToLoginBtn?.addEventListener('click', () => {
            this.switchToLogin();
        });

        // 表单提交
        this.loginSubmitBtn?.addEventListener('click', () => {
            this.handleLogin();
        });

        this.registerSubmitBtn?.addEventListener('click', () => {
            this.handleRegister();
        });

        // 用户菜单项
        this.userLogoutBtn?.addEventListener('click', () => {
            this.handleLogout();
        });

        this.userProfileBtn?.addEventListener('click', () => {
            this.showUserProfile();
        });

        // 回车提交
        this.loginPassword?.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.handleLogin();
            }
        });

        this.registerConfirmPassword?.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') {
                this.handleRegister();
            }
        });

        // 点击其他地方关闭用户菜单
        document.addEventListener('click', (e) => {
            if (!this.userMenuBtn?.contains(e.target) && !this.userMenuDropdown?.contains(e.target)) {
                this.hideUserMenu();
            }
        });
    }

    showAuthModal(mode = 'login') {
        if (!this.authModal) return;

        if (mode === 'login') {
            this.switchToLogin();
        } else {
            this.switchToRegister();
        }

        this.authModal.style.display = 'flex';
        setTimeout(() => {
            this.authModal.style.opacity = '1';
        }, 10);

        // 聚焦到用户名输入框
        setTimeout(() => {
            if (mode === 'login') {
                this.loginUsername?.focus();
            } else {
                this.registerUsername?.focus();
            }
        }, 100);
    }

    hideAuthModal() {
        if (!this.authModal) return;

        this.authModal.style.opacity = '0';
        setTimeout(() => {
            this.authModal.style.display = 'none';
            this.clearForms();
        }, 200);
    }

    switchToLogin() {
        if (!this.loginForm || !this.registerForm) return;

        this.authModalTitle.textContent = '🔑 用户登录';
        this.loginForm.style.display = 'flex';
        this.registerForm.style.display = 'none';
        this.clearForms();
    }

    switchToRegister() {
        if (!this.loginForm || !this.registerForm) return;

        this.authModalTitle.textContent = '✨ 用户注册';
        this.loginForm.style.display = 'none';
        this.registerForm.style.display = 'flex';
        this.clearForms();
    }

    clearForms() {
        // 清空登录表单
        if (this.loginUsername) this.loginUsername.value = '';
        if (this.loginPassword) this.loginPassword.value = '';

        // 清空注册表单
        if (this.registerUsername) this.registerUsername.value = '';
        if (this.registerPassword) this.registerPassword.value = '';
        if (this.registerConfirmPassword) this.registerConfirmPassword.value = '';

        // 重置按钮状态
        this.setButtonLoading(this.loginSubmitBtn, false);
        this.setButtonLoading(this.registerSubmitBtn, false);
    }

    async handleLogin() {
        const username = this.loginUsername?.value.trim();
        const password = this.loginPassword?.value;

        if (!this.validateLoginForm(username, password)) {
            return;
        }

        this.setButtonLoading(this.loginSubmitBtn, true);

        try {
            const response = await fetch('/api/auth/login', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username,
                    password: password
                })
            });

            const data = await response.json();

            if (response.ok && data.success) {
                // 生成一个简单的token用于前端会话管理
                const simpleToken = 'user_' + data.user.username + '_' + Date.now();
                this.onLoginSuccess(data.user, simpleToken);
                this.hideAuthModal();
                this.showMessage('登录成功！', 'success');
            } else {
                this.showMessage(data.message || data.error || '登录失败，请检查用户名和密码', 'error');
            }
        } catch (error) {
            console.error('Login error:', error);
            this.showMessage('登录失败，请检查网络连接', 'error');
        } finally {
            this.setButtonLoading(this.loginSubmitBtn, false);
        }
    }

    async handleRegister() {
        const username = this.registerUsername?.value.trim();
        const password = this.registerPassword?.value;
        const confirmPassword = this.registerConfirmPassword?.value;

        if (!this.validateRegisterForm(username, password, confirmPassword)) {
            return;
        }

        this.setButtonLoading(this.registerSubmitBtn, true);

        try {
            const response = await fetch('/api/auth/register', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username,
                    password: password,
                    email: username + '@example.com' // 临时email，可以后续添加email输入框
                })
            });

            const data = await response.json();

            if (response.ok && data.success) {
                this.showMessage('注册成功！请登录使用', 'success');
                // 注册成功后切换到登录表单
                setTimeout(() => {
                    this.switchToLogin();
                    // 自动填入用户名
                    if (this.loginUsername) {
                        this.loginUsername.value = username;
                    }
                    // 聚焦到密码输入框
                    if (this.loginPassword) {
                        this.loginPassword.focus();
                    }
                }, 1000);
            } else {
                this.showMessage(data.message || data.error || '注册失败，请重试', 'error');
            }
        } catch (error) {
            console.error('Register error:', error);
            this.showMessage('注册失败，请检查网络连接', 'error');
        } finally {
            this.setButtonLoading(this.registerSubmitBtn, false);
        }
    }

    async handleLogout() {
        this.hideUserMenu();

        try {
            await fetch('/api/auth/logout', {
                method: 'POST',
                headers: {
                    'Authorization': `Bearer ${this.sessionToken}`
                }
            });
        } catch (error) {
            console.error('Logout error:', error);
        }

        this.onLogoutSuccess();
        this.showMessage('已退出登录', 'info');
    }

    validateLoginForm(username, password) {
        if (!username) {
            this.showMessage('请输入用户名', 'error');
            this.loginUsername?.focus();
            return false;
        }

        if (!password) {
            this.showMessage('请输入密码', 'error');
            this.loginPassword?.focus();
            return false;
        }

        return true;
    }

    validateRegisterForm(username, password, confirmPassword) {
        if (!username) {
            this.showMessage('请输入用户名', 'error');
            this.registerUsername?.focus();
            return false;
        }

        if (username.length < 3 || username.length > 20) {
            this.showMessage('用户名长度必须在3-20个字符之间', 'error');
            this.registerUsername?.focus();
            return false;
        }

        if (!/^[a-zA-Z0-9_\u4e00-\u9fa5]+$/.test(username)) {
            this.showMessage('用户名只能包含字母、数字、下划线和中文', 'error');
            this.registerUsername?.focus();
            return false;
        }

        if (!password) {
            this.showMessage('请输入密码', 'error');
            this.registerPassword?.focus();
            return false;
        }

        if (password.length < 6) {
            this.showMessage('密码长度至少6位', 'error');
            this.registerPassword?.focus();
            return false;
        }

        if (password !== confirmPassword) {
            this.showMessage('两次输入的密码不一致', 'error');
            this.registerConfirmPassword?.focus();
            return false;
        }

        return true;
    }

    onLoginSuccess(user, token) {
        this.currentUser = user;
        this.sessionToken = token;
        
        // 保存到本地存储
        localStorage.setItem('authToken', token);
        localStorage.setItem('currentUser', JSON.stringify(user));

        this.updateUI();
    }

    onLogoutSuccess() {
        this.currentUser = null;
        this.sessionToken = null;

        // 清除本地存储
        localStorage.removeItem('authToken');
        localStorage.removeItem('currentUser');

        this.updateUI();
    }

    checkLoginStatus() {
        // 从本地存储检查登录状态
        const token = localStorage.getItem('authToken');
        const userStr = localStorage.getItem('currentUser');

        if (token && userStr) {
            try {
                const user = JSON.parse(userStr);
                this.currentUser = user;
                this.sessionToken = token;
                this.updateUI();
                
                // 验证token是否仍然有效（可选）
                this.validateToken();
            } catch (error) {
                console.error('Parse user data error:', error);
                this.onLogoutSuccess();
            }
        }
    }

    async validateToken() {
        if (!this.sessionToken) return;

        try {
            const response = await fetch('/api/auth/validate', {
                method: 'GET',
                headers: {
                    'Authorization': `Bearer ${this.sessionToken}`
                }
            });

            if (!response.ok) {
                this.onLogoutSuccess();
            }
        } catch (error) {
            console.error('Token validation error:', error);
            // 网络错误时不强制退出
        }
    }

    updateUI() {
        if (this.currentUser) {
            // 显示用户状态
            if (this.userStatus) this.userStatus.style.display = 'flex';
            if (this.loginBtn) this.loginBtn.style.display = 'none';
            if (this.userNameDisplay) this.userNameDisplay.textContent = this.currentUser.username;
        } else {
            // 显示登录按钮
            if (this.userStatus) this.userStatus.style.display = 'none';
            if (this.loginBtn) this.loginBtn.style.display = 'flex';
            this.hideUserMenu();
        }
    }

    toggleUserMenu() {
        if (!this.userMenuDropdown) return;

        const isVisible = this.userMenuDropdown.style.display === 'block';
        if (isVisible) {
            this.hideUserMenu();
        } else {
            this.showUserMenu();
        }
    }

    showUserMenu() {
        if (!this.userMenuDropdown) return;
        this.userMenuDropdown.style.display = 'block';
    }

    hideUserMenu() {
        if (!this.userMenuDropdown) return;
        this.userMenuDropdown.style.display = 'none';
    }

    showUserProfile() {
        this.hideUserMenu();
        // TODO: 实现用户信息显示功能
        this.showMessage('用户信息功能正在开发中', 'info');
    }

    setButtonLoading(button, loading) {
        if (!button) return;

        if (loading) {
            button.disabled = true;
            button.textContent = '处理中...';
        } else {
            button.disabled = false;
            if (button === this.loginSubmitBtn) {
                button.textContent = '登录';
            } else if (button === this.registerSubmitBtn) {
                button.textContent = '注册';
            }
        }
    }

    showMessage(message, type = 'info') {
        // 创建消息提示元素
        const messageEl = document.createElement('div');
        messageEl.className = `auth-message auth-message-${type}`;
        messageEl.textContent = message;

        // 样式
        messageEl.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 12px 20px;
            border-radius: 8px;
            color: white;
            font-weight: 500;
            z-index: 9999;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
            transition: all 0.3s ease;
            transform: translateX(400px);
        `;

        // 根据类型设置背景色
        switch (type) {
            case 'success':
                messageEl.style.background = 'linear-gradient(135deg, #10b981, #059669)';
                break;
            case 'error':
                messageEl.style.background = 'linear-gradient(135deg, #ef4444, #dc2626)';
                break;
            case 'warning':
                messageEl.style.background = 'linear-gradient(135deg, #f59e0b, #d97706)';
                break;
            default:
                messageEl.style.background = 'linear-gradient(135deg, #3b82f6, #2563eb)';
        }

        document.body.appendChild(messageEl);

        // 动画显示
        setTimeout(() => {
            messageEl.style.transform = 'translateX(0)';
        }, 100);

        // 自动消失
        setTimeout(() => {
            messageEl.style.transform = 'translateX(400px)';
            setTimeout(() => {
                if (document.body.contains(messageEl)) {
                    document.body.removeChild(messageEl);
                }
            }, 300);
        }, 3000);
    }

    // 获取当前用户信息
    getCurrentUser() {
        return this.currentUser;
    }

    // 获取认证token
    getAuthToken() {
        return this.sessionToken;
    }

    // 检查是否已登录
    isLoggedIn() {
        return !!this.currentUser && !!this.sessionToken;
    }
}

// 导出认证管理器
window.AuthManager = AuthManager;
