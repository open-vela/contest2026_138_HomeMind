// pages/dashboard/dashboard.js
const api = require('../../utils/api.js')

Page({
  data: {
    sensorData: {
      temperature: '--',
      humidity: '--',
      light: '--',
      noise: '--'
    },
    sensorStatus: '未接入（当前硬件未配置环境传感器）',
    deviceStatus: {
      online: 0,
      offline: 0,
      total: 0
    },
    deviceStatusText: '尚未加载',
    alerts: [],
    alertsStatus: '告警服务未接入'
  },

  onLoad() {
    this.loadDeviceStatus()
  },

  onShow() {
    this.refreshData()
  },

  async loadDeviceStatus() {
    this.setData({ deviceStatusText: '加载中…' })
    try {
      await api.login()
      const res = await api.getDevices()
      if (res.statusCode !== 200) throw new Error('HTTP ' + res.statusCode)
      const devices = res.data.devices || []
      const online = devices.filter((d) => d.online === true).length
      this.setData({
        deviceStatus: {
          online: online,
          offline: devices.length - online,
          total: devices.length
        },
        deviceStatusText: '来自服务端实时状态'
      })
    } catch (e) {
      this.setData({
        deviceStatus: { online: 0, offline: 0, total: 0 },
        deviceStatusText: '加载失败，暂无可验证数据'
      })
    }
  },

  refreshData() {
    this.loadDeviceStatus()
  },

  onPullDownRefresh() {
    this.refreshData()
    wx.stopPullDownRefresh()
  },

  goToDevices() {
    wx.switchTab({
      url: '/pages/devices/devices'
    })
  }
})
