/**
 * mm_bootstrap.cpp — Optional HTTP matchmaking bootstrap before netpeer reads env.
 *
 * When built with libcurl (SSB64_HAVE_LIBCURL) and SSB64_MM_AUTO=1, performs the
 * same flow as scripts/mm_smoke_two_clients.sh then sets SSB64_NETPLAY_* via
 * the process environment. See docs/netplay_matchmaking_debug.md.
 */

#include "mm_bootstrap.h"

#include "port_log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef SSB64_HAVE_LIBCURL
#include <curl/curl.h>
#endif

#ifdef _WIN32
#include <stdlib.h>
#else
#include <unistd.h>
#endif

#include <chrono>

static void mm_setenv(const char *key, const char *value)
{
#ifdef _WIN32
	std::string line = std::string(key) + "=" + value;
	_putenv(line.c_str());
#else
	if (setenv(key, value, 1) != 0) {
		port_log("SSB64 MM: setenv(%s) failed\n", key);
	}
#endif
}

static std::string mm_trim(std::string s)
{
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
		s.erase(s.begin());
	}
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
		s.pop_back();
	}
	return s;
}

static std::string mm_env_or(const char *key, const char *fallback)
{
	const char *v = std::getenv(key);
	if (v == nullptr || v[0] == '\0') {
		return std::string(fallback);
	}
	return std::string(v);
}

static bool json_extract_string(const std::string &j, const char *key, std::string &out)
{
	const std::string pat = std::string("\"") + key + "\":\"";
	const size_t p = j.find(pat);
	if (p == std::string::npos) {
		return false;
	}
	size_t start = p + pat.size();
	const size_t e = j.find('"', start);
	if (e == std::string::npos) {
		return false;
	}
	out.assign(j, start, e - start);
	return true;
}

static bool json_extract_bool(const std::string &j, const char *key, bool &out)
{
	const std::string pat = std::string("\"") + key + "\":";
	const size_t p = j.find(pat);
	if (p == std::string::npos) {
		return false;
	}
	size_t q = p + pat.size();
	while (q < j.size() && (j[q] == ' ' || j[q] == '\t')) {
		q++;
	}
	if (q + 4 <= j.size() && j.compare(q, 4, "true") == 0) {
		out = true;
		return true;
	}
	if (q + 5 <= j.size() && j.compare(q, 5, "false") == 0) {
		out = false;
		return true;
	}
	return false;
}

static bool json_extract_uint32(const std::string &j, const char *key, uint32_t &out)
{
	const std::string pat = std::string("\"") + key + "\":";
	const size_t p = j.find(pat);
	if (p == std::string::npos) {
		return false;
	}
	size_t q = p + pat.size();
	while (q < j.size() && (j[q] == ' ' || j[q] == '\t')) {
		q++;
	}
	char *end = nullptr;
	unsigned long v = std::strtoul(j.c_str() + q, &end, 10);
	if (end == j.c_str() + q) {
		return false;
	}
	out = static_cast<uint32_t>(v);
	return true;
}

#ifdef SSB64_HAVE_LIBCURL

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *s = static_cast<std::string *>(userdata);
	s->append(ptr, size * nmemb);
	return size * nmemb;
}

static bool mm_curl_post(const std::string &url, const std::string &hdr1, const std::string &hdr2,
                          const std::string &body, std::string &out_resp, long *http_code)
{
	CURL *curl = curl_easy_init();
	if (curl == nullptr) {
		return false;
	}
	out_resp.clear();
	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!hdr1.empty()) {
		headers = curl_slist_append(headers, hdr1.c_str());
	}
	if (!hdr2.empty()) {
		headers = curl_slist_append(headers, hdr2.c_str());
	}
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	const CURLcode rc = curl_easy_perform(curl);
	long code = 0;
	if (rc == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	if (http_code) {
		*http_code = code;
	}
	return rc == CURLE_OK && code >= 200 && code < 300;
}

static bool mm_curl_get(const std::string &url, std::string &out_resp, long *http_code)
{
	CURL *curl = curl_easy_init();
	if (curl == nullptr) {
		return false;
	}
	out_resp.clear();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	const CURLcode rc = curl_easy_perform(curl);
	long code = 0;
	if (rc == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	}
	curl_easy_cleanup(curl);
	if (http_code) {
		*http_code = code;
	}
	return rc == CURLE_OK && code >= 200 && code < 300;
}

