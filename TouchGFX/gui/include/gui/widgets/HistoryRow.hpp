#ifndef HISTORY_ROW_HPP
#define HISTORY_ROW_HPP

/**
 * @file    HistoryRow.hpp
 * @brief   Reusable two-line row rendering one measurement history record.
 *
 * Line 1: date, time and BPM. Line 2: SpO2/SQI plus an OK marker, or the
 * technical invalid reason (in a warning colour) for invalid records.
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <gui/common/GuiSnapshots.hpp>

namespace gui
{
/** @brief A single measurement-history entry rendered on two lines. */
class HistoryRow : public touchgfx::Container
{
public:
    HistoryRow();

    /**
     * @brief Positions and initializes the row.
     * @param x,y,width,height Geometry in pixels.
     */
    void setup(int16_t x, int16_t y, int16_t width, int16_t height);

    /**
     * @brief Fills the row from a record and makes it visible.
     * @param record History record to display.
     */
    void setRecord(const GuiHistoryRecord& record);

    /** @brief Hides the row (unused slot on a partial page). */
    void hideRow();

private:
    touchgfx::Box background;
    touchgfx::TextAreaWithOneWildcard line1;
    touchgfx::TextAreaWithOneWildcard line2;
    touchgfx::Unicode::UnicodeChar line1Buffer[28];
    touchgfx::Unicode::UnicodeChar line2Buffer[32];
};

} // namespace gui

#endif // HISTORY_ROW_HPP
