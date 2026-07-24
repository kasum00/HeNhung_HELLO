#ifndef I_GUI_DATA_PROVIDER_HPP
#define I_GUI_DATA_PROVIDER_HPP

/**
 * @file    IGuiDataProvider.hpp
 * @brief   Abstract source of GUI snapshots and sink for GUI commands.
 *
 * This interface is the single seam between the presentation layer and whatever
 * produces its data. During the UI-foundation phase the implementation is
 * @ref gui::MockGuiDataProvider; later it becomes an application-backed bridge.
 * Presenters depend only on this interface (via the Model), never on a concrete
 * provider, so the data source can be swapped without touching any view.
 *
 * Each @c getXxxSnapshot() fills a caller-owned struct and returns true when the
 * snapshot content is meaningful. @c tick() advances the provider's internal
 * time base and simulated state machines; @c postCommand() delivers a user
 * request. Implementations must not allocate dynamically and must not block.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/GuiSnapshots.hpp>
#include <gui/common/GuiCommands.hpp>

namespace gui
{
/** @brief Read/command interface implemented by every GUI data source. */
class IGuiDataProvider
{
public:
    virtual ~IGuiDataProvider() = default;

    /**
     * @brief Advances the provider by one GUI frame.
     * @param frameCounter Monotonic GUI frame index (increments each tick).
     *
     * Called from the model tick (~60 Hz). Implementations use @p frameCounter
     * to drive rate-limited internal updates; they must return quickly and must
     * never block.
     */
    virtual void tick(uint32_t frameCounter) = 0;

    /**
     * @brief Delivers a typed user command to the data source.
     * @param command The command to process.
     */
    virtual void postCommand(const GuiCommand& command) = 0;

    /**
     * @brief Notifies the data source that a screen transition occurred.
     *
     * Used only to maintain a diagnostics counter; has no effect on data.
     */
    virtual void notifyScreenTransition() = 0;

    /**
     * @brief Fills the latest measurement snapshot.
     * @param[out] snapshot Destination struct.
     * @return True when @p snapshot is meaningful.
     */
    virtual bool getMeasurementSnapshot(GuiMeasurementSnapshot& snapshot) = 0;

    /**
     * @brief Fills the latest waveform frame.
     * @param[out] snapshot Destination struct (fixed-size buffers filled in place).
     * @return True when @p snapshot is meaningful.
     */
    virtual bool getWaveformSnapshot(GuiWaveformSnapshot& snapshot) = 0;

    /**
     * @brief Fills a page of measurement history.
     * @param[in]  pageIndex Requested 0-based page.
     * @param[out] snapshot  Destination page struct.
     * @return True when @p snapshot is meaningful (including Empty/error states).
     */
    virtual bool getHistoryPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot) = 0;

    /**
     * @brief Fills the current configuration (active + draft) snapshot.
     * @param[out] snapshot Destination struct.
     * @return True when @p snapshot is meaningful.
     */
    virtual bool getConfigurationSnapshot(GuiConfigurationSnapshot& snapshot) = 0;

    /**
     * @brief Fills static system/hardware information.
     * @param[out] snapshot Destination struct.
     * @return True when @p snapshot is meaningful.
     */
    virtual bool getSystemInfoSnapshot(GuiSystemInfoSnapshot& snapshot) = 0;
};

} // namespace gui

#endif // I_GUI_DATA_PROVIDER_HPP
