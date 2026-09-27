/* WorldGen portfolio demo - trilingual UI layer (zh / en / ja).
   Dictionary-driven: static HTML nodes carry data-i18n keys, JS strings go
   through T(). Dynamic sentences produced by the SERVER (stage details,
   validator feedback) stay in the pipeline language and are not translated
   here - the UI chrome, labels, actions and statuses are. */
'use strict';

var I18N = {
  zh: {
    brand_sub: 'AI 生成可玩 3D 场景 · 本机 Demo',
    status_idle: '空闲', status_running: '生成中…', status_queued: '排队中',
    status_done: '完成', status_failed: '失败', status_archive: '查看归档',
    status_poll_fail: '状态查询失败', status_submitting: '提交任务…',
    status_nokey: '未配置 API key（仅可查看归档）',
    queue_n: '排队中 · 前面还有 {n} 个任务',

    hero_title: '描述你想要的房间',
    hero_sub: '一句话 → AI 规划布局并自动校验 → 在虚幻引擎里建关 → 拿到预览和可玩关卡',
    desc_ph: '例如：一个 6 米长 4 米宽、层高 2.8 米的库房，门开在南墙中央，北墙有一扇上锁的门；西侧摆一张木质工作桌，桌上放着钥匙，桌边两个木箱……',
    btn_generate: '生成场景',
    ex_label: '没有想法？试试示例：',
    ex_a: '储藏室寻钥匙', ex_b: '小型展示厅', ex_c: '对称方厅',
    ex_note: '示例是<b>已经生成并验证过</b>的场景：<a id="lnk-archives">直接查看已生成场景</a>；点「生成场景」才会真实调用 DeepSeek 重新生成。',
    archives_show: '直接查看已生成场景', archives_hide: '收起已生成场景',
    arch_tag: '已生成 · 归档样例 {k}', arch_verified: '已验证可玩 · {n} 项检查通过', arch_plain: '已归档',
    err_busy: '已有任务在执行，请等它完成或失败后再试。',
    err_short: '描述有点短——说说房间大小、门在哪、放什么家具吧。',
    err_submit: '提交失败：{m}', err_archive: '归档读取失败：{m}',

    run_title: '正在生成你的场景',
    run_sub: '全程约 10–15 分钟：模型规划、自动校验、建关、渲染、试玩验收',
    fail_title: '这次生成没有成功',
    fail_sub: '没有产生新场景；之前的预览不会被替换。',
    fail_stage: '卡在「{s}」阶段',
    fail_more: '详情可在下方「技术详情」展开查看。',
    btn_retry: '重试', btn_back: '返回修改描述',
    tech_head_run: '技术详情（原始日志 / 校验报文）',
    tech_head_result: '技术详情（模型 / SceneSpec / 校验 / 资产来源 / E2E）',
    tech_pending: '任务开始后，这里会有各阶段的原始输出。',
    tech_raw: '{s} · 原始输出（最后 {n} 行）',
    tech_log: '打开完整日志：{f}', tech_none: '暂无输出。',
    tech_modal_log: '任务日志：{f}', tech_trunc: '\n…（已截断）',
    modal_loading: '加载中…', modal_read_fail: '读取失败：{m}', modal_close: '关闭',

    st_model: '理解描述', st_validate: '检查布局', st_plan: '规划搭建',
    st_build: '构建场景', st_assets: '核对资产', st_preview: '生成预览', st_e2e: '验证可玩',

    tab_overview: '全景', tab_desk_key: '木桌与钥匙', tab_key: '钥匙特写', tab_exit: '出口',
    btn_open: '打开可玩关卡', btn_opened: '已启动 UE 窗口',
    btn_play: '播放展示镜头', btn_new: '生成新场景',
    open_fail: '打开失败：{m}', play_disabled: '该场景没有展示镜头',
    clip_cap: '展示镜头：编辑器相机路径逐帧渲染{d}，不是游玩录像；真实交互由 E2E 自动试玩证明。',
    clip_dims: '（{f} 帧 / {s} 秒）',
    badge_live: '本次生成 · 模型 {m}{t}', badge_live_nomodel: '（运行记录缺失）',
    badge_archive: '已生成场景（归档样例 {k}）· 本次未调用模型',
    res_scene: '场景', res_sceneid: 'scene_id：{s}', res_rendered: ' · 预览渲染于 {t}',
    fact_size: '房间尺寸', fact_doors: '入口 / 出口', fact_goal: '目标',
    fact_goal_txt: '拾取钥匙 → 开门离开', fact_e2e: '可玩验收',
    e2e_pass: '✓ 已验证可玩（{n} 项检查全过）', e2e_fail: '✗ 验收未过（{n} 项失败）',
    e2e_none: '未归档验收结果',
    play_hint: '<b>玩法</b>：W/A/S/D 移动，走近钥匙按 <b>E</b> 拾取，再走到出口门前按 <b>E</b> 开门，穿门而出即完成。',
    obj_title: '房间里的物件', obj_asset_tag: '3D 资产', obj_on: '置于 {t}',
    room_gen: '生成场景', shape_square: '方厅', shape_wide: '横厅', shape_deep: '纵厅',
    wall_north: '北墙', wall_south: '南墙', wall_east: '东墙', wall_west: '西墙',
    short_in: '入口', short_out: '出口',
    type_desk: '木桌', type_crate: '木箱', type_chest: '箱子', type_prop: '物件',
    tech_run: '本次运行', tech_model: '模型（运行记录）', tech_finished: '完成时间',
    tech_taskdir: '任务目录', tech_rounds: '模型轮次（校验不合格时把原因反馈给模型重试）',
    round_n: '第 {n} 轮：', round_ok: '通过', round_bad: '被退回',
    tech_data: '验收与数据', tech_e2e: 'E2E 自动试玩',
    e2e_line: '{c} 项断言 / {f} 项失败 → {v}',
    tech_noe2e: '无归档结果', tech_view_json: '查看 JSON', tech_view_log: '查看日志',
    tech_plan: 'plan（搭建清单）', tech_e2e_log: 'E2E 游玩验收日志',
    tech_assets: '资产来源', tech_desk: '木桌 {a}', tech_license: ' · 许可：{l}',
    tech_input_img: ' · 输入图：{o}', tech_key: '钥匙模型',
    tech_tex: '材质贴图', tech_tex_note: '程序化生成（项目自有，无第三方素材）',
    title_scenespec: 'SceneSpec JSON', title_plan: 'plan JSON',
    m_unk: '未知原因'
  },

  en: {
    brand_sub: 'AI-generated playable 3D scenes · local demo',
    status_idle: 'Idle', status_running: 'Generating…', status_queued: 'Queued',
    status_done: 'Done', status_failed: 'Failed', status_archive: 'Viewing archive',
    status_poll_fail: 'Status check failed', status_submitting: 'Submitting…',
    status_nokey: 'No API key configured (archive view only)',
    queue_n: 'Queued · {n} task(s) ahead',

    hero_title: 'Describe the room you want',
    hero_sub: 'One sentence → AI plans and validates the layout → builds the level in Unreal Engine → you get previews and a playable level',
    desc_ph: 'E.g. a 6 m × 4 m storage room, 2.8 m ceiling, door in the middle of the south wall, a locked door on the north wall; a wooden work desk on the west side with a key on it, two crates nearby…',
    btn_generate: 'Generate Scene',
    ex_label: 'Need an idea? Try a sample:',
    ex_a: 'Storage key hunt', ex_b: 'Small showroom', ex_c: 'Symmetric hall',
    ex_note: 'Samples are <b>already generated and verified</b> scenes: <a id="lnk-archives">browse generated scenes</a>. "Generate Scene" really calls DeepSeek to build a new one.',
    archives_show: 'Browse generated scenes', archives_hide: 'Hide generated scenes',
    arch_tag: 'Generated · archive sample {k}', arch_verified: 'Verified playable · {n} checks passed', arch_plain: 'Archived',
    err_busy: 'A task is already running - wait for it to finish or fail first.',
    err_short: 'Description is a bit short - mention the room size, where the doors are and what furniture to place.',
    err_submit: 'Submit failed: {m}', err_archive: 'Failed to read archive: {m}',

    run_title: 'Generating your scene',
    run_sub: 'Takes about 10–15 minutes: model planning, automatic validation, level build, rendering, play-through acceptance',
    fail_title: 'This generation did not succeed',
    fail_sub: 'No new scene was produced; previous previews are left untouched.',
    fail_stage: 'Stuck at the "{s}" stage',
    fail_more: 'Details can be expanded under "Tech details" below.',
    btn_retry: 'Retry', btn_back: 'Back to edit description',
    tech_head_run: 'Tech details (raw logs / validation messages)',
    tech_head_result: 'Tech details (model / SceneSpec / validation / assets / E2E)',
    tech_pending: 'Raw per-stage output will appear here once the task starts.',
    tech_raw: '{s} · raw output (last {n} lines)',
    tech_log: 'Open full log: {f}', tech_none: 'No output yet.',
    tech_modal_log: 'Task log: {f}', tech_trunc: '\n…(truncated)',
    modal_loading: 'Loading…', modal_read_fail: 'Failed to read: {m}', modal_close: 'Close',

    st_model: 'Understand', st_validate: 'Check layout', st_plan: 'Plan build',
    st_build: 'Build scene', st_assets: 'Match assets', st_preview: 'Render previews', st_e2e: 'Verify playable',

    tab_overview: 'Overview', tab_desk_key: 'Desk & key', tab_key: 'Key close-up', tab_exit: 'Exit',
    btn_open: 'Open playable level', btn_opened: 'UE window launched',
    btn_play: 'Play showcase clip', btn_new: 'New scene',
    open_fail: 'Open failed: {m}', play_disabled: 'No showcase clip for this scene',
    clip_cap: 'Showcase clip: editor camera path rendered frame by frame{d} - not gameplay footage; real interaction is proven by the automated E2E play-through.',
    clip_dims: ' ({f} frames / {s} s)',
    badge_live: 'Generated now · model {m}{t}', badge_live_nomodel: '(run record missing)',
    badge_archive: 'Generated scene (archive sample {k}) · no model call this time',
    res_scene: 'Scene', res_sceneid: 'scene_id: {s}', res_rendered: ' · preview rendered {t}',
    fact_size: 'Room size', fact_doors: 'Entrance / Exit', fact_goal: 'Goal',
    fact_goal_txt: 'Pick up the key → unlock the door → leave',
    fact_e2e: 'Play acceptance',
    e2e_pass: '✓ Verified playable ({n} checks passed)', e2e_fail: '✗ Acceptance failed ({n} failures)',
    e2e_none: 'No archived acceptance result',
    play_hint: '<b>How to play</b>: move with W/A/S/D, walk to the key and press <b>E</b> to pick it up, then press <b>E</b> at the exit door to unlock, and walk through to finish.',
    obj_title: 'Objects in the room', obj_asset_tag: '3D asset', obj_on: 'on {t}',
    room_gen: 'Generated scene', shape_square: 'Square hall', shape_wide: 'Wide hall', shape_deep: 'Deep hall',
    wall_north: 'North wall', wall_south: 'South wall', wall_east: 'East wall', wall_west: 'West wall',
    short_in: ' entrance', short_out: ' exit',
    type_desk: 'Wooden desk', type_crate: 'Crate', type_chest: 'Chest', type_prop: 'Prop',
    tech_run: 'This run', tech_model: 'Model (run record)', tech_finished: 'Finished at',
    tech_taskdir: 'Task directory', tech_rounds: 'Model rounds (invalid specs are fed back with reasons)',
    round_n: 'Round {n}: ', round_ok: 'accepted', round_bad: 'sent back',
    tech_data: 'Acceptance & data', tech_e2e: 'Automated E2E play-through',
    e2e_line: '{c} assertions / {f} failures → {v}',
    tech_noe2e: 'No archived result', tech_view_json: 'View JSON', tech_view_log: 'View log',
    tech_plan: 'plan (build list)', tech_e2e_log: 'E2E play acceptance log',
    tech_assets: 'Asset provenance', tech_desk: 'Desk {a}', tech_license: ' · license: {l}',
    tech_input_img: ' · input image: {o}', tech_key: 'Key prop',
    tech_tex: 'Materials', tech_tex_note: 'Procedurally generated (project-owned, no third-party assets)',
    title_scenespec: 'SceneSpec JSON', title_plan: 'plan JSON',
    m_unk: 'Unknown reason'
  },

  ja: {
    brand_sub: 'AIが生成する遊べる3Dシーン・ローカルDemo',
    status_idle: '待機中', status_running: '生成中…', status_queued: '順番待ち',
    status_done: '完了', status_failed: '失敗', status_archive: 'アーカイブ表示中',
    status_poll_fail: '状態取得に失敗', status_submitting: '送信中…',
    status_nokey: 'APIキー未設定（アーカイブ閲覧のみ）',
    queue_n: '順番待ち · 前に{n}件',

    hero_title: '欲しい部屋を一言で',
    hero_sub: '一言 → AIがレイアウトを設計・自動検証 → Unreal Engineでレベル構築 → プレビューと遊べるレベルを取得',
    desc_ph: '例：6m×4m・天井2.8mの倉庫。ドアは南壁の中央、北壁に施錠されたドア。西側に木の作業台を置き、台上に鍵、近くに木箱を2つ…',
    btn_generate: 'シーンを生成',
    ex_label: 'アイデアが浮かばない？サンプル：',
    ex_a: '倉庫の鍵探し', ex_b: '小型ショールーム', ex_c: '対称ホール',
    ex_note: 'サンプルは<b>生成・検証済み</b>のシーン：<a id="lnk-archives">生成済みシーンを見る</a>。「シーンを生成」は実際にDeepSeekを呼び出します。',
    archives_show: '生成済みシーンを見る', archives_hide: '生成済みシーンを閉じる',
    arch_tag: '生成済み · アーカイブ {k}', arch_verified: '遊玩検証済み · {n}項合格', arch_plain: 'アーカイブ済み',
    err_busy: '別のタスクが実行中です。終了または失敗をお待ちください。',
    err_short: '説明が短すぎます。部屋のサイズ・ドアの位置・家具を書いてみてください。',
    err_submit: '送信失敗：{m}', err_archive: 'アーカイブ読み込み失敗：{m}',

    run_title: 'シーンを生成中',
    run_sub: '所要は約10〜15分：モデル設計・自動検証・レベル構築・レンダリング・遊玩検収',
    fail_title: 'この生成は成功しませんでした',
    fail_sub: '新しいシーンは作成されませんでした。以前のプレビューはそのままです。',
    fail_stage: '「{s}」ステージで停止',
    fail_more: '詳細は下の「技術詳細」で確認できます。',
    btn_retry: '再試行', btn_back: '説明に戻る',
    tech_head_run: '技術詳細（生ログ / 検証メッセージ）',
    tech_head_result: '技術詳細（モデル / SceneSpec / 検証 / アセット / E2E）',
    tech_pending: 'タスク開始後、ここに各ステージの生ログが表示されます。',
    tech_raw: '{s} · 生出力（末尾{n}行）',
    tech_log: '完全なログを開く：{f}', tech_none: '出力はまだありません。',
    tech_modal_log: 'タスクログ：{f}', tech_trunc: '\n…（切り捨て）',
    modal_loading: '読み込み中…', modal_read_fail: '読み込み失敗：{m}', modal_close: '閉じる',

    st_model: '説明を理解', st_validate: 'レイアウト検査', st_plan: '構築計画',
    st_build: 'シーン構築', st_assets: 'アセット照合', st_preview: 'プレビュー生成', st_e2e: '遊玩検証',

    tab_overview: '全景', tab_desk_key: '机と鍵', tab_key: '鍵接写', tab_exit: '出口',
    btn_open: '遊べるレベルを開く', btn_opened: 'UEウィンドウを起動しました',
    btn_play: 'ショーケース再生', btn_new: '新しいシーン',
    open_fail: '起動失敗：{m}', play_disabled: 'このシーンにはショーケースがありません',
    clip_cap: 'ショーケース：エディタカメラ経路を逐帧レンダリング{d}。遊玩映像ではなく、実際の操作はE2E自動プレイで証明されます。',
    clip_dims: '（{f}フレーム / {s}秒）',
    badge_live: '今回生成 · モデル {m}{t}', badge_live_nomodel: '（実行記録なし）',
    badge_archive: '生成済みシーン（アーカイブ {k}）· 今回はモデル不使用',
    res_scene: 'シーン', res_sceneid: 'scene_id：{s}', res_rendered: ' · プレビュー生成 {t}',
    fact_size: '部屋サイズ', fact_doors: '入口 / 出口', fact_goal: '目標',
    fact_goal_txt: '鍵を取る → ドアを開けて退出',
    fact_e2e: '遊玩検収',
    e2e_pass: '✓ 遊玩検証済み（{n}項すべて合格）', e2e_fail: '✗ 検収不合格（{n}項失敗）',
    e2e_none: '検収結果の記録なし',
    play_hint: '<b>遊び方</b>：W/A/S/Dで移動、鍵に近づいて<b>E</b>で拾う。出口のドアで<b>E</b>を押して解錠し、くぐり抜けたらクリア。',
    obj_title: '部屋のオブジェクト', obj_asset_tag: '3Dアセット', obj_on: '{t}の上',
    room_gen: '生成シーン', shape_square: '正方形ホール', shape_wide: '横長ホール', shape_deep: '縦長ホール',
    wall_north: '北壁', wall_south: '南壁', wall_east: '東壁', wall_west: '西壁',
    short_in: '入口', short_out: '出口',
    type_desk: '木の机', type_crate: '木箱', type_chest: '箱', type_prop: 'オブジェ',
    tech_run: '今回の実行', tech_model: 'モデル（実行記録）', tech_finished: '完了時刻',
    tech_taskdir: 'タスクdir', tech_rounds: 'モデルの試行（不合格時は理由を返して再試行）',
    round_n: '第{n}ラウンド：', round_ok: '合格', round_bad: '差し戻し',
    tech_data: '検収とデータ', tech_e2e: 'E2E自動プレイ',
    e2e_line: '{c}項アサーション / {f}項失敗 → {v}',
    tech_noe2e: '記録なし', tech_view_json: 'JSON表示', tech_view_log: 'ログ表示',
    tech_plan: 'plan（構築リスト）', tech_e2e_log: 'E2E遊玩検収ログ',
    tech_assets: 'アセット出所', tech_desk: '机 {a}', tech_license: ' · ライセンス：{l}',
    tech_input_img: ' · 入力画像：{o}', tech_key: '鍵モデル',
    tech_tex: 'マテリアル', tech_tex_note: '手続き的生成（自作、第三者素材なし）',
    title_scenespec: 'SceneSpec JSON', title_plan: 'plan JSON',
    m_unk: '原因不明'
  }
};

