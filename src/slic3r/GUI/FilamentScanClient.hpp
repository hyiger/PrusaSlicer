///|/ Copyright (c) Filament DB integration
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_FilamentScanClient_hpp_
#define slic3r_GUI_FilamentScanClient_hpp_

#include <atomic>
#include <string>
#include <thread>

namespace Slic3r { namespace GUI {

// Subscribes to a Filament DB instance's `/api/scan/stream` Server-Sent
// Events endpoint and switches the active filament preset to match each
// scanned NFC tag. Runs a libcurl loop on a worker thread; every event
// is marshalled back to the wx GUI thread before touching any preset
// state.
//
// Lifecycle: construct in `GUI_App::post_init()` after `preset_bundle`
// is loaded, destroy from `~GUI_App`. `start()` is safe to call from
// the GUI thread and is idempotent; `stop()` blocks until the worker
// has joined.
//
// The base URL points at the Filament DB instance (e.g.
// `http://localhost:3456` for a same-machine Electron deploy or
// `http://raspberrypi.local:3456` for a Pi running headlessly). The
// slicer does not need to be on the same physical machine — only
// network-reachable to the Filament DB instance.
class FilamentScanClient
{
public:
    explicit FilamentScanClient(std::string base_url);
    ~FilamentScanClient();

    FilamentScanClient(const FilamentScanClient&)            = delete;
    FilamentScanClient& operator=(const FilamentScanClient&) = delete;

    void start();
    void stop();

    // Public so the libcurl write callback (translation-unit-local in the
    // .cpp file) can dispatch each parsed SSE record back into the class
    // without needing friend declarations.
    void handle_event(const std::string& event_type, const std::string& data_json);

private:
    void run();

    std::string       m_base_url;
    std::atomic<bool> m_stop { false };
    std::atomic<bool> m_running { false };
    std::thread       m_thread;
};

} } // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentScanClient_hpp_
