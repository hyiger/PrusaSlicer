///|/ Copyright (c) Filament DB integration
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FilamentScanClient.hpp"

#include "GUI_App.hpp"
#include "Tab.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "nlohmann/json.hpp"

#include <curl/curl.h>
#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Slic3r { namespace GUI {

namespace {

// Ignore `replay` events older than this — protects against the slicer
// opening hours after a scan and silently flipping the preset to a stale
// filament. Live `scan` events from the publisher are never filtered.
constexpr int64_t REPLAY_MAX_AGE_MS = 10 * 60 * 1000;

// Per-connection parser state carried through libcurl's write callback.
struct StreamState {
    FilamentScanClient* self = nullptr;
    std::string         buffer;
};

void process_record(FilamentScanClient* self, const std::string& record)
{
    std::string event_type = "message";
    std::string data;
    std::size_t line_start = 0;
    while (line_start <= record.size()) {
        const std::size_t nl = record.find('\n', line_start);
        const std::string line = record.substr(
            line_start,
            nl == std::string::npos ? std::string::npos : nl - line_start);

        if (! line.empty() && line[0] != ':') {
            // SSE field syntax: "field: value" or "field:value". A
            // leading space after the colon is stripped per the spec.
            if (line.rfind("event:", 0) == 0) {
                std::size_t s = 6;
                if (s < line.size() && line[s] == ' ') ++s;
                event_type = line.substr(s);
            } else if (line.rfind("data:", 0) == 0) {
                std::size_t s = 5;
                if (s < line.size() && line[s] == ' ') ++s;
                data += line.substr(s);
            }
            // `retry:` is ignored — we run our own reconnect loop with
            // exponential backoff (libcurl doesn't honour the spec's
            // retry hint on its own).
        }
        if (nl == std::string::npos) break;
        line_start = nl + 1;
    }
    if (! data.empty())
        self->handle_event(event_type, data);
}

extern "C" std::size_t scan_stream_write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
    auto* st = static_cast<StreamState*>(userdata);
    const std::size_t n = size * nmemb;
    st->buffer.append(ptr, n);
    for (;;) {
        const std::size_t pos = st->buffer.find("\n\n");
        if (pos == std::string::npos) break;
        const std::string record = st->buffer.substr(0, pos);
        st->buffer.erase(0, pos + 2);
        process_record(st->self, record);
    }
    return n;
}

extern "C" int scan_stream_xferinfo_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    // Non-zero return aborts the in-flight transfer with
    // CURLE_ABORTED_BY_CALLBACK so stop() can break us out of a
    // blocking curl_easy_perform without waiting for the next byte.
    auto* stop_flag = static_cast<std::atomic<bool>*>(clientp);
    return (stop_flag != nullptr && stop_flag->load()) ? 1 : 0;
}

} // namespace

FilamentScanClient::FilamentScanClient(std::string base_url)
    : m_base_url(std::move(base_url))
{}

FilamentScanClient::~FilamentScanClient()
{
    stop();
}

void FilamentScanClient::start()
{
    // exchange returns the previous value — bail if we were already
    // running. Calls are idempotent.
    if (m_running.exchange(true)) return;
    m_stop.store(false);
    m_thread = std::thread([this] { this->run(); });
}

void FilamentScanClient::stop()
{
    if (! m_running.exchange(false)) return;
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
}

void FilamentScanClient::handle_event(const std::string& event_type, const std::string& data_json)
{
    // The server emits two relevant event types — `scan` (live tag read)
    // and `replay` (the most recent scan, sent once on connect).
    // Anything else (heartbeat comments, future event types) is ignored.
    if (event_type != "scan" && event_type != "replay") return;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(data_json);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "[FilamentDB] scan stream: JSON parse failed: " << e.what();
        return;
    }

    if (! j.contains("filament") || j["filament"].is_null()) {
        // No DB match — the user scanned a tag we don't have a preset
        // for. Leaving the current preset alone is the right behaviour;
        // surfacing this in the UI is a follow-up.
        return;
    }

    const auto& fil = j["filament"];
    const std::string preset_name = fil.value("name", std::string{});
    if (preset_name.empty()) return;

    if (event_type == "replay") {
        const int64_t ts = j.value("timestamp", int64_t{0});
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        if (ts > 0 && (now_ms - ts) > REPLAY_MAX_AGE_MS) {
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] ignoring stale replay (" << (now_ms - ts) / 1000
                << "s old) for preset '" << preset_name << "'";
            return;
        }
    }

    // wx's CallAfter is documented thread-safe — queues the lambda onto
    // the main event loop. Tab::select_preset touches wx widgets and
    // must not be reentered from a worker thread.
    wxGetApp().CallAfter([preset_name]() {
        auto& app = wxGetApp();
        if (app.preset_bundle == nullptr) return;

        const Preset* preset = app.preset_bundle->filaments.find_preset(preset_name);
        if (preset == nullptr) {
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] no matching filament preset for '" << preset_name
                << "' — the slicer hasn't synced this filament yet";
            return;
        }

        Tab* tab = app.get_tab(Preset::TYPE_FILAMENT);
        if (tab == nullptr) return;

        // Tab::select_preset is what the dropdown calls — it handles the
        // dirty-state prompt, compatibility checks, and refreshing the
        // per-extruder combobox. Going through PresetBundle::set_filament_preset
        // directly would skip all of that.
        tab->select_preset(preset_name);
    });
}

void FilamentScanClient::run()
{
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "[FilamentDB] curl_easy_init failed; scan client disabled";
        return;
    }

    const std::string url = m_base_url + "/api/scan/stream";
    BOOST_LOG_TRIVIAL(info) << "[FilamentDB] scan client subscribing to " << url;

    using namespace std::chrono_literals;
    auto       backoff     = 500ms;
    const auto max_backoff = 30s;

    while (! m_stop.load()) {
        StreamState st;
        st.self = this;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &scan_stream_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);          // no overall cap
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // ≥1 B
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 90L);  // ~3.6× heartbeat
        // Honour cooperative cancellation between bytes.
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &scan_stream_xferinfo_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &m_stop);

        const CURLcode rc = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (m_stop.load()) break;

        if (rc != CURLE_OK) {
            BOOST_LOG_TRIVIAL(debug)
                << "[FilamentDB] scan stream disconnected (" << curl_easy_strerror(rc)
                << "); reconnecting after " << backoff.count() << "ms";
        }

        std::this_thread::sleep_for(backoff);
        backoff = std::min<std::chrono::milliseconds>(backoff * 2, max_backoff);
    }

    curl_easy_cleanup(curl);
    BOOST_LOG_TRIVIAL(info) << "[FilamentDB] scan client stopped";
}

} } // namespace Slic3r::GUI
