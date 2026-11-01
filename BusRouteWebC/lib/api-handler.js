const { neon } = require("@neondatabase/serverless");

const initialStops = [
  "Panipat",
  "Karnal",
  "Sonipat",
  "Delhi ISBT",
  "Gurugram",
  "Rohtak",
  "Ambala",
  "Kurukshetra",
  "Jind",
  "Hisar",
];

const initialRoutes = [
  [0, 1, 30, 45, 40, "HR-101"],
  [0, 2, 50, 70, 60, "HR-102"],
  [2, 3, 45, 75, 55, "DL-201"],
  [0, 3, 90, 120, 110, "DL-202"],
  [3, 4, 35, 70, 45, "DL-203"],
  [2, 5, 45, 65, 50, "HR-103"],
  [5, 8, 60, 90, 70, "HR-104"],
  [1, 7, 40, 55, 45, "HR-105"],
  [7, 6, 50, 70, 55, "HR-106"],
  [1, 8, 55, 80, 65, "HR-107"],
  [8, 9, 55, 85, 70, "HR-108"],
  [5, 9, 100, 140, 110, "HR-109"],
  [3, 5, 70, 110, 80, "DL-204"],
];

let initialization;

class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

async function initialize(sql) {
  await sql`
    CREATE TABLE IF NOT EXISTS stops (
      id SERIAL PRIMARY KEY,
      name TEXT NOT NULL UNIQUE
    )
  `;
  await sql`
    CREATE TABLE IF NOT EXISTS routes (
      id SERIAL PRIMARY KEY,
      from_stop INTEGER NOT NULL REFERENCES stops(id),
      to_stop INTEGER NOT NULL REFERENCES stops(id),
      dist INTEGER NOT NULL,
      time INTEGER NOT NULL,
      fare INTEGER NOT NULL,
      bus TEXT NOT NULL,
      UNIQUE (bus, from_stop, to_stop)
    )
  `;
  await sql`
    CREATE TABLE IF NOT EXISTS bookings (
      id SERIAL PRIMARY KEY,
      passenger TEXT NOT NULL,
      from_stop INTEGER NOT NULL REFERENCES stops(id),
      to_stop INTEGER NOT NULL REFERENCES stops(id),
      dist INTEGER NOT NULL,
      time INTEGER NOT NULL,
      fare INTEGER NOT NULL,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    )
  `;

  await Promise.all(initialStops.map((name) => sql`
    INSERT INTO stops (name) VALUES (${name}) ON CONFLICT (name) DO NOTHING
  `));
  const stops = await sql`SELECT id, name FROM stops`;
  const stopIds = new Map(stops.map((stop) => [stop.name, stop.id]));
  await Promise.all(initialRoutes.map(([from, to, dist, time, fare, bus]) => sql`
    INSERT INTO routes (from_stop, to_stop, dist, time, fare, bus)
    VALUES (${stopIds.get(initialStops[from])}, ${stopIds.get(initialStops[to])}, ${dist}, ${time}, ${fare}, ${bus})
    ON CONFLICT (bus, from_stop, to_stop) DO NOTHING
  `));
}

async function getSql() {
  if (!process.env.DATABASE_URL) {
    throw new ApiError(500, "Set the DATABASE_URL environment variable.");
  }
  const sql = neon(process.env.DATABASE_URL);
  if (!initialization) {
    initialization = initialize(sql).catch((error) => {
      initialization = undefined;
      throw error;
    });
  }
  await initialization;
  return sql;
}

function parseBody(body) {
  if (body && typeof body === "object") return body;
  if (typeof body !== "string") return {};
  try {
    return JSON.parse(body);
  } catch {
    throw new ApiError(400, "Invalid JSON body");
  }
}

function parseStopId(value) {
  const id = Number(value);
  return Number.isInteger(id) && id > 0 ? id : -1;
}

