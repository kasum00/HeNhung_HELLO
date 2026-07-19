#ifndef TEXT_BUTTON_HPP
#define TEXT_BUTTON_HPP

/**
 * @file    TextButton.hpp
 * @brief   Reusable image-free push button (coloured box + centered label).
 *
 * Built from a ClickListener<Box> plus a wildcard TextArea, so no image assets
 * are required. Provides press feedback (colour change) and emits a typed action
 * on release carrying the button id, letting one handler serve many buttons.
 *
 * This is a plain touchgfx::Container subclass (not a Designer custom container);
 * it is constructed in code by the owning view.
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/mixins/ClickListener.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>
#include <touchgfx/hal/Types.hpp>

namespace gui
{
/** @brief A coloured, labelled, touchable button with no image dependency. */
class TextButton : public touchgfx::Container
{
public:
    /** Maximum label length (characters, excluding terminator). */
    static constexpr uint16_t LABEL_CAPACITY = 20U;

    TextButton();

    /**
     * @brief Positions and sizes the button; call once before use.
     * @param x Left, @param y Top, @param width, @param height (pixels).
     */
    void setup(int16_t x, int16_t y, int16_t width, int16_t height);

    /**
     * @brief Sets colours.
     * @param released  Background when idle.
     * @param pressed   Background while pressed.
     * @param textColor Label colour.
     */
    void setColors(touchgfx::colortype released, touchgfx::colortype pressed, touchgfx::colortype textColor);

    /**
     * @brief Sets the label text (ASCII, copied into an internal buffer).
     * @param text Null-terminated ASCII string; truncated to LABEL_CAPACITY.
     */
    void setLabel(const char* text);

    /**
     * @brief Sets an application-defined id passed back on click.
     * @param id Identifier (e.g. a ScreenId cast to int16_t).
     */
    void setId(int16_t id) { buttonId = id; }

    /** @brief Returns the application-defined id. */
    int16_t getId() const { return buttonId; }

    /**
     * @brief Registers the click action (invoked on release).
     * @param callback Callback receiving this button by const reference.
     */
    void setAction(touchgfx::GenericCallback<const TextButton&>& callback)
    {
        action = &callback;
    }

private:
    void handleBackgroundClicked(const touchgfx::Box& box, const touchgfx::ClickEvent& event);

    touchgfx::ClickListener<touchgfx::Box> background;
    touchgfx::TextAreaWithOneWildcard label;
    touchgfx::Unicode::UnicodeChar labelBuffer[LABEL_CAPACITY + 1U];

    touchgfx::Callback<TextButton, const touchgfx::Box&, const touchgfx::ClickEvent&> backgroundClickedCallback;
    touchgfx::GenericCallback<const TextButton&>* action;

    touchgfx::colortype releasedColor;
    touchgfx::colortype pressedColor;
    int16_t buttonId;
};

} // namespace gui

#endif // TEXT_BUTTON_HPP
