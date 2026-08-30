// pages/settings/settings.js
const app = getApp()
const api = require('../../utils/api.js')

Page({
  data: {
    settings: {
      notifications: true,
      privacyMode: false,
      autoConnect: true,
      language: 'zh'
    },
    serverUrl: '',
    deviceId: '',
    version: '1.0.0',
    loggedIn: false,
    userId: ''
  },

  onLoad() {
    this.loadSettings()
    this.loadConfig()
    this.setData({
      loggedIn: !!wx.getStorageSync('accessToken'),
      userId: wx.getStorageSync('userId') || ''
    })
  },

  async doLogin() {
    try {
      await api.login()
      this.setData({
        loggedIn: true,
        userId: wx.getStorageSync('userId') || ''
      })
      wx.showToast({ title: '登录成功', icon: 'success' })
    } catch (e) {
      wx.showToast({ title: '登录失败: ' + e.message, icon: 'none' })
    }
  },

  loadSettings() {
    const settings = wx.getStorageSync('settings') || {
      notifications: true,
      privacyMode: false,
      autoConnect: true,
      language: 'zh'
    }
    this.setData({ settings: settings })
  },

  loadConfig() {
    this.setData({
      serverUrl: app.globalData.serverUrl,
      deviceId: app.globalData.deviceId || '未设置'
    })
  },

  onSettingChange(e) {
    const field = e.currentTarget.dataset.field
    const value = e.detail.value
    const settings = this.data.settings
    settings[field] = value
    this.setData({ settings: settings })
    wx.setStorageSync('settings', settings)
    
    wx.showToast({
      title: '设置已保存',
      icon: 'success'
    })
  },

  onServerUrlChange(e) {
    const value = (e.detail.value || '').trim().replace(/\/+$/, '')
    if (!/^https:\/\/[^/]+/i.test(value)) {
      this.setData({ serverUrl: app.globalData.serverUrl })
      wx.showToast({ title: '仅允许 HTTPS 地址', icon: 'none' })
      return
    }
    this.setData({ serverUrl: value })
    app.globalData.serverUrl = value
    wx.setStorageSync('serverUrl', value)
  },

  clearData() {
    wx.showModal({
      title: '确认清除',
      content: '确定要清除所有本地数据吗？此操作不可恢复。',
      success: (res) => {
        if (res.confirm) {
          wx.clearStorageSync()
          this.loadSettings()
          wx.showToast({
            title: '数据已清除',
            icon: 'success'
          })
        }
      }
    })
  },

  exportData() {
    const data = {
      devices: wx.getStorageSync('devices') || [],
      calendar: wx.getStorageSync('calendar') || [],
      settings: wx.getStorageSync('settings') || {}
    }
    
    wx.setClipboardData({
      data: JSON.stringify(data, null, 2),
      success: () => {
        wx.showToast({
          title: '数据已复制到剪贴板',
          icon: 'success'
        })
      }
    })
  },

  showAbout() {
    wx.showModal({
      title: '关于 HomeMind',
      content: `版本：${app.globalData.version || '1.0.0'}\n\nHomeMind 是一个基于 OpenVela RTOS 的家庭智能助手，提供端云协同的智能家居控制能力。`,
      showCancel: false
    })
  },

  showHelp() {
    wx.showModal({
      title: '使用帮助',
      content: '1. 设备页：查看服务端返回的真实设备状态\n2. 日程页：日程当前仅保存在本机，提醒未接入\n3. 仪表盘：环境传感器和告警服务尚未接入\n4. 设置：仅使用 HTTPS 服务器地址',
      showCancel: false
    })
  }
})
