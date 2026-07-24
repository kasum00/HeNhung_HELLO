#ifndef METRIC_CARD_HPP
#define METRIC_CARD_HPP

/**
 * @file    MetricCard.hpp
 * @brief   Reusable card showing a labelled numeric metric with a unit.
 *
 * Displays a caption (e.g. "BPM"), a large value and an optional unit. The value
 * can be shown as "--" when invalid, so stale or unavailable readings are never
 * presented as if valid.
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
/** @brief A caption + big value + unit card. */
class MetricCard : public touchgfx::Container
{
public:
    MetricCard();

    /**
     * @brief Positions and initializes the card.
     * @param x,y,width,height Geometry in pixels.
     * @param caption Static caption text (ASCII).
     * @param unit    Static unit text (ASCII, may be empty).
     */
    void setup(int16_t x, int16_t y, int16_t width, int16_t height,
               const char* caption, const char* unit);

    /**
     * @brief Sets the accent line colour on top of the card.
     * @param color Colour for the accent strip.
     */
    void setAccentColor(touchgfx::colortype color);

    /**
     * @brief Sets the displayed value from an integer.
     * @param value Numeric value.
     * @param valid When false, shows "--" instead of the number.
     */
    void setValue(int32_t value, bool valid);

    /**
     * @brief Sets the value colour (e.g. to reflect quality/state).
     * @param color Colour for the value text.
     */
    void setValueColor(touchgfx::colortype color);

private:
    touchgfx::Box background;
    touchgfx::Box accentLine;
    touchgfx::TextAreaWithOneWildcard captionText;
    touchgfx::TextAreaWithOneWildcard valueText;
    touchgfx::TextAreaWithOneWildcard unitText;

    touchgfx::Unicode::UnicodeChar captionBuffer[10];
    touchgfx::Unicode::UnicodeChar valueBuffer[8];
    touchgfx::Unicode::UnicodeChar unitBuffer[6];
};

} // namespace gui

#endif // METRIC_CARD_HPP
