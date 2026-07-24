/**
 * @file    FrontendApplication.cpp
 * @brief   Model tick and centralized, deferred screen navigation.
 * @note    Owner: user (non-generated).
 */

#include <gui/common/FrontendApplication.hpp>
#include <gui/common/FrontendHeap.hpp>
#include <touchgfx/transitions/NoTransition.hpp>

#include <gui/boot_screen/BootView.hpp>
#include <gui/boot_screen/BootPresenter.hpp>
#include <gui/home_screen/HomeView.hpp>
#include <gui/home_screen/HomePresenter.hpp>
#include <gui/dashboard_screen/DashboardView.hpp>
#include <gui/dashboard_screen/DashboardPresenter.hpp>
#include <gui/waveform_screen/WaveformView.hpp>
#include <gui/waveform_screen/WaveformPresenter.hpp>
#include <gui/history_screen/HistoryView.hpp>
#include <gui/history_screen/HistoryPresenter.hpp>
#include <gui/settings_screen/SettingsView.hpp>
#include <gui/settings_screen/SettingsPresenter.hpp>
#include <gui/about_screen/AboutView.hpp>
#include <gui/about_screen/AboutPresenter.hpp>
#include <gui/datetimesettings_screen/DateTimeSettingsView.hpp>
#include <gui/datetimesettings_screen/DateTimeSettingsPresenter.hpp>

#ifndef SIMULATOR
/* Nút vật lý và telemetry chỉ tồn tại trên target (lớp App/Services). */
#include "physical_input_service.h"
#include "telemetry_service.h"

namespace
{
/** Ánh xạ ScreenId của GUI sang enum màn hình dùng cho telemetry. */
ApplicationScreen toAppScreen(gui::ScreenId id)
{
    switch (id)
    {
    case gui::ScreenId::Boot:             return APP_SCREEN_BOOT;
    case gui::ScreenId::Home:             return APP_SCREEN_HOME;
    case gui::ScreenId::Dashboard:        return APP_SCREEN_DASHBOARD;
    case gui::ScreenId::Waveform:         return APP_SCREEN_WAVEFORM;
    case gui::ScreenId::History:          return APP_SCREEN_HISTORY;
    case gui::ScreenId::Settings:         return APP_SCREEN_SETTINGS;
    case gui::ScreenId::DateTimeSettings: return APP_SCREEN_DATETIME_SETTINGS;
    case gui::ScreenId::About:            return APP_SCREEN_ABOUT;
    default:                              return APP_SCREEN_UNKNOWN;
    }
}
} // namespace
#endif

FrontendApplication::FrontendApplication(Model& m, FrontendHeap& heap)
    : FrontendApplicationBase(m, heap),
      pendingScreen(gui::ScreenId::None),
      activeScreenId(gui::ScreenId::Boot)   /* màn hình khởi động */
{
}

#ifndef SIMULATOR
void FrontendApplication::handlePhysicalButton()
{
    PhysicalInputEvent ev = PHYSICAL_INPUT_EVENT_NONE;
    if (!PhysicalInput_GetEvent(&ev) || (ev != PHYSICAL_INPUT_EVENT_B1_PRESSED))
    {
        return;
    }

    (void)Telemetry_PublishUserAction(TELEMETRY_ACTION_BUTTON_B1,
                                      toAppScreen(activeScreenId));

    /* Không xếp chồng yêu cầu: bỏ qua khi một transition chưa hoàn thành. */
    if (pendingScreen != gui::ScreenId::None)
    {
        return;
    }

    /* B1 CHỈ chuyển qua lại Dashboard <-> Waveform; màn hình khác bỏ qua. */
    if (activeScreenId == gui::ScreenId::Dashboard)
    {
        requestScreen(gui::ScreenId::Waveform);
    }
    else if (activeScreenId == gui::ScreenId::Waveform)
    {
        requestScreen(gui::ScreenId::Dashboard);
    }
    else
    {
        (void)Telemetry_PublishUserAction(TELEMETRY_ACTION_B1_IGNORED,
                                          toAppScreen(activeScreenId));
    }
}
#endif

void FrontendApplication::handleTickEvent()
{
#ifndef SIMULATOR
    /* Sự kiện nút được xử lý ở đây (luồng GUI), KHÔNG trong ISR EXTI. */
    handlePhysicalButton();
#endif

    if (pendingScreen != gui::ScreenId::None)
    {
        const gui::ScreenId next = pendingScreen;
        const gui::ScreenId prev = activeScreenId;
        pendingScreen = gui::ScreenId::None;
        performTransition(next);
        model.onScreenTransition();
#ifndef SIMULATOR
        /* Phát sau khi transition đã hoàn tất, tránh log màn hình chưa mở. */
        (void)Telemetry_PublishScreenChange(toAppScreen(prev), toAppScreen(activeScreenId));
#else
        (void)prev;
#endif
    }

    model.tick();
    FrontendApplicationBase::handleTickEvent();
}

void FrontendApplication::performTransition(gui::ScreenId id)
{
    /* Each case mirrors the generated gotoXxxScreenNoTransitionImpl() body:
       a NoTransition MVP transition constructed within the frontend heap. */
    switch (id)
    {
    case gui::ScreenId::Boot:
        touchgfx::makeTransition<BootView, BootPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::Home:
        touchgfx::makeTransition<HomeView, HomePresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::Dashboard:
        touchgfx::makeTransition<DashboardView, DashboardPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::Waveform:
        touchgfx::makeTransition<WaveformView, WaveformPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::History:
        touchgfx::makeTransition<HistoryView, HistoryPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::Settings:
        touchgfx::makeTransition<SettingsView, SettingsPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::About:
        touchgfx::makeTransition<AboutView, AboutPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::DateTimeSettings:
        touchgfx::makeTransition<DateTimeSettingsView, DateTimeSettingsPresenter, touchgfx::NoTransition, Model>(
            &currentScreen, &currentPresenter, frontendHeap, &currentTransition, &model);
        break;
    case gui::ScreenId::None:
    default:
        return;   /* không đổi màn hình -> giữ nguyên activeScreenId */
    }

    activeScreenId = id;
}
