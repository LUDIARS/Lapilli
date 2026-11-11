//最初の利用者 (Memoria の owntracks-server の C++ 移植) が要る操作で API が足りることを確かめる
//  memoria/server/db.ts の gps_locations の DDL と insertGpsLocation の 3 分岐 (重複 / 停止区間の圧縮 / 挿入) を写した
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "test.hpp"

using namespace lapilli::sqlite;

namespace {

//db.ts の CREATE TABLE gps_locations (後から ALTER で足した列も含む) と索引
const char* kGpsDdl = R"(
CREATE TABLE IF NOT EXISTS gps_locations (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id          TEXT NOT NULL DEFAULT 'me',
  device_id        TEXT,
  recorded_at      TEXT NOT NULL,
  lat              REAL NOT NULL,
  lon              REAL NOT NULL,
  accuracy_m       REAL,
  altitude_m       REAL,
  velocity_kmh     REAL,
  course_deg       REAL,
  battery_pct      INTEGER,
  conn             TEXT,
  raw_json         TEXT,
  received_at      TEXT NOT NULL DEFAULT (datetime('now')),
  samples_count    INTEGER NOT NULL DEFAULT 1,
  samples_first_at TEXT,
  place_name       TEXT,
  place_address    TEXT,
  place_source     TEXT,
  place_resolved_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_gps_locations_at ON gps_locations(recorded_at DESC);
CREATE INDEX IF NOT EXISTS idx_gps_locations_user_at ON gps_locations(user_id, recorded_at DESC);
CREATE INDEX IF NOT EXISTS idx_gps_locations_dedup ON gps_locations(user_id, device_id, recorded_at);
)";

constexpr double kStationaryThresholdM = 50;

struct GpsInput {
	std::string user_id = "me";
	std::optional<std::string> device_id;
	std::string recorded_at;
	double lat = 0;
	double lon = 0;
	std::optional<double> accuracy;
	std::optional<double> altitude;
	std::optional<double> velocity;
	std::optional<double> course;
	std::optional<std::int64_t> battery;
	std::optional<std::string> conn;
	std::optional<std::string> raw_json;
};

enum class Outcome { skipped, merged, inserted };

struct InsertResult {
	Outcome outcome;
	std::int64_t id;
};

struct RecentRow {
	std::int64_t id;
	double lat;
	double lon;
	std::string recorded_at;
	std::optional<std::string> samples_first_at;
};

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
	const double r = 6371008;
	const double pi = 3.14159265358979323846;
	const auto t = [&](double d) { return d * pi / 180; };
	const double sa = std::sin(t(lat2 - lat1) / 2);
	const double so = std::sin(t(lon2 - lon1) / 2);
	const double h = sa * sa + std::cos(t(lat1)) * std::cos(t(lat2)) * so * so;
	return 2 * r * std::asin(std::fmin(1.0, std::sqrt(h)));
}

//insertGpsLocation の写し (recordedAt は呼び出し側で決める)
InsertResult insert_gps_location(Database& db, const GpsInput& loc) {
	Statement dup = db.prepare(R"(
		SELECT id FROM gps_locations
		WHERE user_id = ? AND IFNULL(device_id, '') = IFNULL(?, '') AND recorded_at = ?
		LIMIT 1)");
	dup.bind_all(loc.user_id, loc.device_id, loc.recorded_at);
	if (const auto id = dup.first([](const Statement& r) { return r.column_int64(0); })) return { Outcome::skipped, *id };

	Statement recent_q = db.prepare(R"(
		SELECT id, lat, lon, recorded_at, samples_count, samples_first_at
		FROM gps_locations
		WHERE user_id = ? AND IFNULL(device_id, '') = IFNULL(?, '')
		ORDER BY recorded_at DESC
		LIMIT 2)");
	recent_q.bind_all(loc.user_id, loc.device_id);
	std::vector<RecentRow> recent;
	recent_q.for_each([&](const Statement& r) {
		recent.push_back({ r.column_int64(0), r.column_double(1), r.column_double(2), r.column_text(3), r.column_optional<std::string>(5) });
	});

	if (recent.size() == 2) {
		const RecentRow& last = recent[0];
		const RecentRow& prev = recent[1];
		if (haversine_m(prev.lat, prev.lon, last.lat, last.lon) < kStationaryThresholdM &&
			haversine_m(prev.lat, prev.lon, loc.lat, loc.lon) < kStationaryThresholdM &&
			haversine_m(last.lat, last.lon, loc.lat, loc.lon) < kStationaryThresholdM) {
			const std::string samples_first_at = last.samples_first_at.value_or(last.recorded_at);
			Statement upd = db.prepare(R"(
				UPDATE gps_locations
				SET recorded_at = ?, lat = ?, lon = ?,
				    accuracy_m = ?, altitude_m = ?, velocity_kmh = ?, course_deg = ?,
				    battery_pct = ?, conn = ?, raw_json = ?,
				    samples_count = samples_count + 1,
				    samples_first_at = COALESCE(samples_first_at, ?)
				WHERE id = ?)");
			upd.bind_all(loc.recorded_at, loc.lat, loc.lon, loc.accuracy, loc.altitude, loc.velocity, loc.course, loc.battery, loc.conn, loc.raw_json, samples_first_at, last.id).run();
			return { Outcome::merged, last.id };
		}
	}

	Statement ins = db.prepare(R"(
		INSERT INTO gps_locations
		  (user_id, device_id, recorded_at, lat, lon,
		   accuracy_m, altitude_m, velocity_kmh, course_deg, battery_pct, conn, raw_json,
		   samples_count, samples_first_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, NULL))");
	const RunResult info = ins.bind_all(loc.user_id, loc.device_id, loc.recorded_at, loc.lat, loc.lon, loc.accuracy, loc.altitude, loc.velocity, loc.course, loc.battery, loc.conn, loc.raw_json).run();
	return { Outcome::inserted, info.last_insert_rowid };
}