static void mm_curl_global_init_once(void)
{
	static bool done = false;
	if (done) {
		return;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	done = true;
}

static bool mm_poll_match(const std::string &base, const std::string &ticket, const std::string &pid,
                           const std::string &tok, std::string &match_json)
{
	const std::string hdr1 = "X-Player-Id: " + pid;
	const std::string hdr2 = "Authorization: Bearer " + tok;
	int hb = 0;
	const int timeout_sec = []() {
		const char *e = std::getenv("SSB64_MM_POLL_TIMEOUT_SECS");
		if (e == nullptr || e[0] == '\0') {
			return 300;
		}
		const int v = std::atoi(e);
		return v > 0 ? v : 300;
	}();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
	for (;;) {
		if (std::chrono::steady_clock::now() > deadline) {
			port_log("SSB64 MM: poll timeout (%ds)\n", timeout_sec);
			return false;
		}
		std::string resp;
		long code = 0;
		const std::string url = base + "/v1/match/" + ticket;
		if (!mm_curl_get(url, resp, &code)) {
			port_log("SSB64 MM: GET match failed code=%ld\n", code);
			return false;
		}
		std::string st;
		if (!json_extract_string(resp, "status", st)) {
			port_log("SSB64 MM: match response missing status\n");
			return false;
		}
		if (st == "matched") {
			match_json = std::move(resp);
			return true;
		}
		if (hb % 10 == 0) {
			const std::string body = std::string("{\"ticket_id\":\"") + ticket + "\"}";
			std::string ignore;
			mm_curl_post(base + "/v1/heartbeat", hdr1, hdr2, body, ignore, nullptr);
		}
		hb++;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}

static void mm_bootstrap_with_curl(void)
{
	mm_curl_global_init_once();

	const std::string base_raw = mm_env_or("SSB64_MM_BASE_URL", "http://127.0.0.1:8080");
	std::string base = base_raw;
	while (!base.empty() && base.back() == '/') {
		base.pop_back();
	}

	const std::string bind = mm_trim(mm_env_or("SSB64_MM_BIND", ""));
	if (bind.empty()) {
		port_log("SSB64 MM: SSB64_MM_AUTO=1 but SSB64_MM_BIND is empty (need host:port)\n");
		return;
	}

	std::string pid = mm_trim(mm_env_or("SSB64_MM_PLAYER_ID", ""));
	std::string tok = mm_trim(mm_env_or("SSB64_MM_API_TOKEN", ""));
	if (pid.empty() || tok.empty()) {
		std::string body = "{}";
		std::string resp;
		long code = 0;
		if (!mm_curl_post(base + "/v1/players", "", "", body, resp, &code)) {
			port_log("SSB64 MM: POST /v1/players failed code=%ld\n", code);
			return;
		}
		if (!json_extract_string(resp, "player_id", pid) || !json_extract_string(resp, "api_token", tok)) {
			port_log("SSB64 MM: bad /v1/players JSON\n");
			return;
		}
		port_log("SSB64 MM: created player_id=%s (set SSB64_MM_PLAYER_ID / SSB64_MM_API_TOKEN to reuse)\n",
		         pid.c_str());
	}

	const std::string region = mm_env_or("SSB64_MM_REGION", "na-east");
	const std::string gver = mm_env_or("SSB64_MM_GAME_VERSION", "dev");
	const std::string qbody =
	    std::string("{\"udp_endpoint\":\"") + bind + "\",\"region\":\"" + region + "\",\"game_version\":\"" + gver +
	    "\"}";
	const std::string hdr1 = "X-Player-Id: " + pid;
	const std::string hdr2 = "Authorization: Bearer " + tok;
	std::string qresp;
	long qcode = 0;
	if (!mm_curl_post(base + "/v1/queue", hdr1, hdr2, qbody, qresp, &qcode)) {
		port_log("SSB64 MM: POST /v1/queue failed code=%ld\n", qcode);
		return;
	}
	std::string ticket;
	if (!json_extract_string(qresp, "ticket_id", ticket)) {
		port_log("SSB64 MM: queue response missing ticket_id\n");
		return;
	}
	port_log("SSB64 MM: queued ticket_id=%s\n", ticket.c_str());

	std::string mresp;
	if (!mm_poll_match(base, ticket, pid, tok, mresp)) {
		return;
	}

	const size_t mpos = mresp.find("\"match\"");
	if (mpos == std::string::npos) {
		port_log("SSB64 MM: matched payload missing match object\n");
		return;
	}
	const std::string sub = mresp.substr(mpos);

	std::string peer;
	uint32_t session_id = 0;
	bool host = false;
	if (!json_extract_string(sub, "peer", peer) || !json_extract_uint32(sub, "session_id", session_id) ||
	    !json_extract_bool(sub, "you_are_host", host)) {
		port_log("SSB64 MM: could not parse match fields\n");
		return;
	}

	const std::string local_slot = mm_env_or("SSB64_MM_LOCAL_PLAYER", "0");
	const std::string remote_slot = mm_env_or("SSB64_MM_REMOTE_PLAYER", "1");
	const std::string delay = mm_env_or("SSB64_MM_NETPLAY_DELAY", "2");

	mm_setenv("SSB64_NETPLAY", "1");
	mm_setenv("SSB64_NETPLAY_BOOTSTRAP", "1");
	mm_setenv("SSB64_NETPLAY_LOCAL_PLAYER", local_slot.c_str());
	mm_setenv("SSB64_NETPLAY_REMOTE_PLAYER", remote_slot.c_str());
	mm_setenv("SSB64_NETPLAY_BIND", bind.c_str());
	mm_setenv("SSB64_NETPLAY_PEER", peer.c_str());
	char sess_buf[32];
	std::snprintf(sess_buf, sizeof(sess_buf), "%u", session_id);
	mm_setenv("SSB64_NETPLAY_SESSION", sess_buf);
	mm_setenv("SSB64_NETPLAY_HOST", host ? "1" : "0");
	mm_setenv("SSB64_NETPLAY_DELAY", delay.c_str());

	port_log("SSB64 MM: applied netplay env session=%s peer=%s host=%d\n", sess_buf, peer.c_str(),
	         host ? 1 : 0);
}

#endif /* SSB64_HAVE_LIBCURL */

extern "C" void port_mm_bootstrap_try(void)
{
	const char *auto_mm = std::getenv("SSB64_MM_AUTO");
	if (auto_mm == nullptr || auto_mm[0] == '\0' || std::strcmp(auto_mm, "1") != 0) {
		return;
	}
#ifdef SSB64_HAVE_LIBCURL
	mm_bootstrap_with_curl();
#else
	port_log("SSB64 MM: SSB64_MM_AUTO=1 but this build has no libcurl (SSB64_HAVE_LIBCURL); use "
	         "scripts/mm_smoke_two_clients.sh\n");
#endif
}

extern "C" void port_mm_game_heartbeat_tick(void)
{
#ifdef SSB64_HAVE_LIBCURL
	const char *ticket = std::getenv("SSB64_MM_HEARTBEAT_TICKET");
	if (ticket == nullptr || ticket[0] == '\0') {
		return;
	}
	const char *pid = std::getenv("SSB64_MM_PLAYER_ID");
	const char *tok = std::getenv("SSB64_MM_API_TOKEN");
	if (pid == nullptr || tok == nullptr) {
		return;
	}
	static std::chrono::steady_clock::time_point last{};
	static bool last_init = false;
	const auto now = std::chrono::steady_clock::now();
	if (last_init && std::chrono::duration_cast<std::chrono::seconds>(now - last).count() < 2) {
		return;
	}
	last = now;
	last_init = true;

	mm_curl_global_init_once();
	std::string base = mm_trim(mm_env_or("SSB64_MM_BASE_URL", "http://127.0.0.1:8080"));
	while (!base.empty() && base.back() == '/') {
		base.pop_back();
	}
	const std::string hdr1 = std::string("X-Player-Id: ") + pid;
	const std::string hdr2 = std::string("Authorization: Bearer ") + tok;
	const std::string body = std::string("{\"ticket_id\":\"") + ticket + "\"}";
	std::string ignore;
	mm_curl_post(base + "/v1/heartbeat", hdr1, hdr2, body, ignore, nullptr);
#else
	(void)0;
#endif
}
