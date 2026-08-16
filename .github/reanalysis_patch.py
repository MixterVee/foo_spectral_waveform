from pathlib import Path


def edit(path, transform):
    p = Path(path)
    before = p.read_text(encoding="utf-8")
    after = transform(before)
    if after == before:
        raise SystemExit(f"{path}: patch made no change")
    p.write_text(after, encoding="utf-8")


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


def patch_cache_h(text):
    marker = """void save_stem_waveform_cache(
    metadb_handle_ptr track,
    int mode,
    const waveform_data& data,
    abort_callback& aborter);
"""
    replacement = marker + """
// Removes Original, Vocals and Instrumental waveform caches for one track.
// Missing/unwritable cache files are ignored; abort requests still propagate.
void remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter);
"""
    return once(text, marker, replacement, "waveform_cache.h")


def patch_cache_cpp(text):
    anchor = "} // namespace\n\nbool load_waveform_cache"
    helper = """void remove_cache_variant(
    metadb_handle_ptr track,
    int mode,
    abort_callback& aborter) {

    if (track.is_empty()) return;

    try {
        const auto path = cache_path_for_track(track, mode);
        if (filesystem::g_exists(path, aborter)) {
            filesystem::g_remove(path, aborter);
        }
    } catch (exception_aborted const&) {
        throw;
    } catch (exception_io const&) {
        // Manual recovery is best-effort. A missing/unwritable cache must not
        // interfere with playback or with the new analysis.
    }
}

} // namespace

bool load_waveform_cache"""
    text = once(text, anchor, helper, "waveform_cache.cpp helper")

    tail = """    if (mode != 1 && mode != 2) return;
    save_cache_variant(track, mode, data, aborter);
}

} // namespace spectral_waveform
"""
    replacement = """    if (mode != 1 && mode != 2) return;
    save_cache_variant(track, mode, data, aborter);
}

void remove_waveform_caches(metadb_handle_ptr track, abort_callback& aborter) {
    remove_cache_variant(track, 0, aborter);
    remove_cache_variant(track, 1, aborter);
    remove_cache_variant(track, 2, aborter);
}

} // namespace spectral_waveform
"""
    return once(text, tail, replacement, "waveform_cache.cpp public")


def patch_live_h(text):
    return once(
        text,
        "// Clears the active stem preview.\nvoid reset();",
        "// Clears the active previews and invalidates the current stem-analysis\n"
        "// generation so stale workers cannot recreate caches during manual recovery.\n"
        "void reset();",
        "live_output_capture.h",
    )


def patch_live_cpp(text):
    old = """    void cancel_keep_preview() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_analysisActive = false;
        m_ready = false;
    }

private:
"""
    new = """    void cancel_keep_preview() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_analysisActive = false;
        m_ready = false;
    }

    void invalidate_current() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_analysisActive = false;
        m_ready = false;
    }

private:
"""
    text = once(text, old, new, "live manager invalidate")

    old = """    void prime(metadb_handle_ptr track) {
        m_track = track;
        m_lastMode = current_stem_mode();
        const double position = std::max(0.0, playback_control::get()->playback_get_position());
        if (g_manager) g_manager->request(m_track, m_lastMode, position);
    }

private:
"""
    new = """    void prime(metadb_handle_ptr track) {
        m_track = track;
        m_lastMode = current_stem_mode();
        const double position = std::max(0.0, playback_control::get()->playback_get_position());
        if (g_manager) g_manager->request(m_track, m_lastMode, position);
    }

    void force_mode_refresh() {
        m_lastMode = -999;
    }

private:
"""
    text = once(text, old, new, "live observer refresh")

    return once(
        text,
        "void reset() {\n    clear_previews(true);\n}",
        "void reset() {\n"
        "    if (g_manager) g_manager->invalidate_current();\n"
        "    if (g_observer) g_observer->force_mode_refresh();\n"
        "    clear_previews(true);\n"
        "}",
        "live reset",
    )


def patch_ui(text):
    text = once(
        text,
        "    kMenuFollowPaged,\n};",
        "    kMenuFollowPaged,\n    kMenuReanalyze,\n};",
        "ui enum",
    )

    marker = """            kMenuFollowPaged, L"Follow Mode: Page at Edge");

        const UINT command = TrackPopupMenu(menu,"""
    replacement = """            kMenuFollowPaged, L"Follow Mode: Page at Edge");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_currentTrack.is_empty() ? MF_GRAYED : 0),
            kMenuReanalyze, L"Re-analyze Current Track");

        const UINT command = TrackPopupMenu(menu,"""
    text = once(text, marker, replacement, "ui menu")

    marker = """        case kMenuFollowPaged:
            m_followMode = follow_mode::paged;
            if (m_followPlayhead) invalidate_frame();
            break;
        }
    }

    void begin_drag(int x) {"""
    replacement = """        case kMenuFollowPaged:
            m_followMode = follow_mode::paged;
            if (m_followPlayhead) invalidate_frame();
            break;
        case kMenuReanalyze:
            reanalyze_current_track();
            break;
        }
    }

    void reanalyze_current_track() {
        const auto track = m_currentTrack;
        if (track.is_empty()) return;

        // Stop both generations before deleting files so stale background work
        // cannot recreate an old cache after the recovery command runs.
        stop_analysis();
        spectral_waveform::live_output_capture::reset();

        abort_callback_impl aborter;
        spectral_waveform::remove_waveform_caches(track, aborter);

        // Keep the user's zoom/pan while rebuilding the Original waveform now.
        start_analysis(track, false);
    }

    void begin_drag(int x) {"""
    text = once(text, marker, replacement, "ui command")

    marker = """    void start_analysis(metadb_handle_ptr track) {
        stop_analysis();
        clear_waveform();
        reset_view();
        m_bufferValid = false;"""
    replacement = """    void start_analysis(metadb_handle_ptr track, bool resetView = true) {
        stop_analysis();
        m_currentTrack = track;
        clear_waveform();
        if (resetView) reset_view();
        m_bufferValid = false;"""
    text = once(text, marker, replacement, "ui start analysis")

    marker = """    mutable std::mutex m_waveformMutex;
    std::shared_ptr<const spectral_waveform::waveform_data> m_waveform;
    std::thread m_worker;"""
    replacement = """    mutable std::mutex m_waveformMutex;
    std::shared_ptr<const spectral_waveform::waveform_data> m_waveform;
    metadb_handle_ptr m_currentTrack;
    std::thread m_worker;"""
    return once(text, marker, replacement, "ui current track")


edit("waveform_cache.h", patch_cache_h)
edit("waveform_cache.cpp", patch_cache_cpp)
edit("live_output_capture.h", patch_live_h)
edit("live_output_capture.cpp", patch_live_cpp)
edit("spectral_ui.cpp", patch_ui)
print("Re-analysis patch applied successfully.")
