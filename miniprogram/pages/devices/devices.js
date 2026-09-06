// pages/devices/devices.js —— C1：对接真实后端（登录/绑定/下发/WS 回显）
const app = getApp()
const api = require('../../utils/api.js')
const ws = require('../../utils/ws.js')

// 与网关 mihome_adapter 一致：只接受 light.*/switch.* 实体
const MIHOME_ENTITY_RE = /^(light|switch)\.[A-Za-z0-9_]+$/

Page({
  data: {
    devices: [],
    loading: false,
    pending: {},            // device_id -> {command_id, action}（直到最终状态防重入）
    showAddModal: false,
    newDevice: { device_id: '', name: '' },
    // 米家控制（经网关 -> Home Assistant -> 真实设备）
    mihomeEntity: '',       // 持久化到 Storage('mihomeEntity')
    mihomePending: {},      // device_id -> {command_id, kind}
    mihomeResult: {}        // device_id -> {text, ok}
  },

  onShow() {
    this.setData({ mihomeEntity: wx.getStorageSync('mihomeEntity') || '' })
    this.ensureAndLoad()
  },

  onHide() {
    this.closeWs()
    this.stopCommandPollers()
    this.stopMihomePollers()
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

  stopMihomePollers() {
    const pollers = this._mihomePollers || {}
    Object.keys(pollers).forEach((deviceId) => clearTimeout(pollers[deviceId]))
    this._mihomePollers = {}
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
        return
      }
      const mihomePending = this.data.mihomePending
      const mi = mihomePending[msg.device_id]
      if (mi && mi.command_id === msg.command_id &&
          (msg.status === 'done' || msg.status === 'expired')) {
        this.finishMihome(msg.device_id, msg.command_id, msg.status)
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

  // ---------- 米家控制（云端 -> 网关 -> Home Assistant -> 真实设备） ----------

  onMihomeEntityInput(e) {
    const entity = (e.detail.value || '').trim()
    this.setData({ mihomeEntity: entity })
    wx.setStorageSync('mihomeEntity', entity)
  },

  mihomeAction(e) {
    const deviceId = e.currentTarget.dataset.id
    const kind = e.currentTarget.dataset.kind  // 'on' | 'off' | 'query'
    const entity = this.data.mihomeEntity
    if (this.data.mihomePending[deviceId]) return
    if (!MIHOME_ENTITY_RE.test(entity)) {
      wx.showToast({ title: '实体 ID 须为 light.* 或 switch.*', icon: 'none' })
      return
    }
    const action = kind === 'query' ? 'mihome.get_state' : 'mihome.set_power'
    const params = kind === 'query'
      ? { entity_id: entity }
      : { entity_id: entity, on: kind === 'on' }
    api.sendCommand(deviceId, action, params).then((res) => {
      if (res.statusCode === 200) {
        const mihomePending = Object.assign({}, this.data.mihomePending)
        mihomePending[deviceId] = { command_id: res.data.command_id, kind: kind }
        this.setData({ mihomePending })
        this.setMihomeResult(deviceId, '执行中…', null)
        this.watchMihome(deviceId, res.data.command_id, 0)
      } else {
        const detail = (res.data && res.data.detail) ? String(res.data.detail) : ('HTTP ' + res.statusCode)
        this.setMihomeResult(deviceId, '下发被拒: ' + detail, false)
      }
    }).catch((err) => {
      this.setMihomeResult(deviceId, '下发失败: ' + err.message, false)
    })
  },

  watchMihome(deviceId, commandId, attempt) {
    const maxAttempts = 20
    api.getCommand(commandId).then((res) => {
      const command = res.data || {}
      if (command.status === 'done' || command.status === 'expired') {
        this.finishMihome(deviceId, commandId, command.status)
        return
      }
      if (attempt >= maxAttempts) {
        this.finishMihome(deviceId, commandId, 'expired')
        return
      }
      this._mihomePollers = this._mihomePollers || {}
      this._mihomePollers[deviceId] = setTimeout(
        () => this.watchMihome(deviceId, commandId, attempt + 1), 1500)
    }).catch(() => {
      if (attempt >= maxAttempts) {
        this.finishMihome(deviceId, commandId, 'expired')
        return
      }
      this._mihomePollers = this._mihomePollers || {}
      this._mihomePollers[deviceId] = setTimeout(
        () => this.watchMihome(deviceId, commandId, attempt + 1), 1500)
    })
  },

  finishMihome(deviceId, commandId, status) {
    const mihomePending = this.data.mihomePending
    const mi = mihomePending[deviceId]
    if (!mi || mi.command_id !== commandId) return
    const np = Object.assign({}, mihomePending)
    delete np[deviceId]
    this.setData({ mihomePending: np })
    const kind = mi.kind
    if (status === 'done') {
      // 网关只在实体状态回读确认后才发 done
      const text = kind === 'on' ? '已开启（网关回读确认）'
        : kind === 'off' ? '已关闭（网关回读确认）'
        : '状态查询完成（结果见网关）'
      this.setMihomeResult(deviceId, text, true)
    } else {
      this.setMihomeResult(deviceId, '未确认（超时、TTL 过期或状态回读不匹配）', false)
    }
  },

  setMihomeResult(deviceId, text, ok) {
    const mihomeResult = Object.assign({}, this.data.mihomeResult)
    mihomeResult[deviceId] = { text: text, ok: ok }
    this.setData({ mihomeResult })
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
