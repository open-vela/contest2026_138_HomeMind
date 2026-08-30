// pages/index/index.js
const api = require('../../utils/api.js')

Page({
  data: {
    status: '未连接',
    temperature: '--',
    humidity: '--',
    lastUpdate: '--',
    onlineCount: '--',
    totalCount: '--',
    calendarItems: []
  },

  onLoad() {
    this.loadCalendar()
    this.checkConnection()
  },

  onShow() {
    this.loadCalendar()
    this.checkConnection()
  },

  loadCalendar() {
    const calendarItems = (wx.getStorageSync('calendar') || []).slice(0, 3)
    this.setData({ calendarItems })
  },

  async checkConnection() {
    this.setData({ status: '连接中…' })
    try {
      await api.login()
      const res = await api.getDevices()
      if (res.statusCode !== 200) throw new Error('HTTP ' + res.statusCode)
      const devices = res.data.devices || []
      const onlineCount = devices.filter((d) => d.online === true).length
      this.setData({
        status: onlineCount > 0 ? '在线' : (devices.length > 0 ? '离线' : '无已绑定设备'),
        onlineCount: onlineCount,
        totalCount: devices.length,
        lastUpdate: this.formatTime(new Date())
      })
    } catch (e) {
      this.setData({
        status: '服务不可用',
        onlineCount: '--',
        totalCount: '--'
      })
    }
  },

  formatTime(date) {
    const hour = date.getHours()
    const minute = date.getMinutes()
    return `${hour}:${minute < 10 ? '0' : ''}${minute}`
  },

  goToDashboard() {
    wx.switchTab({
      url: '/pages/dashboard/dashboard'
    })
  },

  goToDevices() {
    wx.switchTab({
      url: '/pages/devices/devices'
    })
  },

  goToCalendar() {
    wx.switchTab({
      url: '/pages/calendar/calendar'
    })
  },

  onPullDownRefresh() {
    this.checkConnection().then(() => wx.stopPullDownRefresh())
  }
})
