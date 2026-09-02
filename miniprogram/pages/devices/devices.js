// pages/devices/devices.js —— C1：对接真实后端（登录/绑定/下发/WS 回显）
const app = getApp()
const api = require('../../utils/api.js')
const ws = require('../../utils/ws.js')

Page({
  data: {
    devices: [],
    loading: false,
    pending: {},            // device_id -> {command_id, action}（直到最终状态防重入）
    showAddModal: false,
    newDevice: { device_id: '', name: '' }
  },

  onShow() {
    this.ensureAndLoad()
  },

  onHide() {
    this.closeWs()
    this.stopCommandPollers()
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

  stopCommandPollers() {
    const pollers = this._commandPollers || {}
    Object.keys(pollers).forEach((deviceId) => clearTimeout(pollers[deviceId]))
    this._commandPollers = {}
  },

  finishCommand(deviceId, commandId, action, status) {
    const pending = this.data.pending
    const item = pending[deviceId]
    if (!item || item.command_id !== commandId) return
    const np = Object.assign({}, pending)
    delete np[deviceId]
    this.setData({ pending: np })
    if (status === 'done' && action === 'led.on') this.updateDevice(deviceId, { led_state: 'on' })
    if (status === 'done' && action === 'led.off') this.updateDevice(deviceId, { led_state: 'off' })
    if (status === 'expired') {
      wx.showToast({ title: '命令超时未执行', icon: 'none' })
      this.updateDevice(deviceId, { led_state: 'unknown' })
    }
  },

  watchCommand(deviceId, commandId, action, attempt) {
    const maxAttempts = 20
    api.getCommand(commandId).then((res) => {
      const command = res.data || {}
      if (command.status === 'done' || command.status === 'expired') {
        this.finishCommand(deviceId, commandId, action, command.status)
        return
      }
      if (attempt >= maxAttempts) {
        this.finishCommand(deviceId, commandId, action, 'expired')
        return
      }
      this._commandPollers = this._commandPollers || {}
      this._commandPollers[deviceId] = setTimeout(
        () => this.watchCommand(deviceId, commandId, action, attempt + 1), 1500)
    }).catch(() => {
      if (attempt >= maxAttempts) {
        this.finishCommand(deviceId, commandId, action, 'expired')
        return
      }
      this._commandPollers = this._commandPollers || {}
      this._commandPollers[deviceId] = setTimeout(
        () => this.watchCommand(deviceId, commandId, action, attempt + 1), 1500)
    })
  },

  onWsMessage(msg) {
    if (msg.type === 'snapshot') {
      if (this.data.devices.length === 0 && msg.devices) this.applyDevices(msg.devices)
    } else if (msg.type === 'status') {
      this.updateDevice(msg.device_id, { online: msg.online, led_state: msg.led })
    } else if (msg.type === 'command') {
      const pending = this.data.pending
      const item = pending[msg.device_id]
      if (item && item.command_id === msg.command_id &&
          (msg.status === 'done' || msg.status === 'expired')) {
        this.finishCommand(msg.device_id, msg.command_id, item.action, msg.status)
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
        pending[deviceId] = { command_id: res.data.command_id, action: action }
        this.setData({ pending })
        this.updateDevice(deviceId, { led_state: 'pending' })
        this.watchCommand(deviceId, res.data.command_id, action, 0)
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
      content: '确认解除此账号与设备的绑定？',
      success: async (r) => {
        if (!r.confirm) return
        try {
          await api.login()
          const res = await api.request('DELETE', '/v1/devices/' + deviceId)
          if (res.statusCode !== 200) throw new Error('unbind failed')
          const devices = self.data.devices.filter((d) => d.id !== deviceId)
          self.setData({ devices })
        } catch (e) {
          wx.showToast({ title: '解绑失败: ' + e.message, icon: 'none' })
        }
      }
    })
  },

  onPullDownRefresh() {
    this.loadDevices().then(() => wx.stopPullDownRefresh())
  }
})
