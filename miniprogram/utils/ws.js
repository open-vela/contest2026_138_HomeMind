// utils/ws.js —— WSS 状态/ACK 推送（带心跳）
const app = getApp()

function connect(onMessage) {
  const base = (app.globalData.serverUrl || 'https://hfy-ai.cloud')
    .replace(/^https/, 'wss')
    .replace(/\/+$/, '')
  const token = wx.getStorageSync('accessToken')
  if (!token) return null

  const sock = wx.connectSocket({
    url: base + '/v1/ws/app?token=' + encodeURIComponent(token),
    fail: (e) => console.error('ws connect fail', e)
  })

  let hb = null
  sock.onOpen(() => {
    hb = setInterval(() => {
      try { sock.send({ data: 'ping' }) } catch (e) {}
    }, 20000)
  })
  sock.onMessage((res) => {
    try {
      const msg = JSON.parse(res.data)
      if (onMessage) onMessage(msg)
    } catch (e) {}
  })
  sock.onClose(() => {
    if (hb) clearInterval(hb)
  })
  return sock
}

module.exports = { connect }