function routePlan(stops, routes, source, destination, metric) {
  const distances = new Map(stops.map((stop) => [stop.id, Infinity]));
  const previous = new Map();
  const visited = new Set();
  distances.set(source, 0);

  while (visited.size < stops.length) {
    let current = -1;
    for (const [id, distance] of distances) {
      if (!visited.has(id) && distance < (current < 0 ? Infinity : distances.get(current))) {
        current = id;
      }
    }
    if (current < 0 || current === destination) break;
    visited.add(current);

    for (const route of routes) {
      const next = route.from_stop === current
        ? route.to_stop
        : route.to_stop === current
          ? route.from_stop
          : -1;
      if (next < 0 || visited.has(next)) continue;
      const cost = metric === "time" ? route.time : metric === "fare" ? route.fare : route.dist;
      const candidate = distances.get(current) + cost;
      if (candidate < distances.get(next)) {
        distances.set(next, candidate);
        previous.set(next, { route, from: current });
      }
    }
  }

  if (!Number.isFinite(distances.get(destination))) {
    throw new ApiError(404, "No route available");
  }

  const steps = [];
  let current = destination;
  let dist = 0;
  let time = 0;
  let fare = 0;
  while (current !== source) {
    const entry = previous.get(current);
    if (!entry) throw new ApiError(404, "No route available");
    const { route } = entry;
    steps.push({
      from: entry.from,
      to: current,
      bus: route.bus,
      dist: route.dist,
      time: route.time,
      fare: route.fare,
    });
    dist += route.dist;
    time += route.time;
    fare += route.fare;
    current = entry.from;
  }
  steps.reverse();

  const stopNames = new Map(stops.map((stop) => [stop.id, stop.name]));
  return {
    steps: steps.map((step) => ({
      ...step,
      from: stopNames.get(step.from),
      to: stopNames.get(step.to),
    })),
    dist,
    time,
    fare,
  };
}

function requireAdmin(request) {
  if (!process.env.ADMIN_KEY) {
    throw new ApiError(500, "Set the ADMIN_KEY environment variable.");
  }
  if (request.headers["x-admin-key"] !== process.env.ADMIN_KEY) {
    throw new ApiError(401, "Wrong admin password");
  }
}

