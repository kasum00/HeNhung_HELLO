#ifndef TOP_BAR_HPP
#define TOP_BAR_HPP

/**
 * @file    TopBar.hpp
 * @brief   Reusable top navigation strip: optional back button + screen title.
 *
 * Placed at the top of a screen. When configured with a back target it shows a
 * back button that requests navigation to that screen via the application. This
 * keeps a consistent header across screens without repeating boilerplate.
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>
#include <gui/widgets/TextButton.hpp>
#include <gui/common/ScreenId.hpp>
#include <gui/common/GuiLayout.hpp>

namespace gui
{
/** @brief Top-of-screen header with an optional back button and a title. */
class TopBar : public touchgfx::Container
{
public:
    /** Height of the bar in pixels (see GuiLayout::TOP_BAR_H). */
    static constexpr int16_t HEIGHT = layout::TOP_BAR_H;
    /** Maximum title length (characters). */
    static constexpr uint16_t TITLE_CAPACITY = 20U;

    TopBar();

    /**
     * @brief Builds the bar with a back button.
     * @param title      Screen title (ASCII).
     * @param backTarget Screen to navigate to when back is pressed.
     */
    void setup(const char* title, ScreenId backTarget);

    /**
     * @brief Builds the bar with a title only (no back button).
     * @param title Screen title (ASCII).
     */
    void setupTitleOnly(const char* title);

private:
    void onBack(const TextButton& button);
    void build(const char* title, bool withBack);

    touchgfx::Box bar;
    touchgfx::Box accentLine;
    touchgfx::TextAreaWithOneWildcard titleText;
    touchgfx::Unicode::UnicodeChar titleBuffer[TITLE_CAPACITY + 1U];
    TextButton backButton;
    touchgfx::Callback<TopBar, const TextButton&> backClickedCallback;
    ScreenId backScreen;
};

} // namespace gui

#endif // TOP_BAR_HPP
