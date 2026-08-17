$ErrorActionPreference = 'Stop'

function Read-Normalized([string]$path) {
    return ([System.IO.File]::ReadAllText($path) -replace "`r`n", "`n")
}

function Write-Normalized([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText(
        $path,
        $text,
        [System.Text.UTF8Encoding]::new($false))
}

function Replace-Exact([string]$label, [string]$path, [string]$old, [string]$new) {
    $text = Read-Normalized $path
    $old = $old.Replace("`r`n", "`n")
    $new = $new.Replace("`r`n", "`n")
    if (-not $text.Contains($old)) {
        throw "${label}: expected source block not found in $path"
    }
    $text = $text.Replace($old, $new)
    Write-Normalized $path $text
}

Replace-Exact 'Preserve relative band dominance' 'spectral_analyzer.cpp' @'
    const double total = low + mid + high + 1.0e-20;
    const double low_n = std::min(1.0, (low / total) * 3.0);
    const double mid_n = std::min(1.0, (mid / total) * 3.0);
    const double high_n = std::min(1.0, (high / total) * 3.0);
    const double rms = count > 0 ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0;

    waveform_point p;
    p.peak = static_cast<std::uint16_t>(std::lround(std::min(1.0f, peak) * 65535.0f));
    p.rms = static_cast<std::uint16_t>(std::lround(std::min(1.0, rms) * 65535.0));
    p.bass = compress_energy(low_n);
    p.mids = compress_energy(mid_n);
    p.treble = compress_energy(high_n);
    m_points.push_back(p);
'@ @'
    const double total = low + mid + high + 1.0e-20;

    // Preserve RELATIVE spectral dominance instead of independently boosting
    // every active band toward 255. The old *3 + clamp + log compression made
    // ordinary full-range music look as if bass, mids and treble were all nearly
    // maximal at once, leaving the palette too little information to distinguish
    // kicks, vocals and cymbals.
    //
    // Mild perceptual compensation keeps the broad mid band from owning almost
    // every frame and gives short high-frequency transients enough weight to be
    // visible. After compensation the three stored values are normalized shares;
    // together they describe one spectral mixture rather than three loudnesses.
    const double low_share = low / total;
    const double mid_share = mid / total;
    const double high_share = high / total;

    const double low_weight = 1.15 * std::pow(low_share, 0.85);
    const double mid_weight = 0.92 * std::pow(mid_share, 0.90);
    const double high_weight = 1.30 * std::pow(high_share, 0.78);
    const double weight_total = low_weight + mid_weight + high_weight + 1.0e-20;

    const auto encode_share = [weight_total](double value) -> std::uint8_t {
        const double normalized = std::clamp(value / weight_total, 0.0, 1.0);
        return static_cast<std::uint8_t>(std::lround(normalized * 255.0));
    };

    const double rms = count > 0 ? std::sqrt(sumSquares / static_cast<double>(count)) : 0.0;

    waveform_point p;
    p.peak = static_cast<std::uint16_t>(std::lround(std::min(1.0f, peak) * 65535.0f));
    p.rms = static_cast<std::uint16_t>(std::lround(std::min(1.0, rms) * 65535.0));
    p.bass = encode_share(low_weight);
    p.mids = encode_share(mid_weight);
    p.treble = encode_share(high_weight);
    m_points.push_back(p);
'@

Replace-Exact 'Stopped mouse-wheel zoom repaint' 'spectral_ui.cpp' @'
            m_viewSpan = newSpan;
            m_viewStart = anchor - cursorX * m_viewSpan;
            clamp_view();
            m_followPlayhead = true;
            SetFocus(m_wnd);
            recenter_on_playhead();
            return;
'@ @'
            m_viewSpan = newSpan;
            m_viewStart = anchor - cursorX * m_viewSpan;
            clamp_view();
            SetFocus(m_wnd);

            // A stopped track has no authoritative playhead. Previously we still
            // called recenter_on_playhead(), which returned without invalidating,
            // so the zoom state changed internally but the display did not repaint
            // until Play was pressed. When playback is stopped, keep the mouse
            // cursor as the zoom anchor and repaint immediately. During playback
            // retain the existing centered-follow behavior.
            double playPosition = 0.0;
            if (playback_fraction(playPosition)) {
                m_followPlayhead = true;
                recenter_on_playhead();
            } else {
                invalidate_all();
            }
            return;
'@

Replace-Exact 'Waveform spectral cache revision constant' 'waveform_cache.cpp' @'
static constexpr std::uint32_t kCacheVersion = 1;
static constexpr std::uint32_t kStemCacheRevision = 2;
'@ @'
static constexpr std::uint32_t kCacheVersion = 1;
// Bump when the analyzer changes the meaning of bass/mids/treble. This changes
// only waveform cache filenames; transport PCM uses its own preserved hash below.
static constexpr std::uint32_t kSpectralCacheRevision = 2;
static constexpr std::uint32_t kStemCacheRevision = 2;
'@

Replace-Exact 'Waveform cache hash includes analyzer revision' 'waveform_cache.cpp' @'
std::uint64_t cache_hash_for_track(metadb_handle_ptr track, int mode) {
    auto hash = track_hash(track);
    if (mode == 1 || mode == 2) {
        // Stem waveform revision is independent of the Original waveform cache.
        // Bump this whenever stem-generation semantics change so old stem-only
        // graphics are not silently reused while Original remains cached.
        hash = fnv1a64(&kStemCacheRevision, sizeof(kStemCacheRevision), hash);
    }
    return hash;
}
'@ @'
std::uint64_t cache_hash_for_track(metadb_handle_ptr track, int mode) {
    auto hash = track_hash(track);
    // Analyzer V2 stores normalized spectral shares instead of three independently
    // saturated bands. Give those values a fresh waveform cache namespace so old
    // V1 graphics cannot be silently reused after installing the component.
    hash = fnv1a64(&kSpectralCacheRevision, sizeof(kSpectralCacheRevision), hash);
    if (mode == 1 || mode == 2) {
        // Stem waveform revision is independent of the Original waveform cache.
        // Bump this whenever stem-generation semantics change so old stem-only
        // graphics are not silently reused while Original remains cached.
        hash = fnv1a64(&kStemCacheRevision, sizeof(kStemCacheRevision), hash);
    }
    return hash;
}
'@

Replace-Exact 'Preserve existing transport PCM cache hash' 'waveform_cache.cpp' @'
std::uint64_t transport_hash_for_track(metadb_handle_ptr track) {
    auto hash = cache_hash_for_track(track, 1);
    hash = fnv1a64(&kTransportCacheVersion, sizeof(kTransportCacheVersion), hash);
    hash = fnv1a64(&kTransportCacheRevision, sizeof(kTransportCacheRevision), hash);
    return hash;
}
'@ @'
std::uint64_t transport_hash_for_track(metadb_handle_ptr track) {
    // Keep the transport PCM namespace byte-for-byte compatible with the old
    // calculation. Spectral analyzer revisions affect waveform colors only and
    // must not throw away already generated stem transport PCM.
    auto hash = track_hash(track);
    hash = fnv1a64(&kStemCacheRevision, sizeof(kStemCacheRevision), hash);
    hash = fnv1a64(&kTransportCacheVersion, sizeof(kTransportCacheVersion), hash);
    hash = fnv1a64(&kTransportCacheRevision, sizeof(kTransportCacheRevision), hash);
    return hash;
}
'@

Replace-Exact 'Update palette analyzer comment' 'spectral_palette.cpp' @'
    // The analyzer deliberately log-compresses each band so quiet spectral
    // detail survives in the cache. That is useful for analysis, but a direct
    // linear RGB mix makes most full-range music converge toward the same
    // pastel color. Re-expand the differences here so the dominant frequency
    // region is immediately obvious, like a DJ waveform.
'@ @'
    // Analyzer V2 stores normalized spectral shares, so these values now carry
    // real bass/mid/treble dominance instead of three independently saturated
    // loudness values. Add one more contrast curve at draw time so the dominant
    // frequency region is immediately obvious, like a DJ waveform.
'@

Write-Host 'Applied spectral V2 analyzer + stopped-zoom patch.'
