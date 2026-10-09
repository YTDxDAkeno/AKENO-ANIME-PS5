// Akeno Anime PS5 - UI with real HTTPS and public HLS manifest diagnostics.
// Copyright (C) 2026 Akeno Anime contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_model.hpp"
#include "controller.hpp"
#include "demo_renderer.hpp"
#include "network_probe.hpp"

#include <array>
#include <cstdio>
#include <string_view>

namespace
{
using akeno::Capability;
using akeno::Model;
using akeno::Page;
using akeno::ProbeStage;
using ps5::demo::Canvas;
using ps5::demo::Color;

Model state{};
akeno::Controller pad{};
akeno::NetworkProbe network{};

void text_row(Canvas &canvas, unsigned y, std::string_view title,
              std::string_view details, bool selected) noexcept
{
    canvas.rectangle(150, y, 1610, 108, selected ? Color::cyan : Color::background);
    canvas.rectangle(160, y + 10, 1590, 88, Color::panel);
    canvas.text(190, y + 23, title, 5, Color::white);
    canvas.text(190, y + 70, details, 3, Color::yellow);
}

void draw_tabs(Canvas &canvas) noexcept
{
    constexpr std::array<std::string_view, 5> tabs = {
        "START", "KATALOG", "MEINE LISTE", "KONTO", "DIAGNOSE"};
    for (unsigned i = 0; i < tabs.size(); ++i)
    {
        const unsigned x = 110 + i * 345;
        const bool selected = static_cast<unsigned>(state.page) == i;
        canvas.rectangle(x, 205, 320, 78, selected ? Color::yellow : Color::panel);
        canvas.text(x + 18, 230, tabs[i], 4,
                    selected ? Color::background : Color::white);
    }
}

void draw_diagnostics(Canvas &canvas) noexcept
{
    const akeno::ProbeSnapshot probe = network.snapshot();
    canvas.text(160, 478, "FIRMWARE 12.20 - X: HTTPS/HLS TEST STARTEN", 4, Color::white);
    canvas.text(160, 546, "HTTPS", 5, Color::white);
    canvas.text(160, 615, "HLS PLAYLIST", 5, Color::white);
    canvas.text(160, 684, "CRUNCHYROLL LOGIN", 5, Color::white);
    canvas.text(160, 753, "DRM VIDEOPLAYER", 5, Color::white);

    canvas.text(850, 684, "NICHT IMPLEMENTIERT", 4, Color::yellow);
    canvas.text(850, 753, "NICHT IMPLEMENTIERT", 4, Color::yellow);

    const char *https = "NICHT GEPRUEFT";
    const char *playlist = "NICHT GEPRUEFT";
    char http_detail[72]{};
    char hls_detail[72]{};

    switch (probe.stage)
    {
    case ProbeStage::idle:
        break;
    case ProbeStage::running:
        https = "TEST LAEUFT ...";
        playlist = "BITTE WARTEN ...";
        break;
    case ProbeStage::thread_error:
        https = "THREAD FEHLER";
        playlist = "KEIN TEST";
        break;
    case ProbeStage::https_error:
        std::snprintf(http_detail, sizeof(http_detail), "FEHLER HTTP %d CURL %d",
                      probe.https_http_code, probe.curl_code);
        https = http_detail;
        playlist = "NICHT ERREICHT";
        break;
    case ProbeStage::playlist_error:
        std::snprintf(http_detail, sizeof(http_detail), "OK HTTP %d", probe.https_http_code);
        std::snprintf(hls_detail, sizeof(hls_detail), "FEHLER HTTP %d CURL %d",
                      probe.hls_http_code, probe.curl_code);
        https = http_detail;
        playlist = hls_detail;
        break;
    case ProbeStage::ready:
        std::snprintf(http_detail, sizeof(http_detail), "OK HTTP %d", probe.https_http_code);
        std::snprintf(hls_detail, sizeof(hls_detail), "OK HTTP %d - %d QUALITAETEN",
                      probe.hls_http_code, probe.variants);
        https = http_detail;
        playlist = hls_detail;
        break;
    }
    const Color first = probe.stage == ProbeStage::ready || probe.stage == ProbeStage::playlist_error
                            ? Color::cyan
                            : Color::yellow;
    canvas.text(850, 546, https, 4, first);
    canvas.text(850, 615, playlist, 4, probe.stage == ProbeStage::ready ? Color::cyan : Color::yellow);
}

void draw_scene(Canvas &canvas) noexcept
{
    pad.poll(state);
    // Diagnostics actions initiate a genuine network request, not a fake success dialog.
    if (state.page == Page::diagnostics && state.details)
    {
        (void)network.start();
        state.details = false;
    }
    state.status.graphics = Capability::ready;

    canvas.clear(Color::background);
    canvas.rectangle(0, 0, 1920, 14, Color::yellow);
    canvas.text(105, 65, "AKENO ANIME", 11, Color::white);
    canvas.text(105, 158, "CRUNCHYROLL CLIENT - TECH PREVIEW 0.2", 3, Color::cyan);
    draw_tabs(canvas);
    canvas.rectangle(110, 330, 1700, 590, Color::panel);
    canvas.text(150, 370, akeno::page_title(state.page), 7, Color::white);
    canvas.rectangle(150, 445, 1600, 6, Color::yellow);

    if (state.details)
    {
        canvas.text(160, 505, "AKTUELLE EINSCHRAENKUNG", 5, Color::yellow);
        canvas.text(160, 585, "CRUNCHYROLL LOGIN UND DRM NOCH NICHT VERFUEGBAR", 4, Color::white);
        canvas.text(160, 655, "NETZTEST UNTER DIAGNOSE MIT X STARTEN", 4, Color::white);
        canvas.text(160, 790, "X ODER KREIS ZURUECK", 4, Color::cyan);
    }
    else if (state.page == Page::diagnostics)
    {
        draw_diagnostics(canvas);
    }
    else
    {
        constexpr std::array<std::string_view, 3> labels = {
            "VERBINDUNG", "HLS TESTSTREAM", "CRUNCHYROLL"};
        constexpr std::array<std::string_view, 3> info = {
            "DIAGNOSE: HTTPS OHNE PSN PRUEFEN",
            "PUBLIC HLS PLAYLIST TESTEN - NOCH KEIN VIDEO",
            "LOGIN UND DRM SIND NOCH NICHT INTEGRIERT"};
        for (unsigned i = 0; i < labels.size(); ++i)
        {
            text_row(canvas, 490 + i * 132, labels[i], info[i], i == state.focus);
        }
    }

    canvas.text(110, 985, "L1/R1 SEITEN  -  HOCH/RUNTER  -  X AUSWAHL  -  KREIS ZURUECK",
                3, Color::white);
    canvas.text(1270, 1030, "HLS DATENTEST - KEINE VIDEOWIEDERGABE", 2, Color::yellow);
}
} // namespace

int main()
{
    state.set_controller(pad.open());
    ps5::demo::run_frames(draw_scene, "Akeno Anime 0.2 started");
}
