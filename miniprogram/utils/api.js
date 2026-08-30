// utils/api.js —— HomeMind C1 后端对接（无硬编码密钥）
const app = getApp()

function baseUrl() {
  return (app.globalData.serverUrl || '').replace(/\/+$/, '')
}

function authHeader() {
  const token = wx.getStorageSync('accessToken')
  return token ? { Authorization: 'Bearer ' + token } : {}
}

function request(method, path, data) {
  return new Promise((resolve, reject) => {
    wx.request({
      url: baseUrl() + path,
      method: method,
      data: data,
      header: Object.assign({ 'content-type': 'application/json' }, authHeader()),
      success: (res) => {
        if (res.statusCode === 401) {
          reject(new Error('unauthorized'))
          return
        }
        resolve(res)
      },
      fail: (e) => reject(e)
    })
  })
}

function wxLogin() {
  return new Promise((resolve, reject) => {
    wx.login({
      success: (r) => (r.code ? resolve(r.code) : reject(new Error('no code'))),
      fail: reject
    })
  })
}

// 小程序登录：wx.login -> /v1/auth/wechat/login -> 存 token
async function login() {
  if (wx.getStorageSync('accessToken')) return wx.getStorageSync('accessToken')
  const code = await wxLogin()
  const res = await request('POST', '/v1/auth/wechat/login', { code: code })
  if (res.statusCode !== 200) {
    throw new Error('login failed: ' + JSON.stringify(res.data))
  }
  const d = res.data
  wx.setStorageSync('accessToken', d.access_token)
  wx.setStorageSync('userId', d.user_id)
  return d.access_token
}

function getDevices() {
  return request('GET', '/v1/devices')
}

function sendCommand(deviceId, action, params) {
  return request('POST', '/v1/devices/' + deviceId + '/commands', {
    action: action,
    params: params || {}
  })
}

function getCommand(commandId) {
  return request('GET', '/v1/commands/' + commandId)
}

module.exports = { login, wxLogin, getDevices, sendCommand, getCommand, request, baseUrl }
