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
    this.loadPerception()
  },

  onShow() {
    this.refreshData()
  },

  // 端侧（ESP32-S3）上报的感知事件：人员识别、语音唤醒、场景执行、提醒等
  // 数据链路：板端 --MQTT--> 家庭私有云 --HTTPS--> 小程序
  async loadPerception() {
    this.setData({ alertsStatus: '加载中…' })
    try {
      await api.login()
      const res = await api.getEvents(12)
      if (res.statusCode !== 200) throw new Error('HTTP ' + res.statusCode)
      const events = (res.data && res.data.events) || []
      const alerts = events.map((e, i) => this._toAlert(e, i))
      this.setData({
        alerts: alerts,
        alertsStatus: alerts.length ? '' : '暂无感知事件'
      })
      wx.setStorageSync('perception', alerts)
    } catch (e) {
      const cached = wx.getStorageSync('perception') || []
      this.setData({
        alerts: cached,
        alertsStatus: cached.length ? '未连接家庭云，显示上次记录' : '未连接家庭云'
      })
    }
  },

  _toAlert(e, idx) {
    const map = {
      person_detected: { title: '检测到有人出现', level: 'medium' },
      motion: { title: '检测到移动', level: 'low' },
      kws_wake: { title: '语音唤醒', level: 'low' },
      wake_word: { title: '语音唤醒', level: 'low' },
      plan_created: { title: '已执行语义规划', level: 'low' },
      scene_completed: { title: '场景执行完成', level: 'low' },
      task_reminder: { title: '定时提醒已播报', level: 'medium' },
      device_offline: { title: '设备离线', level: 'high' }
    }
    const known = map[e.event_type]
    let payload = {}
    try {
      payload = typeof e.payload === 'string' ? JSON.parse(e.payload) : (e.payload || {})
    } catch (err) {
      payload = {}
    }
    let title = known ? known.title : (e.event_type || '感知事件')
    // 尽量带上人类可读的补充信息（如场景名、意图类型）
    if (payload.intent_type) title += '（' + payload.intent_type + '）'
    if (payload.title) title += '：' + payload.title
    return {
      id: e.event_id || String(idx),
      level: known ? known.level : 'low',
      title: title,
      time: this._fmtTime(e.created_at)
    }
  },

  _fmtTime(s) {
    if (!s) return ''
    // 后端存的是 UTC，展示时按本地时区解析
    const d = new Date(s.replace(' ', 'T') + (s.endsWith('Z') ? '' : 'Z'))
    if (isNaN(d.getTime())) return String(s).slice(0, 16)
    const p = (n) => n.toString().padStart(2, '0')
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
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
    this.loadPerception()
  },

  onPullDownRefresh() {
    this.refreshData()
    wx.stopPullDownRefresh()
  },

  goToDevices() {
    wx.switchTab({
      url: '/pages/devices/devices'
    })
  },

  // 分享：转发给微信好友/群（分享卡片默认带小程序名与页面截图）
  onShareAppMessage() {
    return {
      title: 'HomeMind · 端云协同的家庭感知中枢',
      path: '/pages/dashboard/dashboard'
    }
  },
})
