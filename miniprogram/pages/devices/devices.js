// pages/devices/devices.js —— C1：对接真实后端（登录/绑定/下发/WS 回显）
const app = getApp()
const api = require('../../utils/api.js')
const ws = require('../../utils/ws.js')

Page({
  data: {
    devices: [],
    loading: false,
    pending: {},            // device_id -> command_id（执行中防重入）
    showAddModal: false,
    newDevice: { device_id: '', name: '' }
  },

  onShow() {
    this.ensureAndLoad()
  },

  onHide() {
    this.closeWs()
  },

  async ensureAndLoad() {
    try {
      await api.login()
      await this.loadDevices()
      this.openWs()
    } catch (e) {
      wx.showToast({ title: '登录/加载失败: ' + e.message, icon: 'none' })
    }
  },

  async loadDevices() {
    this.setData({ loading: true })
    try {
      const res = await api.getDevices()
      if (res.statusCode === 200) {
        const list = (res.data.devices || []).map((d) => ({
          id: d.device_id,
          name: d.name || d.device_id,
          online: d.online,
          led_state: d.led_state || 'unknown',
          last_seen: d.last_seen
        }))
        this.setData({ devices: list })
      }
    } catch (e) {
      console.error(e)
    } finally {
      this.setData({ loading: false })
    }
  },

  openWs() {
    this.closeWs()
    this._ws = ws.connect((msg) => this.onWsMessage(msg))
  },

  closeWs() {
    if (this._ws) {
      try { this._ws.close() } catch (e) {}
      this._ws = null
    }
  },

  onWsMessage(msg) {
    if (msg.type === 'snapshot') {
      if (this.data.devices.length === 0 && msg.devices) this.applyDevices(msg.devices)
    } else if (msg.type === 'status') {
      this.updateDevice(msg.device_id, { online: msg.online, led_state: msg.led })
    } else if (msg.type === 'command') {
      const pending = this.data.pending
      if (pending[msg.device_id] === msg.command_id) {
        const np = Object.assign({}, pending)
        delete np[msg.device_id]
        this.setData({ pending: np })
      }
      if (msg.status === 'expired') {
        wx.showToast({ title: '命令超时未执行', icon: 'none' })
        this.updateDevice(msg.device_id, { led_state: 'unknown' })
      }
    }
  },

  applyDevices(list) {
    const devices = list.map((d) => ({
      id: d.device_id,
      name: d.name || d.device_id,
      online: d.online,
      led_state: d.led_state || 'unknown'
    }))
    this.setData({ devices })
  },

  updateDevice(deviceId, patch) {
    const devices = this.data.devices.map((d) =>
      d.id === deviceId ? Object.assign({}, d, patch) : d)
    this.setData({ devices })
  },

  toggleDevice(e) {
    const deviceId = e.currentTarget.dataset.id
    const dev = this.data.devices.find((d) => d.id === deviceId)
    if (!dev || this.data.pending[deviceId]) return
    const action = dev.led_state === 'on' ? 'led.off' : 'led.on'
    api.sendCommand(deviceId, action, {}).then((res) => {
      if (res.statusCode === 200) {
        const pending = Object.assign({}, this.data.pending)
        pending[deviceId] = res.data.command_id
        this.setData({ pending })
        this.updateDevice(deviceId, { led_state: 'pending' })
      } else {
        wx.showToast({ title: '下发失败', icon: 'none' })
      }
    }).catch((err) => {
      wx.showToast({ title: '下发失败: ' + err.message, icon: 'none' })
    })
  },

  addDevice() {
    this.setData({ showAddModal: true, newDevice: { device_id: '', name: '' } })
  },
  hideAddModal() {
    this.setData({ showAddModal: false })
  },
  onInputChange(e) {
    const field = e.currentTarget.dataset.field
    this.setData({ ['newDevice.' + field]: e.detail.value })
  },
  async confirmAdd() {
    const self = this
    const dev = this.data.newDevice
    if (!dev.device_id) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' })
      return
    }
    try {
      await api.login()
      const res = await api.request('POST', '/v1/devices', {
        device_id: dev.device_id, name: dev.name
      })
      if (res.statusCode === 200) {
        this.setData({ showAddModal: false })
        await this.loadDevices()
      } else {
        wx.showToast({ title: '绑定失败', icon: 'none' })
      }
    } catch (e) {
      wx.showToast({ title: '绑定失败: ' + e.message, icon: 'none' })
    }
  },

  deleteDevice(e) {
    const deviceId = e.currentTarget.dataset.id
    const self = this
    wx.showModal({
      title: '提示',
      content: '云端解绑接口本期未开放，仅从本机视图移除？',
      success: (r) => {
        if (r.confirm) {
          const devices = self.data.devices.filter((d) => d.id !== deviceId)
          self.setData({ devices })
        }
      }
    })
  },

  onPullDownRefresh() {
    this.loadDevices().then(() => wx.stopPullDownRefresh())
  }
})
