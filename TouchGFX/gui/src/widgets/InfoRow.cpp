/**
 * @file    InfoRow.cpp
 * @brief   Implementation of the reusable label/value row.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/InfoRow.hpp>
#include <gui/common/GuiTheme.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
InfoRow::InfoRow()
{
    labelBuffer[0] = 0;
    valueBuffer[0] = 0;
}

void InfoRow::setup(int16_t x, int16_t y, int16_t width, int16_t height, const char* label)
{
    setPosition(x, y, width, height);

    /* Even split works with the 14px Medium font for both captions and values;
       tune the ratio in GuiLayout (INFOROW_LABEL_NUM / _DEN). */
    const int16_t half = static_cast<int16_t>(
        (width * layout::widget::INFOROW_LABEL_NUM) / layout::widget::INFOROW_LABEL_DEN);
    labelText.setPosition(0, 0, half, height);
    labelText.setColor(theme::textSecondary());
    labelText.setTypedText(TypedText(T_WCMEDIUMLEFT));
    Unicode::strncpy(labelBuffer, label, LABEL_CAPACITY + 1U);
    labelBuffer[LABEL_CAPACITY] = 0;
    labelText.setWildcard1(labelBuffer);
    add(labelText);

    valueText.setPosition(half, 0, static_cast<int16_t>(width - half), height);
    valueText.setColor(theme::textPrimary());
    valueText.setTypedText(TypedText(T_WCMEDIUMRIGHT));
    valueText.setWildcard1(valueBuffer);
    add(valueText);
}

void InfoRow::setLabelText(const char* label)
{
    Unicode::strncpy(labelBuffer, label, LABEL_CAPACITY + 1U);
    labelBuffer[LABEL_CAPACITY] = 0;
    labelText.setWildcard1(labelBuffer);
    labelText.invalidate();
}

void InfoRow::setValueText(const char* value)
{
    Unicode::strncpy(valueBuffer, value, VALUE_CAPACITY + 1U);
    valueBuffer[VALUE_CAPACITY] = 0;
    valueText.setWildcard1(valueBuffer);
    valueText.invalidate();
}

void InfoRow::setValueColor(colortype color)
{
    valueText.setColor(color);
    valueText.invalidate();
}

} // namespace gui
