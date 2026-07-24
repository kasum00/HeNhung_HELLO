/**
 * @file    MetricCard.cpp
 * @brief   Implementation of the reusable metric card.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/MetricCard.hpp>
#include <gui/common/GuiTheme.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
MetricCard::MetricCard()
{
    captionBuffer[0] = 0;
    valueBuffer[0] = 0;
    unitBuffer[0] = 0;
}

void MetricCard::setup(int16_t x, int16_t y, int16_t width, int16_t height,
                       const char* caption, const char* unit)
{
    setPosition(x, y, width, height);

    background.setPosition(0, 0, width, height);
    background.setColor(theme::surface());
    add(background);

    /* Coloured accent strip on top of the card. */
    accentLine.setPosition(0, 0, width, layout::widget::CARD_ACCENT_H);
    accentLine.setColor(theme::primary());
    add(accentLine);

    captionText.setPosition(0, layout::widget::CARD_CAPTION_Y, width, layout::widget::CARD_CAPTION_H);
    captionText.setColor(theme::textSecondary());
    captionText.setTypedText(TypedText(T_WCSMALLCENTER));
    Unicode::strncpy(captionBuffer, caption, 10);
    captionBuffer[9] = 0;
    captionText.setWildcard1(captionBuffer);
    add(captionText);

    valueText.setPosition(0, layout::widget::CARD_VALUE_Y, width, layout::widget::CARD_VALUE_H);
    valueText.setColor(theme::textPrimary());
    valueText.setTypedText(TypedText(T_WCLARGECENTER));
    Unicode::strncpy(valueBuffer, "--", 8);
    valueText.setWildcard1(valueBuffer);
    add(valueText);

    unitText.setPosition(0, static_cast<int16_t>(height - layout::widget::CARD_UNIT_H),
                         width, layout::widget::CARD_UNIT_H);
    unitText.setColor(theme::textSecondary());
    unitText.setTypedText(TypedText(T_WCSMALLCENTER));
    Unicode::strncpy(unitBuffer, unit, 6);
    unitBuffer[5] = 0;
    unitText.setWildcard1(unitBuffer);
    add(unitText);
}

void MetricCard::setAccentColor(colortype color)
{
    accentLine.setColor(color);
    accentLine.invalidate();
}

void MetricCard::setValue(int32_t value, bool valid)
{
    if (valid)
    {
        Unicode::snprintf(valueBuffer, 8, "%d", value);
    }
    else
    {
        Unicode::strncpy(valueBuffer, "--", 8);
    }
    valueText.setWildcard1(valueBuffer);
    valueText.invalidate();
}

void MetricCard::setValueColor(colortype color)
{
    valueText.setColor(color);
    valueText.invalidate();
}

} // namespace gui
