// pages/calendar/calendar.js
Page({
  data: {
    calendarList: [],
    currentDate: '',
    selectedDate: '',
    showAddModal: false,
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

  loadCalendar() {
    const calendarList = wx.getStorageSync('calendar') || []
    this.setData({
      calendarList: calendarList
    })
  },

  onDateChange(e) {
    this.setData({
      selectedDate: e.detail.value
    })
  },

  addCalendar() {
    this.setData({
      showAddModal: true,
      newCalendar: {
        title: '',
        time: '',
        description: '',
        reminder: false
      }
    })
  },

  hideAddModal() {
    this.setData({
      showAddModal: false
    })
  },

  onInputChange(e) {
    const field = e.currentTarget.dataset.field
    this.setData({
      [`newCalendar.${field}`]: e.detail.value
    })
  },

  confirmAdd() {
    const { newCalendar, selectedDate } = this.data
    if (!newCalendar.title) {
      wx.showToast({
        title: '请输入日程标题',
        icon: 'none'
      })
      return
    }

    const calendarList = wx.getStorageSync('calendar') || []
    const calendar = {
      id: Date.now().toString(),
      ...newCalendar,
      date: selectedDate,
      createdAt: new Date().toISOString()
    }
    calendarList.push(calendar)
    wx.setStorageSync('calendar', calendarList)

    this.setData({
      showAddModal: false,
      calendarList: calendarList
    })

    wx.showToast({
      title: '已保存到本机',
      icon: 'success'
    })
  },

  deleteCalendar(e) {
    const calendarId = e.currentTarget.dataset.id
    wx.showModal({
      title: '确认删除',
      content: '确定要删除这个日程吗？',
      success: (res) => {
        if (res.confirm) {
          let calendarList = wx.getStorageSync('calendar') || []
          calendarList = calendarList.filter(c => c.id !== calendarId)
          wx.setStorageSync('calendar', calendarList)
          this.setData({ calendarList: calendarList })
          wx.showToast({
            title: '删除成功',
            icon: 'success'
          })
        }
      }
    })
  },

  onPullDownRefresh() {
    this.loadCalendar()
    wx.stopPullDownRefresh()
  }
})
