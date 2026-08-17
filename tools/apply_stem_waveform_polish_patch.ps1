$ErrorActionPreference = 'Stop'

$path = Join-Path $PSScriptRoot '..\live_output_capture.cpp'
$source = ([System.IO.File]::ReadAllText($path) -replace "`r`n", "`n")

function Replace-Exact([string]$label, [string]$old, [string]$new) {
    $script:source = $script:source.Replace("`r`n", "`n")
    $old = $old.Replace("`r`n", "`n")
    $new = $new.Replace("`r`n", "`n")
    if (-not $script:source.Contains($old)) {
        throw "${label}: expected source block not found"
    }
    $script:source = $script:source.Replace($old, $new)
}

Replace-Exact 'Invalidate only on actual mode change' @'
void set_selected_mode(int mode, bool invalidate) {
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        g_selectedMode = (mode == 1 || mode == 2) ? mode : 0;
    }
    if (invalidate) invalidate_waveform_windows();
}
'@ @'
void set_selected_mode(int mode, bool invalidate) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_previewMutex);
        const int normalized = (mode == 1 || mode == 2) ? mode : 0;
        changed = normalized != g_selectedMode;
        g_selectedMode = normalized;
    }
    // Playback-time/seek callbacks can report the same mode repeatedly. Avoid
    // throwing away the waveform bitmap unless the selected visual source really
    // changed; progressive preview publication still invalidates on new data.
    if (invalidate && changed) invalidate_waveform_windows();
}
'@

Replace-Exact 'Preload cached stem previews on track start' @'
    void request(metadb_handle_ptr track, int mode, double prioritySeconds) {
        bool newTrack = false;
        bool queueAnalysis = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            newTrack = !same_track(m_track, track);

            if (newTrack) {
                if (m_activeAbort) m_activeAbort->abort();
                ++m_generation;
                m_track = track;
                m_ready = false;
                m_hasRequest = false;
                // The worker being aborted belongs to the old generation. Mark
                // the new generation idle so its request can be queued now; the
                // single worker thread will pick it up after the old one exits.
                m_analysisActive = false;
            }

            m_mode = mode;
            m_prioritySeconds = std::max(0.0, prioritySeconds);

            // Mode changes on the same track do not cancel a dual-stem pass.
            // Both stem previews are being generated together, so the UI can
            // simply select whichever one is requested while work continues.
            if (mode > 0 && mode <= 2 && track.is_valid() &&
                !m_ready && !m_analysisActive && !m_hasRequest) {
                m_hasRequest = true;
                queueAnalysis = true;
            }
        }

        if (newTrack) clear_previews(false);
        set_selected_mode(mode, true);
        if (queueAnalysis) m_cv.notify_one();
    }
'@ @'
    void request(metadb_handle_ptr track, int mode, double prioritySeconds) {
        bool newTrack = false;
        bool queueAnalysis = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            newTrack = !same_track(m_track, track);

            if (newTrack) {
                if (m_activeAbort) m_activeAbort->abort();
                ++m_generation;
                m_track = track;
                m_ready = false;
                m_hasRequest = false;
                m_queuedAllowAnalysis = false;
                // The worker being aborted belongs to the old generation. Mark
                // the new generation idle so its request can be queued now; the
                // single worker thread will pick it up after the old one exits.
                m_analysisActive = false;
            }

            m_mode = mode;
            m_prioritySeconds = std::max(0.0, prioritySeconds);

            const bool wantsStem = mode > 0 && mode <= 2;

            // On every new track, quietly probe the on-disk Vocals/Instrumental
            // waveform caches even while Original is selected. This makes the
            // first visual stem switch on previously analyzed tracks immediate,
            // but deliberately does NOT start a new Spleeter pass just for a
            // listener who stays in Original mode.
            //
            // If a stem is already selected, allow the normal analysis path.
            if (track.is_valid() && !m_ready && !m_analysisActive && !m_hasRequest &&
                (wantsStem || newTrack)) {
                m_hasRequest = true;
                m_queuedAllowAnalysis = wantsStem;
                queueAnalysis = true;
            }
        }

        if (newTrack) clear_previews(false);
        set_selected_mode(mode, true);
        if (queueAnalysis) m_cv.notify_one();
    }
