// pages/calendar/calendar.js
// 日程/待办：优先与家庭私有云同步（跨端：小程序、板端、小爱提醒同一份数据）；
// 取不到云端时回落本机缓存，保证断网也能查看与新增。
const api = require('../../utils/api.js')

Page({
  data: {
    calendarList: [],
    currentDate: '',
    selectedDate: '',
    showAddModal: false,
    syncState: 'loading',   // loading | cloud | local | error
    syncHint: '正在同步…',
    newCalendar: {
      title: '',
      time: '',
      description: '',
      reminder: false
    }
  },

  onLoad() {
    this.setCurrentDate()
    this.loadCalendar()
  },

  onShow() {
    this.loadCalendar()
  },

  setCurrentDate() {
    const now = new Date()
    const year = now.getFullYear()
    const month = (now.getMonth() + 1).toString().padStart(2, '0')
    const day = now.getDate().toString().padStart(2, '0')
    const dateStr = `${year}-${month}-${day}`
    this.setData({
      currentDate: dateStr,
      selectedDate: dateStr
    })
  },

  // 云端任务 -> 页面结构
  _fromCloud(tasks) {
    return (tasks || []).map(t => {
      const due = t.due_at || ''
      return {
        id: t.task_id,
        title: t.title || '',
        date: due ? due.slice(0, 10) : '',
        time: due.length >= 16 ? due.slice(11, 16) : '',
        description: t.note || '',
        reminder: !!t.remind_at,
        status: t.status,
        synced: true
      }
    })
  },

  _fromLocal(list) {
    return (list || []).map(c => Object.assign({ synced: false }, c))
  },

  loadCalendar() {
    const local = wx.getStorageSync('calendar') || []
    this.setData({ calendarList: this._fromLocal(local) })

    api.getTasks()
      .then(res => {
        if (res.statusCode === 200 && res.data && Array.isArray(res.data.tasks)) {
          const list = this._fromCloud(res.data.tasks)
          this.setData({
            calendarList: list,
            syncState: 'cloud',
            syncHint: '已与家庭云同步'
          })
          wx.setStorageSync('calendar', list)
        } else {
          this._fallback(local, '云端返回异常')
        }
      })
      .catch(() => {
        this._fallback(local, '未连接家庭云，显示本机数据')
      })
  },

  _fallback(local, hint) {
    this.setData({
      calendarList: this._fromLocal(local),
      syncState: local.length ? 'local' : 'error',
      syncHint: hint
    })
  },

  onDateChange(e) {
    this.setData({ selectedDate: e.detail.value })
  },

  addCalendar() {
    this.setData({
      showAddModal: true,
      newCalendar: { title: '', time: '', description: '', reminder: false }
    })
  },

  hideAddModal() {
    this.setData({ showAddModal: false })
  },

  onInputChange(e) {
    const field = e.currentTarget.dataset.field
    this.setData({ [`newCalendar.${field}`]: e.detail.value })
  },

  onReminderChange(e) {
    this.setData({ 'newCalendar.reminder': !!e.detail.value })
  },

  // 组装成后端能解析的本地时间字符串（后端按 Asia/Shanghai 存）
  _composeDue() {
    const { newCalendar, selectedDate } = this.data
    const time = (newCalendar.time || '').trim()
    if (time) {
      return `${selectedDate} ${time.length === 5 ? time + ':00' : time}`
    }
    return selectedDate
  },

  confirmAdd() {
    const { newCalendar, selectedDate } = this.data
    if (!newCalendar.title || !newCalendar.title.trim()) {
      wx.showToast({ title: '请输入日程标题', icon: 'none' })
      return
    }
    const due = this._composeDue()
    const payload = {
      title: newCalendar.title.trim(),
      note: newCalendar.description || '',
      due_at: due,
      timezone: 'Asia/Shanghai',
      // 勾选提醒即到期提醒；reminder worker 会通过小爱/板端播报
      remind_at: newCalendar.reminder ? due : '',
      status: 'pending'
    }

    wx.showLoading({ title: '保存中', mask: true })
    api.createTask(payload)
      .then(res => {
        wx.hideLoading()
        if (res.statusCode === 200 || res.statusCode === 201) {
          this.setData({ showAddModal: false })
          wx.showToast({ title: '已同步到家庭云', icon: 'success' })
          this.loadCalendar()
        } else {
          this._saveLocal(payload, '云端保存失败，已存本机')
        }
      })
      .catch(() => {
        wx.hideLoading()
        this._saveLocal(payload, '未连接家庭云，已存本机')
      })
  },

  _saveLocal(payload, hint) {
    const list = wx.getStorageSync('calendar') || []
    list.push({
      id: Date.now().toString(),
      title: payload.title,
      date: this.data.selectedDate,
      time: this.data.newCalendar.time || '',
      description: payload.note,
      reminder: !!payload.remind_at,
      createdAt: new Date().toISOString(),
      synced: false
    })
    wx.setStorageSync('calendar', list)
    this.setData({ showAddModal: false })
    wx.showToast({ title: hint, icon: 'none' })
    this.loadCalendar()
  },

  deleteCalendar(e) {
    const item = e.currentTarget.dataset.item || {}
    const id = e.currentTarget.dataset.id
    wx.showModal({
      title: '确认删除',
      content: '确定要删除这个日程吗？',
      success: (res) => {
        if (!res.confirm) return
        if (item.synced && id) {
          // 云端数据用取消状态表示删除，保留审计痕迹
          api.updateTask(id, { status: 'cancelled' })
            .then(() => {
              wx.showToast({ title: '已删除', icon: 'success' })
              this.loadCalendar()
            })
            .catch(() => this._deleteLocal(id, '云端删除失败，仅删本机'))
        } else {
          this._deleteLocal(id, '删除成功')
        }
      }
    })
  },

  _deleteLocal(id, hint) {
    let list = wx.getStorageSync('calendar') || []
    list = list.filter(c => c.id !== id)
    wx.setStorageSync('calendar', list)
    this.setData({ calendarList: this._fromLocal(list) })
    wx.showToast({ title: hint, icon: 'none' })
  },

  onPullDownRefresh() {
    this.loadCalendar()
    wx.stopPullDownRefresh()
  },

  onShareAppMessage() {
    return {
      title: 'HomeMind · 端云协同的家庭感知中枢',
      path: '/pages/calendar/calendar'
    }
  }
})
