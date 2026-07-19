#ifndef MOCK_CLOCK_HPP
#define MOCK_CLOCK_HPP

/**
 * @file    MockClock.hpp
 * @brief   Monotonic mock wall-clock for the UI-foundation phase.
 *
 * Produces a @ref gui::GuiTime that advances one second at a time from a fixed
 * start date. It never touches DS1307 or the HAL RTC; it is driven purely by GUI
 * frame ticks. Later this is replaced by an RtcService behind the same snapshot,
 * with no change to any view.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/GuiSnapshots.hpp>

namespace gui
{
/** @brief Advances a GuiTime deterministically from GUI ticks. */
class MockClock
{
public:
    MockClock()
        : hour(9U), minute(41U), second(0U),
          day(17U), month(7U), year(2026U)
    {
    }

    /**
     * @brief Advances the clock by one second.
     *
     * Call once per simulated second (i.e. gated by the 1 Hz frame divisor).
     * Day/month roll-over uses a fixed 30-day month, which is sufficient for a
     * mock and avoids a full calendar implementation.
     */
    void advanceOneSecond()
    {
        if (++second < 60U)
        {
            return;
        }
        second = 0U;
        if (++minute < 60U)
        {
            return;
        }
        minute = 0U;
        if (++hour < 24U)
        {
            return;
        }
        hour = 0U;
        if (++day <= 30U)
        {
            return;
        }
        day = 1U;
        if (++month <= 12U)
        {
            return;
        }
        month = 1U;
        ++year;
    }

    /**
     * @brief Sets the mock clock (used by the SetDateTime command in the sim).
     */
    void set(uint8_t h, uint8_t m, uint8_t s, uint8_t d, uint8_t mon, uint16_t y)
    {
        hour = h; minute = m; second = s;
        day = d; month = mon; year = y;
    }

    /**
     * @brief Returns the current mock time.
     * @return A fully populated GuiTime value.
     */
    GuiTime now() const
    {
        GuiTime t;
        t.hour = hour;
        t.minute = minute;
        t.second = second;
        t.day = day;
        t.month = month;
        t.year = year;
        return t;
    }

private:
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

} // namespace gui

#endif // MOCK_CLOCK_HPP
