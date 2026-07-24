/**
 * @file    PlaceholderContent.cpp
 * @brief   Implementation of the reusable placeholder screen content.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/PlaceholderContent.hpp>
#include <gui/common/GuiTheme.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
PlaceholderContent::PlaceholderContent()
{
    noteBuffer[0] = 0;
}

void PlaceholderContent::setup(const char* title, const char* note)
{
    setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    noteText.setPosition(10, layout::widget::PLACEHOLDER_NOTE_Y, SCREEN_WIDTH - 20,
                         layout::widget::PLACEHOLDER_NOTE_H);
    noteText.setColor(theme::textSecondary());
    noteText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    Unicode::strncpy(noteBuffer, note, 40);
    noteBuffer[39] = 0;
    noteText.setWildcard1(noteBuffer);
    add(noteText);

    topBar.setup(title, ScreenId::Home);
    add(topBar);
}

} // namespace gui