'@

Replace-Exact 'Reset queued analysis policy' @'
    void cancel_keep_preview() {
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
'@ @'
    void cancel_keep_preview() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_queuedAllowAnalysis = false;
        m_analysisActive = false;
        m_ready = false;
    }

    void invalidate_current() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeAbort) m_activeAbort->abort();
        ++m_generation;
        m_hasRequest = false;
        m_queuedAllowAnalysis = false;
        m_analysisActive = false;
        m_ready = false;
    }
'@

Replace-Exact 'Capture cache-probe versus full-analysis request' @'
            double prioritySeconds = 0.0;
            std::uint64_t generation = 0;
            std::shared_ptr<abort_callback_impl> aborter;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stop || m_hasRequest; });
                if (m_stop) return;

                track = m_track;
                prioritySeconds = m_prioritySeconds;
                generation = m_generation;
                m_hasRequest = false;
                m_analysisActive = true;

                aborter = std::make_shared<abort_callback_impl>();
'@ @'
            double prioritySeconds = 0.0;
            std::uint64_t generation = 0;
            bool allowAnalysis = false;
            std::shared_ptr<abort_callback_impl> aborter;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stop || m_hasRequest; });
                if (m_stop) return;

                track = m_track;
                prioritySeconds = m_prioritySeconds;
                generation = m_generation;
                allowAnalysis = m_queuedAllowAnalysis;
                m_hasRequest = false;
                m_queuedAllowAnalysis = false;
                m_analysisActive = true;

                aborter = std::make_shared<abort_callback_impl>();
'@

Replace-Exact 'Cache-only probe does not start Spleeter' @'
                    if (haveVocals && haveInstrumental && is_current(generation)) {
                        // Already-cached tracks stay visually instant. Restore the
                        // matching separated PCM in this same background worker so
                        // scrub/reverse is restart-ready without another Spleeter pass.
                        publish_previews(cachedVocals, cachedInstrumental, false);
                        rehydrate_transport_pcm_cache(track, prioritySeconds, *aborter);
                        completed = true;
                    } else {
                        waveform_data original;
'@ @'
                    if (haveVocals && haveInstrumental && is_current(generation)) {
                        // Already-cached tracks stay visually instant. Restore the
                        // matching separated PCM in this same background worker so
                        // scrub/reverse is restart-ready without another Spleeter pass.
                        publish_previews(cachedVocals, cachedInstrumental, false);
                        rehydrate_transport_pcm_cache(track, prioritySeconds, *aborter);
                        completed = true;
                    } else if (allowAnalysis) {
                        waveform_data original;
'@

Replace-Exact 'Promote cache probe if user requests stem mid-probe' @'
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (generation == m_generation) {
                    m_analysisActive = false;
                    if (completed) m_ready = true;
                }
                if (m_activeAbort == aborter) m_activeAbort.reset();
            }
'@ @'
            bool queueFollowup = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (generation == m_generation) {
                    m_analysisActive = false;
                    if (completed) {
                        m_ready = true;
                    } else if (!allowAnalysis && m_mode > 0 && m_mode <= 2 && !m_hasRequest) {
                        // The user can select Vocals/Instrumental while a cheap
                        // cache-only probe is still running. Promote that same
                        // generation to one real dual-stem analysis pass as soon as
                        // the probe finishes, rather than losing the request.
                        m_hasRequest = true;
                        m_queuedAllowAnalysis = true;
                        queueFollowup = true;
                    }
                }
                if (m_activeAbort == aborter) m_activeAbort.reset();
            }
            if (queueFollowup) m_cv.notify_one();
'@

Replace-Exact 'Add queued analysis policy state' @'
    bool m_stop = false;
    bool m_hasRequest = false;
    bool m_analysisActive = false;
'@ @'
    bool m_stop = false;
    bool m_hasRequest = false;
    bool m_queuedAllowAnalysis = false;
    bool m_analysisActive = false;
'@

[System.IO.File]::WriteAllText(
    $path,
    $source,
    [System.Text.UTF8Encoding]::new($false))

Write-Host 'Applied stem waveform preload/switch polish patch.'
