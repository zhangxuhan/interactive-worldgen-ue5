/* WorldGen portfolio-demo front end. Single-page flow: input -> running ->
   result. Talks to the local server only; no model key, no scene parsing and
   no geometry rules live here - everything displayed comes from the project's
   existing pipeline artifacts. All UI strings go through T() (i18n.js);
   server-produced dynamic sentences (stage details, validator feedback) are
   displayed as-is from the pipeline. */
'use strict';

var STATE = {
  view: 'input', taskId: null, timer: null, busy: false,
  lastDesc: '', samples: [], archivesOpen: false,
  result: null,          // current result item shown in view 3 (never stale)
  lastJob: null,         // last polled job (for language re-render)
  lang: 'zh'
};

var $ = function (id) { return document.getElementById(id); };

var STAGE_ORDER = ['model', 'validate', 'plan', 'build', 'assets', 'preview', 'e2e'];
var VIEW_TABS = [['overview', 'tab_overview'], ['desk_key', 'tab_desk_key'],
                 ['key', 'tab_key'], ['exit', 'tab_exit']];

function api(path, opts) {
  return fetch(path, opts || {}).then(function (r) {
    return r.json().catch(function () { return {}; }).then(function (j) {
      if (!r.ok) { throw new Error(j.error || ('HTTP ' + r.status)); }
      return j;
    });
  });
}