var LANGS = [
  ['zh', '中'], ['en', 'EN'], ['ja', '日']
];

function i18nLang() { return STATE && STATE.lang ? STATE.lang : 'zh'; }

function T(key, vars) {
  var L = I18N[i18nLang()] || I18N.zh;
  var s = L[key] !== undefined ? L[key] : (I18N.zh[key] !== undefined ? I18N.zh[key] : key);
  if (vars) {
    Object.keys(vars).forEach(function (k) {
      s = s.split('{' + k + '}').join(String(vars[k]));
    });
  }
  return s;
}

/* Apply dictionary strings to all static [data-i18n] nodes.
   data-i18n-html: trusted dictionary HTML; data-i18n-ph: placeholder. */
function applyStaticI18n() {
  document.querySelectorAll('[data-i18n]').forEach(function (el) {
    el.textContent = T(el.getAttribute('data-i18n'));
  });
  document.querySelectorAll('[data-i18n-html]').forEach(function (el) {
    el.innerHTML = T(el.getAttribute('data-i18n-html'));
  });
  document.querySelectorAll('[data-i18n-ph]').forEach(function (el) {
    el.setAttribute('placeholder', T(el.getAttribute('data-i18n-ph')));
  });
  document.documentElement.setAttribute('lang', i18nLang() === 'zh' ? 'zh-CN' : i18nLang());
  document.title = i18nLang() === 'en' ? 'WorldGen · One sentence to a playable scene'
    : i18nLang() === 'ja' ? 'WorldGen · 一言から遊べるシーンを生成'
    : 'WorldGen · 描述一句话，生成可玩场景';
}

