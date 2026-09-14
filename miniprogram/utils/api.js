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
    const hadToken = !!wx.getStorageSync('accessToken')
    wx.request({
      url: baseUrl() + path,
      method: method,
      data: data,
      header: Object.assign({ 'content-type': 'application/json' }, authHeader()),
      success: (res) => {
        if (res.statusCode === 401 && hadToken) {
          // 缓存 token 失效（过期/后端密钥变更/换后端）：立即清除，
          // 让带鉴权的调用走强制重登路径，而不是永远拿着旧 token。
          wx.removeStorageSync('accessToken')
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
// force=true 时忽略缓存 token 强制重登（401 自愈用）
async function login(force) {
  if (!force) {
    const saved = wx.getStorageSync('accessToken')
    if (saved) return saved
  }
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

// 带鉴权请求：401 时强制重登一次再试（处理缓存 token 失效/后端密钥变更）
async function authRequest(method, path, data) {
  await login()
  try {
    return await request(method, path, data)
  } catch (e) {
    if (e && e.message === 'unauthorized') {
      await login(true)
      return request(method, path, data)
    }
    throw e
  }
}

function getDevices() {
  return authRequest('GET', '/v1/devices')
}

function sendCommand(deviceId, action, params) {
  return authRequest('POST', '/v1/devices/' + deviceId + '/commands', {
    action: action,
    params: params || {}
  })
}

function getCommand(commandId) {
  return authRequest('GET', '/v1/commands/' + commandId)
}

module.exports = { login, wxLogin, getDevices, sendCommand, getCommand, request, authRequest, baseUrl }
