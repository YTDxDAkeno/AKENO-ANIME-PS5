// Akeno Anime - Crunchyroll client feasibility prototype for PS5.
// WARNING: This preview does not log in or play Crunchyroll content.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_model.hpp"
#include "controller.hpp"
#include "demo_renderer.hpp"
#include <array>
#include <string_view>

namespace {
using akeno::Capability;
using akeno::Model;
using akeno::Page;
using ps5::demo::Canvas;
using ps5::demo::Color;
Model state{};
akeno::Controller pad{};

void line(Canvas &c, unsigned x, unsigned y, std::string_view label, Capability status) noexcept {
    c.text(x, y, label, 4, Color::white);
    c.text(x + 610, y, akeno::capability_label(status), 4,
           status == Capability::ready ? Color::cyan : Color::yellow);
}

void draw_tabs(Canvas &c) noexcept {
    constexpr std::array<std::string_view, 5> tabs = {
        "START", "KATALOG", "MEINE LISTE", "KONTO", "DIAGNOSE"
    };
    for (unsigned i = 0; i < tabs.size(); ++i) {
        const unsigned x = 110 + i * 345;
        const bool selected = static_cast<unsigned>(state.page) == i;
        c.rectangle(x, 205, 320, 78, selected ? Color::yellow : Color::panel);
        c.text(x + 18, 230, tabs[i], 4, selected ? Color::background : Color::white);
    }
}

void draw_screen(Canvas &c) noexcept {
    pad.poll(state);
    state.status.graphics = Capability::ready; // Only if this render callback is running.
    c.clear(Color::background);
    c.rectangle(0, 0, 1920, 14, Color::yellow);
    c.text(105, 65, "AKENO ANIME", 11, Color::white);
    c.text(105, 158, "CRUNCHYROLL CLIENT - TECH PREVIEW 0.1", 3, Color::cyan);
    draw_tabs(c);
    c.rectangle(110, 330, 1700, 590, Color::panel);
    c.text(150, 370, akeno::page_title(state.page), 7, Color::white);
    c.rectangle(150, 445, 1600, 6, Color::yellow);

    if (state.details) {
        c.text(160, 505, "TECHNISCHE EINSCHRAENKUNG", 5, Color::yellow);
        c.text(160, 585, "CRUNCHYROLL LOGIN UND DRM NICHT INTEGRIERT", 4, Color::white);
        c.text(160, 655, "KEINE VIDEOWIEDERGABE IN DIESEM BUILD", 4, Color::white);
        c.text(160, 790, "X ODER KREIS ZURUECK", 4, Color::cyan);
    } else if (state.page == Page::diagnostics) {
        line(c, 160, 495, "GRAFIK", state.status.graphics);
        line(c, 160, 570, "DUALSENSE", state.status.controller);
        line(c, 160, 645, "KATALOG", state.status.catalog);
        line(c, 160, 720, "LOGIN", state.status.authentication);
        line(c, 160, 795, "DRM VIDEO", state.status.protected_video);
    } else {
        constexpr std::array<std::string_view, 3> labels = {
            "CRUNCHYROLL KATALOG", "MEINE SERIEN", "WIEDERGABE"
        };
        constexpr std::array<std::string_view, 3> info = {
            "LIVE KATALOG NOCH NICHT ANGEBUNDEN",
            "KONTO LOGIN NOCH NICHT ANGEBUNDEN",
            "DRM WIEDERGABE NOCH NICHT VERFUEGBAR"
        };
        for (unsigned i = 0; i < labels.size(); ++i) {
            const unsigned y = 490 + i * 132;
            c.rectangle(150, y, 1610, 108, i == state.focus ? Color::cyan : Color::background);
            c.rectangle(160, y + 10, 1590, 88, Color::panel);
            c.text(190, y + 23, labels[i], 5, Color::white);
            c.text(190, y + 70, info[i], 3, Color::yellow);
        }
    }
    c.text(110, 985, "L1/R1 SEITEN  -  HOCH/RUNTER AUSWAHL  -  X INFOS  -  KREIS ZURUECK", 3, Color::white);
    c.text(1330, 1030, "OFFLINE PROTOTYP - KEIN STREAMING", 2, Color::yellow);
}
} // namespace

int main() {
    const bool controller_opened = pad.open();
    state.set_controller(controller_opened);
    ps5::demo::run_frames(draw_screen, "Akeno Anime technical preview started");
}
