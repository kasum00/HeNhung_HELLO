#ifndef MODEL_HPP
#define MODEL_HPP

/**
 * @file    Model.hpp
 * @brief   MVP Model: single seam between the GUI and its data source.
 *
 * The Model owns the active @ref gui::IGuiDataProvider implementation and is the
 * ONLY place that names a concrete provider. Presenters reach data exclusively
 * through @ref data() (an @ref gui::IGuiDataProvider reference) and send user
 * intent through @ref postCommand(). To move from mock data to real data later,
 * change only the @c provider member type here (e.g. to ApplicationGuiBridge);
 * no presenter or view changes are required.
 *
 * @note  Owner: user (non-generated). Contains no HAL/driver/DSP access; the
 *        provider abstraction is the sole data path.
 */

/* Simulator uses the mock provider (GUI development); the target uses the
   real application bridge (sensor + measurement engine + RTC). */
#ifdef SIMULATOR
#include <gui/common/MockGuiDataProvider.hpp>
#else
#include "application_gui_bridge.hpp"
#endif

class ModelListener;

/** @brief Application data model for the TouchGFX MVP stack. */
class Model
{
public:
    Model();

    /**
     * @brief Binds the presenter of the currently active screen.
     * @param listener Presenter to receive model notifications.
     */
    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    /**
     * @brief Called once per GUI frame; advances the data provider.
     *
     * Runs light work only: it forwards a monotonic frame counter to the
     * provider and returns. No blocking, no allocation, no heavy formatting.
     */
    void tick();

    /**
     * @brief Access to the active data provider (interface, never concrete).
     * @return Reference to the GUI data provider.
     */
    gui::IGuiDataProvider& data()
    {
        return provider;
    }

    /**
     * @brief Forwards a typed user command to the data provider.
     * @param command Command to process.
     */
    void postCommand(const gui::GuiCommand& command)
    {
        provider.postCommand(command);
    }

    /**
     * @brief Returns the monotonic GUI frame counter.
     * @return Number of ticks since construction.
     */
    uint32_t frameCounter() const
    {
        return frame;
    }

    /**
     * @brief Records that the active screen changed (for diagnostics).
     */
    void onScreenTransition()
    {
        provider.notifyScreenTransition();
    }

protected:
    ModelListener* modelListener; /**< Active screen's presenter, or null. */

private:
    /* The concrete provider type is named only here. Both implement
       gui::IGuiDataProvider, so data()/tick()/postCommand() are unchanged. */
#ifdef SIMULATOR
    gui::MockGuiDataProvider provider;
#else
    gui::ApplicationGuiBridge provider;
#endif
    uint32_t frame;               /**< Monotonic GUI frame index. */
};

#endif // MODEL_HPP
