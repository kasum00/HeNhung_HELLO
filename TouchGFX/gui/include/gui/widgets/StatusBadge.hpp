#ifndef STATUS_BADGE_HPP
#define STATUS_BADGE_HPP

/**
 * @file    StatusBadge.hpp
 * @brief   Reusable status indicator combining colour AND text.
 *
 * A coloured pill with a text label. State is always conveyed by both colour and
 * words, so meaning survives even if colour is misread (per the project rule that
 * status must never be colour-only).
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/hal/Types.hpp>

namespace gui
{
/** @brief Coloured pill + label status indicator. */
class StatusBadge : public touchgfx::Container
{
public:
    /** Maximum label length (characters). */
    static constexpr uint16_t CAPACITY = 22U;

    StatusBadge();

    /**
     * @brief Positions and initializes the badge.
     * @param x,y,width,height Geometry in pixels.
     */
    void setup(int16_t x, int16_t y, int16_t width, int16_t height);

    /**
     * @brief Sets the badge colour and label together.
     * @param color Pill colour.
     * @param text  Label (ASCII).
     */
    void set(touchgfx::colortype color, const char* text);

private:
    touchgfx::Box background;
    touchgfx::TextAreaWithOneWildcard labelText;
    touchgfx::Unicode::UnicodeChar labelBuffer[CAPACITY + 1U];
};

} // namespace gui

#endif // STATUS_BADGE_HPP