async function handle(request, sql, url) {
  const { pathname, searchParams } = url;
  const method = request.method || "GET";

  if (method === "GET" && pathname === "/api/stops") {
    return sql`SELECT id, name FROM stops ORDER BY id`;
  }
  if (method === "GET" && pathname === "/api/routes") {
    return sql`
      SELECT r.id, r.bus, a.name AS "from", b.name AS "to", r.dist, r.time, r.fare
      FROM routes r
      JOIN stops a ON a.id = r.from_stop
      JOIN stops b ON b.id = r.to_stop
      ORDER BY r.id
    `;
  }
  if (method === "GET" && pathname === "/api/plan") {
    const source = parseStopId(searchParams.get("from"));
    const destination = parseStopId(searchParams.get("to"));
    const metric = searchParams.get("by") || "distance";
    if (source < 0 || destination < 0 || source === destination) {
      throw new ApiError(400, "Choose two different stops");
    }
    if (!["distance", "time", "fare"].includes(metric)) {
      throw new ApiError(400, "Choose distance, time, or fare");
    }
    const stops = await sql`SELECT id, name FROM stops`;
    if (!stops.some((stop) => stop.id === source) || !stops.some((stop) => stop.id === destination)) {
      throw new ApiError(400, "Choose two different stops");
    }
    const routes = await sql`SELECT from_stop, to_stop, dist, time, fare, bus FROM routes`;
    return routePlan(stops, routes, source, destination, metric);
  }
  if (method === "GET" && pathname === "/api/bookings") {
    return sql`
      SELECT b.id, b.passenger, a.name AS "from", c.name AS "to",
        b.dist, b.time, b.fare,
        TO_CHAR(b.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD HH24:MI') AS created
      FROM bookings b
      JOIN stops a ON a.id = b.from_stop
      JOIN stops c ON c.id = b.to_stop
      ORDER BY b.id DESC
    `;
  }
  if (method === "POST" && pathname === "/api/bookings") {
    const body = parseBody(request.body);
    const passenger = typeof body.passenger === "string" ? body.passenger.trim() : "";
    const source = parseStopId(body.from);
    const destination = parseStopId(body.to);
    const metric = body.by || "distance";
    if (!passenger || passenger.length >= 40 || source < 0 || destination < 0 || source === destination) {
      throw new ApiError(400, "Passenger name and two different stops required");
    }
    if (!["distance", "time", "fare"].includes(metric)) {
      throw new ApiError(400, "Choose distance, time, or fare");
    }
    const stops = await sql`SELECT id, name FROM stops`;
    if (!stops.some((stop) => stop.id === source) || !stops.some((stop) => stop.id === destination)) {
      throw new ApiError(400, "Passenger name and two different stops required");
    }
    const routes = await sql`SELECT from_stop, to_stop, dist, time, fare, bus FROM routes`;
    const plan = routePlan(stops, routes, source, destination, metric);
    const inserted = await sql`
      INSERT INTO bookings (passenger, from_stop, to_stop, dist, time, fare)
      VALUES (${passenger}, ${source}, ${destination}, ${plan.dist}, ${plan.time}, ${plan.fare})
      RETURNING id
    `;
    return { id: inserted[0].id, ...plan };
  }
  if (method === "DELETE" && (pathname === "/api/bookings" || pathname.startsWith("/api/bookings/"))) {
    const idValue = pathname === "/api/bookings"
      ? searchParams.get("id")
      : pathname.slice("/api/bookings/".length);
    const id = parseStopId(idValue);
    if (id < 1) throw new ApiError(404, "Booking not found");
    const deleted = await sql`DELETE FROM bookings WHERE id = ${id} RETURNING id`;
    if (!deleted.length) throw new ApiError(404, "Booking not found");
    return { ok: true };
  }
  if (method === "POST" && (pathname === "/api/stops" || pathname === "/api/routes")) {
    requireAdmin(request);
    const body = parseBody(request.body);
    if (pathname === "/api/stops") {
      const name = typeof body.name === "string" ? body.name.trim() : "";
      if (!name || name.length >= 40) throw new ApiError(400, "A stop name under 40 characters is required");
      const inserted = await sql`
        INSERT INTO stops (name) VALUES (${name})
        ON CONFLICT (name) DO NOTHING
        RETURNING id
      `;
      if (!inserted.length) throw new ApiError(409, "Stop already exists");
    } else {
      const source = parseStopId(body.from);
      const destination = parseStopId(body.to);
      const dist = Number(body.dist);
      const time = Number(body.time);
      const fare = Number(body.fare);
      const bus = typeof body.bus === "string" ? body.bus.trim() : "";
      if (
        source < 1 || destination < 1 || source === destination ||
        !Number.isInteger(dist) || dist <= 0 ||
        !Number.isInteger(time) || time <= 0 ||
        !Number.isInteger(fare) || fare < 0 ||
        !bus || bus.length >= 12
      ) {
        throw new ApiError(400, "Invalid route data");
      }
      const stops = await sql`SELECT id FROM stops WHERE id IN (${source}, ${destination})`;
      if (stops.length !== 2) throw new ApiError(400, "Invalid route data");
      try {
        await sql`
          INSERT INTO routes (from_stop, to_stop, dist, time, fare, bus)
          VALUES (${source}, ${destination}, ${dist}, ${time}, ${fare}, ${bus})
        `;
      } catch (error) {
        if (error.code === "23505") throw new ApiError(409, "Route already exists");
        throw error;
      }
    }
    return { ok: true };
  }
  throw new ApiError(404, "Unknown API endpoint");
}

module.exports = async function handler(request, response) {
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  try {
    const url = new URL(request.url, "http://localhost");
    const sql = await getSql();
    const result = await handle(request, sql, url);
    response.status(request.method === "POST" ? 201 : 200).json(result);
  } catch (error) {
    if (error instanceof ApiError) {
      response.status(error.status).json({ error: error.message });
      return;
    }
    console.error("BusRoute API error:", error);
    response.status(500).json({ error: "Internal server error" });
  }
};
