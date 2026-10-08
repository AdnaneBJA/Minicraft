// Keeps the dashboard live: the page is rendered on the server with real numbers, and this refreshes them every
// 10 seconds from the JSON API (and draws the activity chart).
(function () {
  const REFRESH_MS = 10000;

  const number = (n) => n.toLocaleString('en-US');
  const duration = (s) =>
    s >= 3600 ? `${Math.floor(s / 3600)}h ${String(Math.floor(s / 60) % 60).padStart(2, '0')}m`
      : s >= 60 ? `${Math.floor(s / 60)}m ${String(s % 60).padStart(2, '0')}s`
        : `${s}s`;
  const depth = (d) => (d >= 4 ? 'The sky' : d >= 1 ? `Cave ${d}` : 'Surface');
  const formats = { num: number, duration, depth };
  const ago = (iso) => {
    const minutes = Math.floor((Date.now() - new Date(iso).getTime()) / 60000);
    if (minutes < 1) return 'just now';
    if (minutes < 60) return `${minutes} min ago`;
    if (minutes < 1440) return `${Math.floor(minutes / 60)} h ago`;
    return `${Math.floor(minutes / 1440)} d ago`;
  };
  const getJSON = (path) => fetch(`/stats/api/${path}`, { cache: 'no-store' }).then((r) => {
    if (!r.ok) throw new Error(`${path}: ${r.status}`);
    return r.json();
  });
  const el = (tag, attrs = {}, text) => {
    const node = document.createElement(tag);
    Object.assign(node, attrs);
    if (text !== undefined) node.textContent = text;
    return node;
  };

  function showSummary(summary) {
    document.querySelectorAll('[data-stat]').forEach((node) => {
      const value = summary[node.dataset.stat];
      if (value === undefined) return;
      node.textContent = node.dataset.format === 'duration' ? duration(value) : number(value);
    });
    const pill = document.getElementById('online-pill');
    document.getElementById('online-text').textContent =
      summary.online > 0 ? `${summary.online} playing now` : 'nobody online';
    pill.classList.toggle('live', summary.online > 0);
  }

  function showLeaderboards(boards) {
    document.querySelectorAll('[data-board]').forEach((list) => {
      const leaders = boards[list.dataset.board] || [];
      const format = formats[list.dataset.format] || number;
      list.replaceChildren(...(leaders.length ? leaders.map((leader, i) => {
        const item = el('li');
        item.append(el('span', { className: 'rank' }, String(i + 1)),
          el('a', { href: `/stats/player/${encodeURIComponent(leader.name)}` }, leader.name),
          el('span', { className: 'score' }, format(leader.value)));
        return item;
      }) : [el('li', { className: 'empty' }, 'Nobody yet')]));
    });
  }

  function showRecent(recent) {
    const feed = document.getElementById('feed');
    if (!feed) return;
    feed.replaceChildren(...(recent.length ? recent.map((event) => {
      const item = el('li');
      item.append(el('span', {}, event.text), el('time', {}, ago(event.at)));
      return item;
    }) : [el('li', { className: 'empty' }, 'Quiet so far.')]));
  }

  let chart = null;
  function showTimeline(points) {
    const canvas = document.getElementById('timeline');
    if (!canvas || typeof Chart === 'undefined') return;
    // Every hour of the last 7 days, including the quiet ones.
    const hours = [];
    const start = new Date(Date.now() - 7 * 24 * 3600 * 1000);
    start.setMinutes(0, 0, 0);
    for (let t = start.getTime(); t <= Date.now(); t += 3600 * 1000) hours.push(t);
    const byHour = new Map(points.map((p) => [new Date(p.hour).getTime(), p]));
    const labels = hours.map((t) => new Date(t).toLocaleString('en-US', { weekday: 'short', hour: 'numeric' }));
    const joins = hours.map((t) => (byHour.get(t) || {}).joins || 0);
    const kills = hours.map((t) => (byHour.get(t) || {}).kills || 0);
    if (chart) {
      chart.data.labels = labels;
      chart.data.datasets[0].data = joins;
      chart.data.datasets[1].data = kills;
      chart.update('none');
      return;
    }
    chart = new Chart(canvas, {
      type: 'line',
      data: {
        labels,
        datasets: [
          { label: 'Players joining', data: joins, borderColor: '#6ac46a', backgroundColor: 'rgba(106,196,106,0.15)', fill: true, tension: 0.3, pointRadius: 0 },
          { label: 'Creatures killed', data: kills, borderColor: '#e0605a', backgroundColor: 'rgba(224,96,90,0.12)', fill: true, tension: 0.3, pointRadius: 0 },
        ],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'index', intersect: false },
        scales: {
          x: { ticks: { color: '#9aa0aa', maxTicksLimit: 8 }, grid: { color: '#2a2f3a' } },
          y: { beginAtZero: true, ticks: { color: '#9aa0aa', precision: 0 }, grid: { color: '#2a2f3a' } },
        },
        plugins: { legend: { labels: { color: '#e8e6e3' } } },
      },
    });
  }

  function refresh() {
    getJSON('summary').then(showSummary).catch(() => {});
    if (document.querySelector('[data-board]')) getJSON('leaderboards').then(showLeaderboards).catch(() => {});
    if (document.getElementById('feed')) getJSON('recent').then(showRecent).catch(() => {});
    if (document.getElementById('timeline')) getJSON('timeline').then(showTimeline).catch(() => {});
  }

  refresh();
  setInterval(refresh, REFRESH_MS);
})();
