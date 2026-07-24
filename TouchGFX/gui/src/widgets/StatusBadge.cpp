/**
 * @file    StatusBadge.cpp
 * @brief   Implementation of the reusable status badge.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/StatusBadge.hpp>
#include <gui/common/GuiTheme.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
StatusBadge::StatusBadge()
{
    labelBuffer[0] = 0;
}

void StatusBadge::setup(int16_t x, int16_t y, int16_t width, int16_t height)
{
    setPosition(x, y, width, height);

    background.setPosition(0, 0, width, height);
    background.setColor(theme::neutral());
    add(background);

    const int16_t textY = static_cast<int16_t>((height - layout::widget::BADGE_TEXT_H) / 2);
    labelText.setPosition(0, textY, width, layout::widget::BADGE_TEXT_H);
    labelText.setColor(theme::textOnPrimary());
    labelText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    labelText.setWildcard1(labelBuffer);
    add(labelText);
}

void StatusBadge::set(colortype color, const char* text)
{
    background.setColor(color);
    background.invalidate();
    Unicode::strncpy(labelBuffer, text, CAPACITY + 1U);
    labelBuffer[CAPACITY] = 0;
    labelText.setWildcard1(labelBuffer);
    labelText.invalidate();
}

} // namespace gui