function setLang(lang) {
  if (!I18N[lang]) { return; }
  STATE.lang = lang;
  try { localStorage.setItem('wg_lang', lang); } catch (e) { /* private mode */ }
  document.querySelectorAll('.lang-btn').forEach(function (b) {
    b.className = 'lang-btn' + (b.getAttribute('data-lang') === lang ? ' active' : '');
  });
  applyStaticI18n();
  if (STATE.samples && STATE.samples.length) { fillSamples(STATE.samples); }
  renderStages(STATE.lastJob || null);          // re-render dynamic views
  renderRunTech(STATE.lastJob || null);
  if (STATE.result) { showResult(STATE.result); }
}

function initLang() {
  var saved = null;
  try { saved = localStorage.getItem('wg_lang'); } catch (e) { /* private mode */ }
  var nav = (navigator.language || 'zh').slice(0, 2).toLowerCase();
  STATE.lang = I18N[saved] ? saved : (I18N[nav] ? nav : 'zh');
  var bar = document.getElementById('lang-switch');
  if (bar) {
    LANGS.forEach(function (p) {
      var b = document.createElement('button');
      b.className = 'lang-btn' + (p[0] === STATE.lang ? ' active' : '');
      b.textContent = p[1];
      b.setAttribute('data-lang', p[0]);
      b.setAttribute('title', p[0]);
      b.onclick = function () { setLang(p[0]); };
      bar.appendChild(b);
    });
  }
  applyStaticI18n();
}
