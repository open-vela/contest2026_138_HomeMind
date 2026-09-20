/* ============================================================
   HomeMind 栖心 · hm-app.js
   导航 / 滚动显现 / 粒子心 Canvas / 能力屏文案 / 控制台
   ============================================================ */
(function () {
  'use strict';

  /* ---------- 1. 顶部导航状态 ---------- */
  var nav = document.getElementById('nav');
  var sections = ['home', 'product', 'tech', 'story', 'console'];
  var navLinks = Array.prototype.slice.call(document.querySelectorAll('.nav-links a'));

  function onScroll() {
    // 滚动超过 hero 顶部约 40% 或不在 hero 视口 → 白色毛玻璃
    var hero = document.getElementById('home');
    var past = window.scrollY > (hero ? hero.offsetHeight * 0.45 : 80);
    nav.classList.toggle('solid', past);

    // scrollspy：当前区块高亮
    var cur = 'home';
    for (var i = 0; i < sections.length; i++) {
      var el = document.getElementById(sections[i]);
      if (!el) continue;
      var r = el.getBoundingClientRect();
      if (r.top <= window.innerHeight * 0.5 && r.bottom > window.innerHeight * 0.5) cur = sections[i];
    }
    for (var k = 0; k < navLinks.length; k++) {
      navLinks[k].classList.toggle('on', navLinks[k].getAttribute('href') === '#' + cur);
    }
  }
  window.addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  /* ---------- 2. 滚动显现动效 ---------- */
  var rvEls = Array.prototype.slice.call(document.querySelectorAll('.rv,.rv-left,.rv-right,.rv-scale'));
  if ('IntersectionObserver' in window) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        if (e.isIntersecting) {
          var d = parseFloat(e.target.getAttribute('data-d') || 0);
          e.target.style.transitionDelay = d + 's';
          e.target.classList.add('in');
          io.unobserve(e.target);
        }
      });
    }, { threshold: 0.12, rootMargin: '0px 0px -6% 0px' });
    rvEls.forEach(function (el) { io.observe(el); });
  } else {
    rvEls.forEach(function (el) { el.classList.add('in'); });
  }

  /* ---------- 3. 能力屏 · 轮换文案 ---------- */
  var rotators = {
    listenLine: {
      base: '归家的脚步声…',
      items: ['归家的脚步声…', '水烧开了，咕嘟咕嘟…', '孩子睡着了，呼吸很轻…'],
      every: 4200
    },
    talkBubble: {
      base: '今天辛苦了，早点休息。',
      items: ['今天辛苦了，早点休息。', '室温 24°C，湿度刚刚好。', '阳台的窗，我已经关好啦。', '明天要下雨，记得带伞。'],
      every: 4000
    },
    actLine: {
      base: '回家模式 · 灯光亮起…',
      items: ['回家模式 · 灯光亮起…', '窗帘缓缓拉开，晨光进屋…', '空调已调至 26°C…', '灯、温、帘 · 依次就位'],
      every: 4000
    }
  };
  var rotIdx = {};
  Object.keys(rotators).forEach(function (id) { rotIdx[id] = 0; });

  function advanceRotator(id) {
    var cfg = rotators[id], el = document.getElementById(id);
    if (!cfg || !el) return;
    rotIdx[id] = (rotIdx[id] + 1) % cfg.items.length;
    el.style.opacity = '0';
    setTimeout(function () {
      el.textContent = cfg.items[rotIdx[id]];
      el.style.opacity = '1';
    }, 420);
  }
  // 每个轮换独立计时，交错启动避免同时淡出
  Object.keys(rotators).forEach(function (id, idx) {
    setTimeout(function () {
      setInterval(function () { advanceRotator(id); }, rotators[id].every);
    }, idx * 1600);
  });

  /* ---------- 4. 首页 HERO · 粒子心 ---------- */
  function initHeart() {
    var canvas = document.getElementById('fxHeart');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var W = 0, H = 0, DPR = Math.min(window.devicePixelRatio || 1, 2);
    var parts = [], running = false, rafId = null;
    var mouse = { x: -1e4, y: -1e4 };
    var reduced = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    // 心形采样点（参数方程，surface + 内部填充）
    function heartPts(n) {
      var pts = [], i;
      for (i = 0; i < n; i++) {
        var t = Math.random() * Math.PI * 2;
        var s = Math.pow(Math.random(), 0.55); // 倾向表面一点，也落内部
        var x = 16 * Math.pow(Math.sin(t), 3);
        var y = -(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t));
        pts.push([x * s * 3.4, y * s * 3.4]);
      }
      return pts;
    }
    var targets = heartPts(1500);

    function sizeUp() {
      W = canvas.clientWidth; H = canvas.clientHeight;
      canvas.width = W * DPR; canvas.height = H * DPR;
      ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
      var n = Math.round(Math.min(1600, Math.max(420, (W * H) / 1400)));
      // 重采样并重设粒子
      targets = heartPts(n);
      var old = parts.slice(0);
      parts = [];
      var cx = W / 2, cy = H * 0.46, sc = Math.min(W, H) / (32 * 3.4 * 2.6);
      sc = Math.max(sc, Math.min(W, H) * 0.0105);
      for (var i = 0; i < n; i++) {
        var tg = targets[i];
        if (old[i]) {
          parts.push({ x: old[i].x, y: old[i].y, tx: cx + tg[0] * sc, ty: cy + tg[1] * sc, vx: old[i].vx, vy: old[i].vy, c: old[i].c, r: old[i].r });
        } else {
          parts.push({
            x: Math.random() * W, y: Math.random() * H,
            tx: cx + tg[0] * sc, ty: cy + tg[1] * sc,
            vx: (Math.random() - .5) * .4, vy: (Math.random() - .5) * .4,
            c: pickCol(), r: (Math.random() * 1.4 + .6) * DPR > 1.6 ? 1.35 : 1.05
          });
        }
      }
      // 让新粒子 tx/ty 对齐（保持旧粒子回中）
      for (var j = 0; j < parts.length; j++) parts[j].tx = cx + targets[j][0] * sc, parts[j].ty = cy + targets[j][1] * sc;
    }
    function pickCol() {
      var r = Math.random();
      if (r < 0.72) return '#3fa0ff';
      if (r < 0.9) return '#ff9f0a';
      if (r < 0.96) return '#ffffff';
      return '#5ac8fa';
    }
    function step() {
      if (!running) return;
      ctx.clearRect(0, 0, W, H);
      // 呼吸
      var br = 1 + Math.sin(Date.now() / 1300) * 0.018;
      var cx = W / 2, cy = H * 0.46;
      for (var i = 0; i < parts.length; i++) {
        var p = parts[i];
        var ox = (p.tx - cx) * (br - 1), oy = (p.ty - cy) * (br - 1);
        var tx = p.tx + ox, ty = p.ty + oy;
        // 鼠标排斥
        var dx = p.x - mouse.x, dy = p.y - mouse.y;
        var d2 = dx * dx + dy * dy;
        var R = 90;
        if (d2 < R * R && d2 > 0.01) {
          var d = Math.sqrt(d2);
          var f = (1 - d / R) * 1.15;
          tx += (dx / d) * f * 46;
          ty += (dy / d) * f * 46;
        }
        // 弹簧趋近
        p.vx += (tx - p.x) * 0.012;
        p.vy += (ty - p.y) * 0.012;
        p.vx *= 0.9; p.vy *= 0.9;
        p.x += p.vx; p.y += p.vy;
        ctx.globalAlpha = 0.85;
        ctx.fillStyle = p.c;
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, 6.2832);
        ctx.fill();
      }
      rafId = requestAnimationFrame(step);
    }
    function start() { if (!running) { running = true; requestAnimationFrame(step); } }
    function stop() { running = false; if (rafId) cancelAnimationFrame(rafId); }

    window.addEventListener('resize', function () { sizeUp(); }, { passive: true });
    window.addEventListener('mousemove', function (e) {
      var r = canvas.getBoundingClientRect();
      mouse.x = e.clientX - r.left; mouse.y = e.clientY - r.top;
    }, { passive: true });
    window.addEventListener('mouseout', function () { mouse.x = -1e4; mouse.y = -1e4; });
    canvas.addEventListener('pointermove', function (e) {
      var r = canvas.getBoundingClientRect();
      mouse.x = e.clientX - r.left; mouse.y = e.clientY - r.top;
    }, { passive: true });

    // 可视时运行（性能）
    var io2 = new IntersectionObserver(function (en) {
      en.forEach(function (e) { if (e.isIntersecting) { start(); } else { stop(); } });
    }, { threshold: 0.02 });
    io2.observe(canvas);

    sizeUp();
    if (reduced) {
      // 降级：直接画静态心形，不跑 RAF
      ctx.clearRect(0, 0, W, H);
      var cx = W / 2, cy = H * 0.46, sc = Math.min(W, H) * 0.0105;
      for (var s = 0; s < targets.length; s++) {
        ctx.fillStyle = pickCol(); ctx.globalAlpha = .9;
        ctx.beginPath(); ctx.arc(cx + targets[s][0] * sc, cy + targets[s][1] * sc, 1.2, 0, 6.2832); ctx.fill();
      }
    } else {
      start();
    }
  }

  /* ---------- 5. 控制台（登录 / 状态） ---------- */
  var TOKEN_KEY = 'hm_console_tk';
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; }); }
  function getToken() { try { return localStorage.getItem(TOKEN_KEY); } catch (e) { return null; } }
  function setToken(t) { try { if (t) localStorage.setItem(TOKEN_KEY, t); else localStorage.removeItem(TOKEN_KEY); } catch (e) {} }
  function fmtDT(s) {
    if (!s) return '从未';
    var d = new Date(/Z$|[+-]\d\d:\d\d$/.test(s) ? s : s + 'Z');
    if (isNaN(d)) return s;
    function p(n) { return (n < 10 ? '0' : '') + n; }
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }
  function showLogin(msg) {
    var lv = document.getElementById('loginView'), sv = document.getElementById('statusView');
    if (lv) lv.style.display = 'block';
    if (sv) sv.style.display = 'none';
    var em = document.getElementById('errMsg');
    if (em) em.textContent = msg || '';
  }
  function doLogin() {
    var u = document.getElementById('u'), p = document.getElementById('p'), em = document.getElementById('errMsg');
    if (!u || !p || !em) return;
    var un = u.value.trim(), pw = p.value;
    if (!un || !pw) { em.textContent = '请输入账号和密码'; return; }
    em.textContent = '登录中…';
    fetch('/v1/console/login', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: un, password: pw })
    }).then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
      .then(function (res) {
        if (!res.ok) throw new Error(res.j && res.j.detail ? res.j.detail : '登录失败');
        setToken(res.j.access_token);
        em.textContent = '';
        loadStatus();
      }).catch(function (e) { showLogin(e && e.message || '网络错误'); });
  }
  function dotEl(ok) { return '<span class="dot ' + (ok ? 'ok' : 'bad') + '"></span>'; }
  function svcRow(name, ok, tail) { return '<div class="svc"><span class="name">' + name + '</span><span style="white-space:nowrap">' + dotEl(ok) + ' ' + esc(tail) + '</span></div>'; }
  function loadStatus() {
    if (!getToken()) { showLogin('请先登录'); return; }
    var st = document.getElementById('stTime');
    if (st) st.textContent = '状态加载中…';
    fetch('/v1/console/status', { headers: { 'Authorization': 'Bearer ' + getToken() } })
      .then(function (r) {
        if (r.status === 401) { setToken(null); showLogin('登录已过期，请重新登录'); throw new Error('expired'); }
        return r.json();
      })
      .then(function (s) { renderStatus(s); })
      .catch(function (e) { if (e && e.message !== 'expired' && st) st.textContent = '加载失败：' + e.message; });
  }
  function renderStatus(s) {
    var svcDot = document.getElementById('svcDot'), dbDot = document.getElementById('dbDot'), devDot = document.getElementById('devDot');
    if (svcDot) svcDot.className = 'dot ' + (s.services.api === 'ok' && s.services.nginx && s.services.mqtt ? 'ok' : 'bad');
    var svc = '';
    svc += svcRow('FastAPI 服务', s.services.api === 'ok', '正常');
    svc += svcRow('Nginx 网关', !!s.services.nginx, s.services.nginx ? '正常' : '异常');
    svc += svcRow('MQTT 消息服务', !!s.services.mqtt, s.services.mqtt ? '正常' : '异常');
    if (s.services.cert_expires) {
      var days = Math.floor((new Date(s.services.cert_expires) - Date.now()) / 86400000);
      svc += svcRow('HTTPS 证书', days > 30, esc(fmtDT(s.services.cert_expires)) + '（剩 ' + days + ' 天）');
    }
    document.getElementById('svcList').innerHTML = svc;
    if (dbDot) dbDot.className = 'dot ' + (s.database.ok ? 'ok' : 'bad');
    document.getElementById('dbList').innerHTML =
      '<div class="num-row"><span class="name">数据库</span><span>' + (s.database.ok ? '正常' : '异常') + '</span></div>' +
      '<div class="num-row"><span class="name">注册用户</span><span class="num">' + s.database.users + '</span></div>' +
      '<div class="num-row"><span class="name">设备绑定</span><span class="num">' + s.database.bindings + '</span></div>' +
      '<div class="num-row"><span class="name">下发命令</span><span class="num">' + s.database.commands + '</span></div>';
    var devs = (s.devices && s.devices.list) || [];
    if (devDot) devDot.className = 'dot ' + (s.devices.total > 0 && s.devices.online > 0 ? 'ok' : 'bad');
    var dc = document.getElementById('devCount');
    if (dc) dc.textContent = '在线 ' + s.devices.online + ' / 共 ' + s.devices.total;
    var dl = document.getElementById('devList');
    if (!devs.length) { dl.innerHTML = '<div class="muted">暂无设备上报</div>'; }
    else {
      var h = '';
      for (var i = 0; i < devs.length; i++) {
        var d = devs[i];
        h += '<div class="dev-line">' + dotEl(d.online) + '<span class="did">' + esc(d.device_id) + '</span><span>' + (d.online ? '在线' : '离线') + '</span><span class="muted">LED ' + esc(d.led_state) + '</span><span class="muted">' + fmtDT(d.last_seen) + '</span></div>';
      }
      dl.innerHTML = h;
    }
    var st = document.getElementById('stTime');
    if (st) st.textContent = '更新于 ' + fmtDT(s.time);
    document.getElementById('loginView').style.display = 'none';
    document.getElementById('statusView').style.display = 'block';
  }
  function doLogout() {
    setToken(null);
    var p = document.getElementById('p'); if (p) p.value = '';
    showLogin('已退出登录');
    updateNavLogin();
  }
  // 右上角登录按钮
  var navLogin = document.getElementById('navLogin');
  function updateNavLogin() {
    var txt = document.getElementById('navLoginTxt');
    if (txt) txt.textContent = getToken() ? '控制台 · 已登录' : '登录控制台';
    if (navLogin) navLogin.classList.toggle('logged', !!getToken());
  }
  if (navLogin) {
    navLogin.addEventListener('click', function (e) {
      e.preventDefault();
      var c = document.getElementById('console');
      if (c) c.scrollIntoView({ behavior: 'smooth', block: 'start' });
      setTimeout(function () {
        if (!getToken()) {
          var u = document.getElementById('u');
          if (u) u.focus({ preventScroll: true });
        } else {
          loadStatus();
        }
      }, 650);
    });
  }
  document.getElementById('p').addEventListener('keydown', function (e) { if (e.key === 'Enter') doLogin(); });
  document.getElementById('u').addEventListener('keydown', function (e) { if (e.key === 'Enter') doLogin(); });
  document.getElementById('loginBtn').addEventListener('click', doLogin);

  if (getToken()) { loadStatus(); } else { showLogin(''); }
  updateNavLogin();

  /* ---------- 6. 深色 hero 下导航自动变浅（第一屏） ---------- */
  // (已由 nav.solid 控制；当 hash 直达 #console 等白底区时立即 solid)
  if (location.hash && location.hash !== '#home') {
    setTimeout(function () {
      var el = document.querySelector(location.hash);
      if (el) el.scrollIntoView({ behavior: 'auto', block: 'start' });
      nav.classList.add('solid');
    }, 60);
  }

  /* ---------- 启动 ---------- */
  if (document.readyState === 'complete' || document.readyState === 'interactive') {
    initHeart();
  } else {
    window.addEventListener('DOMContentLoaded', initHeart);
  }
})();