GpsInput point(const char* at, double lat, double lon) {
	GpsInput p;
	p.recorded_at = at;
	p.lat = lat;
	p.lon = lon;
	return p;
}

std::int64_t row_count(Database& db) {
	Statement s = db.prepare("SELECT count(*) FROM gps_locations");
	return s.first([](const Statement& r) { return r.column_int64(0); }).value_or(-1);
}

}

TEST(gps_insert_three_way, "gps_locations: 重複は skip、停止中の 3 点目は UPDATE で圧縮、動いた点は INSERT (insertGpsLocation の写し)") {
	testing::TempPath path;
	OpenOptions o;
	o.create = true;
	o.wal = true;
	Database db = Database::open(path.str(), o);
	db.exec(kGpsDdl);
	const std::vector<std::string> cols = db.table_columns("gps_locations");
	CHECK_EQ(cols.size(), 20u);
	CHECK_EQ(cols.back(), "place_resolved_at");

	//device_id なし (NULL の bind と IFNULL の比較を通す)。0.0001 度 ≒ 11m
	GpsInput a = point("2026-11-11T00:00:00.000Z", 35.0, 135.0);
	a.accuracy = 8.5;
	a.battery = 80;
	a.conn = std::string("w");
	a.raw_json = std::string(u8R"({"_type":"location","desc":"自宅"})");
	const InsertResult r1 = insert_gps_location(db, a);
	CHECK(r1.outcome == Outcome::inserted);
	CHECK_EQ(r1.id, 1);

	const InsertResult dup = insert_gps_location(db, a);
	CHECK(dup.outcome == Outcome::skipped);
	CHECK_EQ(dup.id, r1.id);
	CHECK_EQ(row_count(db), 1);

	const InsertResult r2 = insert_gps_location(db, point("2026-11-11T00:01:00.000Z", 35.0001, 135.0));
	CHECK(r2.outcome == Outcome::inserted);	//直近が 1 行だけなので圧縮しない

	GpsInput c = point("2026-11-11T00:02:00.000Z", 35.0, 135.0001);
	c.velocity = 0.0;
	const InsertResult r3 = insert_gps_location(db, c);
	CHECK(r3.outcome == Outcome::merged);
	CHECK_EQ(r3.id, r2.id);
	CHECK_EQ(row_count(db), 2);

	const InsertResult r4 = insert_gps_location(db, point("2026-11-11T00:03:00.000Z", 35.0001, 135.0001));
	CHECK(r4.outcome == Outcome::merged);
	CHECK_EQ(r4.id, r2.id);

	Statement tail = db.prepare("SELECT recorded_at, samples_count, samples_first_at, velocity_kmh, accuracy_m FROM gps_locations WHERE id = ?");
	tail.bind_all(r2.id);
	CHECK(tail.step());
	CHECK_EQ(tail.column_text(0), "2026-11-11T00:03:00.000Z");
	CHECK_EQ(tail.column_int64(1), 3);
	CHECK_EQ(tail.column_text(2), "2026-11-11T00:01:00.000Z");	//最初に圧縮対象になったときの recorded_at を保つ
	CHECK(tail.column_is_null(3));	//4 点目は velocity なし → NULL で上書き
	CHECK(tail.column_is_null(4));
	tail.reset();

	//1km ほど動いた
	const InsertResult r5 = insert_gps_location(db, point("2026-11-11T00:04:00.000Z", 35.01, 135.0));
	CHECK(r5.outcome == Outcome::inserted);
	CHECK_EQ(r5.id, 3);
	CHECK_EQ(row_count(db), 3);

	//別の端末は別系列 (同じ時刻でも重複ではない)
	GpsInput phone = a;
	phone.device_id = std::string("phone");
	const InsertResult r6 = insert_gps_location(db, phone);
	CHECK(r6.outcome == Outcome::inserted);
	CHECK_EQ(insert_gps_location(db, phone).outcome, Outcome::skipped);

	Statement first = db.prepare("SELECT raw_json, battery_pct, conn, user_id, received_at FROM gps_locations WHERE id = 1");
	CHECK(first.step());
	CHECK_EQ(first.column_text(0), std::string(u8R"({"_type":"location","desc":"自宅"})"));
	CHECK_EQ(first.column_int64(1), 80);
	CHECK_EQ(first.column_text(2), "w");
	CHECK_EQ(first.column_text(3), "me");
	CHECK(!first.column_is_null(4));	//DEFAULT (datetime('now'))
}