function esc(s) {
  return String(s === null || s === undefined ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function stageName(k) { return T('st_' + k); }
function wallT(w) { return w ? T('wall_' + w) : '—'; }

function showView(name) {
  STATE.view = name;
  $('view-input').className = 'view' + (name === 'input' ? '' : ' hidden');
  $('view-run').className = 'view' + (name === 'run' ? '' : ' hidden');
  $('view-result').className = 'view' + (name === 'result' ? '' : ' hidden');
  window.scrollTo(0, 0);
}

function setStatus(textKey, cls, vars) {
  var c = $('status-chip');
  c.textContent = vars ? T(textKey, vars) : T(textKey);
  c.className = 'chip ' + (cls || '');
}
function setQueue(n) {
  var c = $('queue-chip');
  if (!n) { c.className = 'chip hidden'; return; }
  c.textContent = T('queue_n', { n: n });
  c.className = 'chip chip-run';
}

function showInputError(msg) {
  var e = $('input-error');
  if (!msg) { e.className = 'error hidden'; e.textContent = ''; return; }
  e.className = 'error'; e.textContent = msg;
}

/* ------------------------------------------------------------- input view */
// Card title is composed CLIENT-SIDE from room data (the server's `title`
// field is pipeline-language Chinese and is deliberately not used here).
function sampleTitle(s) {
  var r = s.room || {};
  if (!r.width_cm || !r.length_cm) { return s.scene_id || ''; }
  var en = (s.entrance || {}).wall, ex = (s.exit || {}).wall;
  return r.width_cm + '×' + r.length_cm + ' cm · '
    + wallT(en) + T('short_in') + ' / ' + wallT(ex) + T('short_out');
}

function fillSamples(list) {
  STATE.samples = list;
  var strip = $('archives');
  strip.innerHTML = '';
  list.forEach(function (s) {
    var card = document.createElement('button');
    card.className = 'arch-card';
    card.setAttribute('data-key', s.key);
    var img = s.thumb || '';
    var meta = (s.e2e && s.e2e.verdict === 'ALL PASS')
      ? T('arch_verified', { n: s.e2e.checks }) : T('arch_plain');
    card.innerHTML =
      '<img src="' + esc(img) + '" alt="">' +
      '<div class="ac-body">' +
      '<div class="ac-tag">' + T('arch_tag', { k: s.key }) + '</div>' +
      '<div class="ac-title">' + esc(sampleTitle(s)) + '</div>' +
      '<div class="ac-meta">' + meta + '</div>' +
      '</div>';
    card.onclick = function () { loadArchive(s.key); };
    strip.appendChild(card);
  });
}

// Delegated binding: the ex-note (data-i18n-html) is re-rendered on language
// switch, which would drop a direct onclick on #lnk-archives.
document.addEventListener('click', function (ev) {
  if (ev.target && ev.target.id === 'lnk-archives') {
    STATE.archivesOpen = !STATE.archivesOpen;
    $('archives').className = 'archives' + (STATE.archivesOpen ? '' : ' hidden');
    ev.target.textContent = STATE.archivesOpen ? T('archives_hide') : T('archives_show');
  }
});

document.querySelectorAll('.ex-chip').forEach(function (b) {
  b.onclick = function () {
    var s = STATE.samples.filter(function (x) { return x.key === b.getAttribute('data-key'); })[0];
    if (s && s.description) { $('desc').value = s.description; $('desc').focus(); }
  };
});

$('btn-generate').onclick = function () {
  if (STATE.busy) { showInputError(T('err_busy')); return; }
  var d = $('desc').value.trim();
  if (d.length < 6) { showInputError(T('err_short')); return; }
  startTask(d, null);
};

function startTask(desc, sampleKey) {
  STATE.lastDesc = desc;
  showInputError('');
  setStatus('status_submitting', 'chip-run');
  var url = sampleKey ? '/api/regenerate' : '/api/generate';
  var body = sampleKey ? { sample: sampleKey } : { description: desc };
  api(url, { method: 'POST', headers: { 'Content-Type': 'application/json' },
             body: JSON.stringify(body) }).then(function (r) {
    STATE.taskId = r.task_id;
    enterRunView();
    poll();
  }).catch(function (e) {
    setStatus('status_idle', '');
    showInputError(T('err_submit', { m: e.message }));
  });
}

/* -------------------------------------------------------------- run view */
function enterRunView() {
  showView('run');
  $('run-fail').className = 'fail-card hidden';
  $('run-title').textContent = T('run_title');
  $('run-sub').textContent = T('run_sub');
  renderStages(null);
  renderRunTech(null);
}

function renderStages(job) {
  var ol = $('stages');
  if (!ol) { return; }
  ol.innerHTML = '';
  var byKey = {};
  (job && job.stages ? job.stages : []).forEach(function (s) { byKey[s.key] = s; });
  var currentText = '';
  STAGE_ORDER.forEach(function (k) {
    var s = byKey[k] || { state: 'pending', detail: '' };
    var li = document.createElement('li');
    li.className = 'st-' + (s.state || 'pending');
    li.innerHTML =
      '<span class="dot"></span>' +
      '<span><span class="st-name">' + esc(stageName(k)) + '</span>' +
      (s.detail ? '<div class="st-detail">' + esc(s.detail) + '</div>' : '') +
      '</span>';
    ol.appendChild(li);
    if (s.state === 'running' && s.detail) { currentText = s.detail; }
  });
  if (currentText) { $('run-title').textContent = currentText; }
  else if (job === null) { $('run-title').textContent = T('run_title'); }
}

function renderRunTech(job) {
  var box = $('run-tech-body');
  if (!box) { return; }
  if (!job) { box.innerHTML = '<div class="muted" style="font-size:12.5px">' + T('tech_pending') + '</div>'; return; }
  var H = [];
  (job.stages || []).forEach(function (s) {
    if (!s.lines || !s.lines.length) { return; }
    H.push('<div class="tsec"><h4>' + T('tech_raw', { s: esc(stageName(s.key) || s.key), n: s.lines.length }) + '</h4>' +
           '<pre style="font:11.5px/1.5 Consolas,monospace;color:#93a1ad;white-space:pre-wrap;max-height:130px;overflow:auto">' +
           esc(s.lines.join('\n')) + '</pre></div>');
    if (s.log) {
      H.push('<div class="tsec"><span class="tlink" data-log="' + esc(s.log) + '">' + T('tech_log', { f: esc(s.log) }) + '</span></div>');
    }
  });
  box.innerHTML = H.join('') || '<div class="muted" style="font-size:12.5px">' + T('tech_none') + '</div>';
  box.querySelectorAll('[data-log]').forEach(function (l) {
    l.onclick = function () {
      openModal(T('tech_modal_log', { f: l.getAttribute('data-log') }),
        '/api/task/' + job.id + '/log?name=' + encodeURIComponent(l.getAttribute('data-log')));
    };
  });
}

function poll() {
  if (!STATE.taskId) { return; }
  api('/api/task/' + STATE.taskId).then(function (job) {
    STATE.lastJob = job;
    renderStages(job);
    renderRunTech(job);
    STATE.busy = job.state === 'running' || job.state === 'queued';
    setQueue(job.state === 'queued' ? (job.queue_position || 0) : 0);
    if (job.state === 'running') { setStatus('status_running', 'chip-run'); }
    else if (job.state === 'queued') { setStatus('status_queued', 'chip-run'); }
    else if (job.state === 'done') { setStatus('status_done', 'chip-done'); }
    else if (job.state === 'failed') { setStatus('status_failed', 'chip-bad'); }

    if (job.state === 'failed') {
      showFailure(job);
      stopPoll();
      return;
    }
    if (job.state === 'done') {
      renderTaskResult(job);
      stopPoll();
      return;
    }
    STATE.timer = setTimeout(poll, 2000);
  }).catch(function (e) {
    setStatus('status_poll_fail', 'chip-bad');
    $('run-title').textContent = T('status_poll_fail') + '：' + e.message;
    stopPoll();
  });
}

function stopPoll() { if (STATE.timer) { clearTimeout(STATE.timer); STATE.timer = null; } }

function showFailure(job) {
  var st = (job.stages || []).filter(function (s) { return s.state === 'failed'; })[0];
  var stageNameStr = st ? (stageName(st.key) || st.label || st.key) : '';
  var reason = (st && st.detail) ? st.detail : (job.error || T('m_unk'));
  $('run-title').textContent = T('fail_title');
  $('run-sub').textContent = T('fail_sub');
  var extra = (st && st.lines && st.lines.length)
    ? '<div style="margin-top:8px;color:var(--muted);font-size:12px">' + T('fail_more') + '</div>' : '';
  $('fail-reason').innerHTML =
    '<div class="fr-stage">' + T('fail_stage', { s: esc(stageNameStr) }) + '</div>' + esc(reason) + extra;
  $('run-fail').className = 'fail-card';
  $('btn-retry').onclick = function () {
    var sampleKey = job.sample || null;
    var desc = sampleKey ? null : STATE.lastDesc;
    if (!sampleKey && !desc) { showView('input'); return; }
    startTask(desc || '', sampleKey);
  };
  $('btn-back').onclick = function () {
    if (STATE.lastDesc) { $('desc').value = STATE.lastDesc; }
    showView('input');
  };
}

/* ------------------------------------------------------------ result view */
function loadArchive(key) {
  stopPoll();
  STATE.taskId = null;
  setStatus('status_archive', '');
  api('/api/archive/' + key).then(function (a) {
    var item = {
      kind: 'archive',
      sample: a.sample,
      title: a.sample_title || null,
      scene_id: a.scene_id, level: a.level,
      room: a.room, entrance: a.entrance, exit: a.exit,
      objects: a.objects, objective: a.objective,
      desk_asset: a.desk_asset, assets: a.assets,
      e2e: a.e2e || {}, previews: a.previews || {},
      clip: (a.previews && a.previews.clip) || null,
      clip_frames: a.clip_frames, clip_seconds: a.clip_seconds,
      preview_at: a.preview_at,
      files: a.files, archive_rel: a.archive_rel,
      model: null
    };
    showResult(item);
  }).catch(function (e) { showView('input'); showInputError(T('err_archive', { m: e.message })); });
}

function renderTaskResult(job) {
  var r = job.results || {};
  var item = {
    kind: 'live',
    task_id: job.id,
    scene_id: job.scene_id, level: job.level,
    room: r.room, entrance: r.entrance, exit: r.exit,
    objects: r.objects, objective: r.objective,
    desk_asset: r.desk_asset || null, assets: r.assets || null,
    e2e: r.e2e || {}, model: job.model,
    rounds: job.rounds || [],
    finished: job.finished,
    clip: r.clip ? '/media/tasks/' + job.id + '/' + r.clip : null,
    clip_frames: r.clip_frames, clip_seconds: r.clip_seconds,
    preview_at: r.preview_at,
    previews: {}, stages: job.stages
  };
  Object.keys(r.preview || {}).forEach(function (k) {
    var p = r.preview[k];
    if (p) { item.previews[k] = '/media/tasks/' + job.id + '/' + p; }
  });
  item.files = {
    spec: 'scene_spec.json', plan: 'plan.json', e2e: 'e2e.log',
    umap_check: 'umap_check.json', task: 'task.json'
  };
  showResult(item);
}

function showResult(item) {
  STATE.result = item;                       // the ONLY thing view 3 displays
  showView('result');
  var isLive = item.kind === 'live';
  var badge = $('result-badge');
  if (isLive) {
    badge.className = 'badge badge-live';
    badge.textContent = T('badge_live', {
      m: item.model || T('badge_live_nomodel'),
      t: item.finished ? ' · ' + item.finished : ''
    });
  } else {
    badge.className = 'badge badge-archive';
    badge.textContent = T('badge_archive', { k: item.sample });
  }

  // preview + tabs
  var tabs = $('view-tabs');
  tabs.innerHTML = '';
  var first = null;
  VIEW_TABS.forEach(function (t) {
    if (!item.previews[t[0]]) { return; }
    if (!first) { first = t; }
    var b = document.createElement('button');
    b.className = 'view-tab';
    b.textContent = T(t[1]);
    b.setAttribute('data-src', item.previews[t[0]]);
    b.onclick = function () {
      $('main-preview').src = this.getAttribute('data-src');
      tabs.querySelectorAll('.view-tab').forEach(function (x) { x.className = 'view-tab'; });
      this.className = 'view-tab active';
    };
    tabs.appendChild(b);
  });
  if (first) {
    $('main-preview').src = item.previews[first[0]];
    tabs.querySelector('.view-tab').className = 'view-tab active';
  } else {
    $('main-preview').removeAttribute('src');
  }

  // actions
  var play = $('btn-play');
  if (item.clip) {
    play.disabled = false;
    play.onclick = function () {
      var box = $('clip-box');
      box.className = 'clip-box';
      var v = $('clip-video');
      v.src = item.clip;
      $('clip-cap').textContent = T('clip_cap', { d: item.clip_frames
        ? T('clip_dims', { f: item.clip_frames, s: item.clip_seconds }) : '' });
      v.scrollIntoView({ behavior: 'smooth', block: 'center' });
      v.play().catch(function () {});
    };
  } else {
    play.disabled = true;
    play.title = T('play_disabled');
  }
  var open = $('btn-open');
  open.disabled = false;
  open.textContent = T('btn_open');
  open.onclick = function () {
    var payload = isLive ? { task_id: item.task_id } : { sample: item.sample };
    open.disabled = true;
    api('/api/open', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                       body: JSON.stringify(payload) }).then(function () {
      open.disabled = false;
      open.textContent = T('btn_opened');
    }).catch(function (e) {
      open.disabled = false;
      $('clip-cap').textContent = T('open_fail', { m: e.message });
      $('clip-box').className = 'clip-box';
    });
  };

  // side facts
  var room = item.room || {};
  $('res-title').textContent = roomTitle(room);
  $('res-sub').textContent = T('res_sceneid', { s: item.scene_id || '—' })
    + (item.preview_at ? T('res_rendered', { t: item.preview_at }) : '');
  var e2e = item.e2e || {};
  var e2eText = e2e.checks
    ? (e2e.failures === 0 ? T('e2e_pass', { n: e2e.checks })
                          : T('e2e_fail', { n: e2e.failures }))
    : T('e2e_none');
  $('res-facts').innerHTML =
    '<dt>' + T('fact_size') + '</dt><dd>' + esc(room.width_cm) + ' × ' + esc(room.length_cm)
      + ' × ' + esc(room.height_cm) + ' cm</dd>' +
    '<dt>' + T('fact_doors') + '</dt><dd><span class="dir">' + esc(wallT((item.entrance || {}).wall))
      + '</span> / <span class="dir">' + esc(wallT((item.exit || {}).wall)) + '</span></dd>' +
    '<dt>' + T('fact_goal') + '</dt><dd>' + T('fact_goal_txt') + '</dd>' +
    '<dt>' + T('fact_e2e') + '</dt><dd>' + esc(e2eText) + '</dd>';

  var objs = $('res-objects');
  var rows = (item.objects || []).map(function (o) {
    var isDesk = o.asset_id && o.asset_id !== 'placeholder' && o.asset_id !== 'key_placeholder';
    var tag = isDesk ? T('obj_asset_tag') : '';
    return '<div class="obj-row"><span><span class="o-name">' + esc(typeT(o)) + '</span>'
      + (tag ? ' <span class="o-tag">' + tag + '</span>' : '')
      + '</span><span class="o-type">' + esc(sizeTxt(o)) + '</span></div>';
  });
  objs.innerHTML = '<div class="ol-title">' + T('obj_title') + '</div>' + rows.join('');

  renderResultTech(item);
}

function roomTitle(room) {
  var w = parseFloat(room.width_cm), l = parseFloat(room.length_cm);
  if (!w || !l) { return T('room_gen'); }
  var shape = Math.abs(w - l) / Math.max(w, l) < 0.12 ? T('shape_square')
    : (w > l ? T('shape_wide') : T('shape_deep'));
  return shape + ' · ' + Math.round(w / 100) + 'm × ' + Math.round(l / 100) + 'm';
}

function typeT(o) {
  var st = (o.semantic_type || '').toLowerCase();
  var key = st === 'desk' || st === 'table' ? 'type_desk'
    : st === 'crate' || st === 'box' ? 'type_crate'
    : st === 'chest' ? 'type_chest' : null;
  return key ? T(key) : (o.semantic_type || o.id || T('type_prop'));
}
function sizeTxt(o) {
  if (o.target_size_cm) { return o.target_size_cm.join('×') + ' cm'; }
  if (o.placement && o.placement.on_top_of) { return T('obj_on', { t: o.placement.on_top_of }); }
  return '';
}

$('btn-new').onclick = function () { showView('input'); };

/* -------------------------------------------------- technical details */
function bindTechToggle(id) {
  var el = $(id);
  el.querySelector('.tech-head').onclick = function () { el.className = el.className.indexOf('open') >= 0 ? 'tech' : 'tech open'; };
}
bindTechToggle('run-tech');
bindTechToggle('result-tech');

function specLink(item) {
  return item.kind === 'archive'
    ? '/media/' + item.archive_rel + '/scene_spec.json'
    : '/api/task/' + item.task_id + '/file?path=scene_spec.json';
}
function planLink(item) {
  return item.kind === 'archive'
    ? '/media/' + item.archive_rel + '/plan.json'
    : '/api/task/' + item.task_id + '/file?path=plan.json';
}
function e2eLink(item) {
  return item.kind === 'archive'
    ? '/media/' + item.archive_rel + '/e2e.log'
    : '/api/task/' + item.task_id + '/file?path=e2e.log';
}

function renderResultTech(item) {
  var box = $('result-tech-body');
  var H = [];
  var e2e = item.e2e || {};

  if (item.kind === 'live') {
    H.push('<div class="tsec"><h4>' + T('tech_run') + '</h4><div class="tkv">'
      + '<span class="k">' + T('tech_model') + '</span><span class="v">' + esc(item.model || '—') + '</span>'
      + '<span class="k">' + T('tech_finished') + '</span><span class="v">' + esc(item.finished || '—') + '</span>'
      + '<span class="k">' + T('tech_taskdir') + '</span><span class="v">Data/Demo/tasks/' + esc(item.task_id) + '</span>'
      + '</div></div>');
    if (item.rounds && item.rounds.length) {
      H.push('<div class="tsec"><h4>' + T('tech_rounds') + '</h4>');
      item.rounds.forEach(function (r) {
        H.push('<div class="round-item ' + (r.ok ? 'ok' : 'bad') + '"><div class="rr">' + T('round_n', { n: r.round })
          + (r.ok ? T('round_ok') : T('round_bad')) + '</div>');
        (r.errors || []).forEach(function (e) {
          var raw = typeof e === 'string' ? e : (e.raw || '');
          var hint = typeof e === 'string' ? '' : (e.hint || '');
          if (raw) { H.push('<div class="rerr">' + esc(raw) + '</div>'); }
          if (hint) { H.push('<div class="rhint">→ ' + esc(hint) + '</div>'); }
        });
        H.push('</div>');
      });
      H.push('</div>');
    }
  }

  H.push('<div class="tsec"><h4>' + T('tech_data') + '</h4><div class="tkv">'
    + '<span class="k">' + T('tech_e2e') + '</span><span class="v">'
    + (e2e.checks
        ? '<span class="e2e-line ' + (e2e.failures === 0 ? 'pass' : 'fail') + '">'
          + T('e2e_line', { c: e2e.checks, f: e2e.failures,
                            v: e2e.verdict || (e2e.failures === 0 ? 'ALL PASS' : 'FAIL') }) + '</span>'
          + (e2e.outside_dist ? ' · outside_dist=' + esc(e2e.outside_dist) + ' cm' : '')
        : T('tech_noe2e'))
    + '</span>'
    + '<span class="k">SceneSpec</span><span class="v"><span class="tlink" data-url="' + esc(specLink(item)) + '" data-title="' + T('title_scenespec') + '">' + T('tech_view_json') + '</span></span>'
    + '<span class="k">' + T('tech_plan') + '</span><span class="v"><span class="tlink" data-url="' + esc(planLink(item)) + '" data-title="' + T('title_plan') + '">' + T('tech_view_json') + '</span></span>'
    + '<span class="k">E2E</span><span class="v"><span class="tlink" data-url="' + esc(e2eLink(item)) + '" data-title="' + T('tech_e2e_log') + '">' + T('tech_view_log') + '</span></span>'
    + '</div></div>');

  var a = item.assets;
  if (a) {
    H.push('<div class="tsec"><h4>' + T('tech_assets') + '</h4><div class="tkv">');
    if (a.desk && a.desk.asset_id) {
      H.push('<span class="k">' + T('tech_desk', { a: a.desk.asset_id }) + '</span><span class="v">'
        + esc(a.desk.source || '') + (a.desk.license ? T('tech_license', { l: a.desk.license }) : '')
        + (a.desk.input_image_origin ? T('tech_input_img', { o: a.desk.input_image_origin }) : '') + '</span>');
    }
    if (a.key_prop) {
      H.push('<span class="k">' + T('tech_key') + '</span><span class="v">' + esc(a.key_prop.source) + '（' + esc(a.key_prop.license) + '）</span>');
    }
    if (a.textures) {
      H.push('<span class="k">' + T('tech_tex') + '</span><span class="v">'
        + esc(a.textures.note || a.textures.source || T('tech_tex_note')) + '</span>');
    }
    H.push('</div></div>');
  }

  box.innerHTML = H.join('');
  box.querySelectorAll('[data-url]').forEach(function (l) {
    l.onclick = function () { openModal(l.getAttribute('data-title'), l.getAttribute('data-url')); };
  });
}

/* ----------------------------------------------------------------- modal */
function openModal(title, url) {
  $('modal-title').textContent = title;
  $('modal-body').textContent = T('modal_loading');
  $('modal').className = 'modal';
  fetch(url).then(function (r) { return r.text(); }).then(function (t) {
    $('modal-body').textContent = t.length > 200000 ? t.slice(0, 200000) + T('tech_trunc') : t;
  }).catch(function (e) { $('modal-body').textContent = T('modal_read_fail', { m: e.message }); });
}
$('modal-close').onclick = function () { $('modal').className = 'modal hidden'; };
$('modal').onclick = function (ev) { if (ev.target === $('modal')) { $('modal').className = 'modal hidden'; } };

/* ------------------------------------------------------------------ init */
initLang();

api('/api/samples').then(function (list) {
  // thumbnails come from the per-sample archive payload (overview still)
  var missing = list.filter(function (s) { return !s.thumb; }).map(function (s) {
    return api('/api/archive/' + s.key).then(function (a) {
      s.thumb = (a.previews && a.previews.overview) || '';
      s.title = s.title || null;
    }).catch(function () {});
  });
  return Promise.all(missing).then(function () { return list; });
}).then(fillSamples).catch(function () {});

api('/api/state').then(function (s) {
  if (!s.api_key) {
    setStatus('status_nokey', 'chip-bad');
  }
  if (s.busy && s.current) {
    STATE.taskId = s.current.id;
    STATE.busy = true;
    enterRunView();
    poll();
  }
}).catch(function () {});
