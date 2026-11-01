// FRONTEND: talks to the backend through the REST API
const $ = id => document.getElementById(id);
const api = async (url, opts = {}) => {
  const r = await fetch(url, { headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) }, ...opts });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || 'Request failed');
  return data;
};
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
function toast(msg) { const t = $('toast'); t.textContent = msg; t.classList.add('show'); setTimeout(() => t.classList.remove('show'), 2500); }

async function loadStops() {
  const stops = await api('/api/stops');
  const opts = stops.map(s => `<option value="${s.id}">${esc(s.name)}</option>`).join('');
  ['from', 'to', 'rf', 'rto'].forEach(id => { const v = $(id).value; $(id).innerHTML = opts; if (v) $(id).value = v; });
  if (!$('to').dataset.init) { $('to').selectedIndex = Math.min(1, stops.length - 1); $('to').dataset.init = 1; }
}
async function loadRoutes() {
  $('rt').innerHTML = (await api('/api/routes')).map(r =>
    `<tr><td>${esc(r.bus)}</td><td>${esc(r.from)}</td><td>${esc(r.to)}</td><td>${r.dist}</td><td>${r.time}</td><td>₹${r.fare}</td></tr>`).join('');
}
async function loadBookings() {
  const rows = await api('/api/bookings');
  $('bk').innerHTML = rows.length ? rows.map(b =>
    `<tr><td>${b.id}</td><td>${esc(b.passenger)}</td><td>${esc(b.from)}</td><td>${esc(b.to)}</td><td>${b.dist}</td><td>₹${b.fare}</td><td>${esc(b.created)}</td>
     <td><button class="danger" data-id="${b.id}">Cancel</button></td></tr>`).join('') : '<tr><td colspan="8">No tickets yet.</td></tr>';
}

$('go').onclick = async () => {
  const q = new URLSearchParams({ from: $('from').value, to: $('to').value, by: $('by').value });
  try {
    const r = await api('/api/plan?' + q);
    $('result').innerHTML = `
      <div class="stats"><div class="stat"><b>${r.dist} km</b>Distance</div><div class="stat"><b>${Math.floor(r.time / 60)}h ${r.time % 60}m</b>Travel time</div><div class="stat"><b>₹${r.fare}</b>Total fare</div></div>
      <ul class="timeline"><li><b>${esc(r.steps[0].from)}</b></li>${r.steps.map(s =>
        `<li><b>${esc(s.to)}</b><small>Bus ${esc(s.bus)} · ${s.dist} km · ${s.time} min · ₹${s.fare}</small></li>`).join('')}</ul>
      <div class="book"><input id="who" placeholder="Passenger name"><button id="bookBtn" class="primary">Book ticket</button></div>`;
    $('bookBtn').onclick = async () => {
      try {
        const b = await api('/api/bookings', { method: 'POST', body: JSON.stringify({ passenger: $('who').value, from: $('from').value, to: $('to').value, by: $('by').value }) });
        toast('Ticket #' + b.id + ' booked!'); loadBookings();
      } catch (e) { toast(e.message); }
    };
  } catch (e) { $('result').innerHTML = `<p class="err">${esc(e.message)}</p>`; }
};
$('bk').onclick = async e => {
  const id = e.target.dataset.id; if (!id) return;
  try { await api('/api/bookings/' + id, { method: 'DELETE' }); toast('Ticket cancelled'); loadBookings(); } catch (err) { toast(err.message); }
};
const admin = async (url, data, done) => {
  try { await api(url, { method: 'POST', headers: { 'x-admin-key': $('key').value }, body: JSON.stringify(data) }); toast('Saved'); done(); }
  catch (e) { toast(e.message); }
};
$('addStop').onclick = () => admin('/api/stops', { name: $('sname').value }, () => { $('sname').value = ''; loadStops(); });
$('addRoute').onclick = () => admin('/api/routes', { from: $('rf').value, to: $('rto').value, dist: $('rd').value, time: $('rm').value, fare: $('rfare').value, bus: $('rbus').value }, loadRoutes);

loadStops().then(() => { loadRoutes(); loadBookings(); });
