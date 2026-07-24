/**
 * @file    TextButton.cpp
 * @brief   Implementation of the reusable image-free push button.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/TextButton.hpp>
#include <gui/common/GuiLayout.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
namespace
{
constexpr int16_t LABEL_LINE_HEIGHT = layout::widget::BUTTON_LABEL_LINE_H;
}

TextButton::TextButton()
    : background(),
      label(),
      backgroundClickedCallback(this, &TextButton::handleBackgroundClicked),
      action(0),
      releasedColor(Color::getColorFromRGB(48, 122, 232)),
      pressedColor(Color::getColorFromRGB(32, 88, 176)),
      buttonId(0)
{
    labelBuffer[0] = 0;
}

void TextButton::setup(int16_t x, int16_t y, int16_t width, int16_t height)
{
    setPosition(x, y, width, height);

    background.setPosition(0, 0, width, height);
    background.setColor(releasedColor);
    background.setClickAction(backgroundClickedCallback);
    add(background);

    const int16_t labelY = static_cast<int16_t>((height - LABEL_LINE_HEIGHT) / 2);
    label.setPosition(0, labelY, width, LABEL_LINE_HEIGHT);
    label.setColor(Color::getColorFromRGB(255, 255, 255));
    label.setTypedText(TypedText(T_WCDEFAULTCENTER));
    label.setWildcard1(labelBuffer);
    add(label);
}

void TextButton::setColors(colortype released, colortype pressed, colortype textColor)
{
    releasedColor = released;
    pressedColor = pressed;
    background.setColor(releasedColor);
    label.setColor(textColor);
    background.invalidate();
    label.invalidate();
}

void TextButton::setLabel(const char* text)
{
    Unicode::strncpy(labelBuffer, text, LABEL_CAPACITY + 1U);
    labelBuffer[LABEL_CAPACITY] = 0;
    label.setWildcard1(labelBuffer);
    label.invalidate();
}

void TextButton::handleBackgroundClicked(const Box& /*box*/, const ClickEvent& event)
{
    switch (event.getType())
    {
    case ClickEvent::PRESSED:
        background.setColor(pressedColor);
        background.invalidate();
        break;
    case ClickEvent::RELEASED:
        background.setColor(releasedColor);
        background.invalidate();
        if ((action != 0) && action->isValid())
        {
            action->execute(*this);
        }
        break;
    case ClickEvent::CANCEL:
    default:
        background.setColor(releasedColor);
        background.invalidate();
        break;
    }
}

} // namespace gui
