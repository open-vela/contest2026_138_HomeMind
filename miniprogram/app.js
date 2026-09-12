// app.js
App({
  onLaunch() {
    // 正式域名（微信后台登记）；用户可在设置页修改并存 Storage
    const configuredServerUrl = (wx.getStorageSync('serverUrl') || '').trim().replace(/\/+$/, '')
    const savedServerUrl = /^https:\/\/[^/]+/i.test(configuredServerUrl)
      ? configuredServerUrl
      : 'https://hfy-ai.cloud'
    this.globalData.serverUrl = savedServerUrl
    wx.setStorageSync('serverUrl', savedServerUrl)
    // 客户端尚未实现刷新流程，不持久化无用的长期凭据。
    wx.removeStorageSync('refreshToken')
    this.initStorage()
  },

  globalData: {
    userInfo: null,
    deviceId: null,
    // C1 正式域名（仅 HTTPS/WSS，绝不裸 IP/localhost/自签名）
    serverUrl: 'https://hfy-ai.cloud'
  },

  initStorage() {
    if (!wx.getStorageSync('devices')) {
      wx.setStorageSync('devices', [])
    }
    if (!wx.getStorageSync('calendar')) {
      wx.setStorageSync('calendar', [])
    }
    if (!wx.getStorageSync('settings')) {
      wx.setStorageSync('settings', {
        notifications: true,
        privacyMode: false,
        autoConnect: true
      })
    }
  }
})
