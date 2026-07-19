#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

/**
 * @file    FrontendApplication.hpp
 * @brief   User-owned application object: model tick + centralized navigation.
 *
 * Extends the generated FrontendApplicationBase. It ticks the Model each frame
 * and performs deferred screen transitions requested by views. Navigation is
 * centralized here (not spread across views) and executed at tick time, which
 * is the safe point to destroy the current view and construct the next one.
 *
 * @note  Owner: user (non-generated). Regeneration re-creates only the *Base
 *        class; this file is preserved.
 */

#include <gui_generated/common/FrontendApplicationBase.hpp>
#include <gui/common/ScreenId.hpp>

class FrontendHeap;

using namespace touchgfx;

/** @brief Application front-end with model tick and screen navigation. */
class FrontendApplication : public FrontendApplicationBase
{
public:
    FrontendApplication(Model& m, FrontendHeap& heap);
    virtual ~FrontendApplication() { }

    /**
     * @brief Requests navigation to a screen (performed on the next tick).
     * @param id Target screen.
     *
     * Safe to call from a view/presenter event handler. The transition itself
     * runs in @ref handleTickEvent(), never mid-event.
     */
    void requestScreen(gui::ScreenId id)
    {
        pendingScreen = id;
    }

    /**
     * @brief The screen currently shown.
     *
     * Tracked here so physical-button navigation can decide what to toggle and
     * so screen-change telemetry reports a transition only after it completed.
     */
    gui::ScreenId activeScreen() const
    {
        return activeScreenId;
    }

    /** @brief Per-frame tick: applies a pending transition, then ticks the model. */
    virtual void handleTickEvent();

private:
    void performTransition(gui::ScreenId id);

#ifndef SIMULATOR
    /** @brief Polls the B1 button and toggles Dashboard/Waveform (target only). */
    void handlePhysicalButton();
#endif

    gui::ScreenId pendingScreen;
    gui::ScreenId activeScreenId;
};

#endif // FRONTENDAPPLICATION_HPP
