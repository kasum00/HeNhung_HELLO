#ifndef PLACEHOLDER_CONTENT_HPP
#define PLACEHOLDER_CONTENT_HPP

/**
 * @file    PlaceholderContent.hpp
 * @brief   Full-screen placeholder: background + TopBar (back) + centered note.
 *
 * Used by screens that are declared and navigable but whose detailed content is
 * implemented in a later phase step. Guarantees every screen is reachable and
 * returns to Home, so navigation can be verified before each screen is built.
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <gui/widgets/TopBar.hpp>

namespace gui
{
/** @brief Reusable "screen not yet implemented" content with back navigation. */
class PlaceholderContent : public touchgfx::Container
{
public:
    PlaceholderContent();

    /**
     * @brief Builds the placeholder.
     * @param title Screen title shown in the top bar.
     * @param note  Centered explanatory note (ASCII).
     */
    void setup(const char* title, const char* note);

private:
    touchgfx::Box background;
    TopBar topBar;
    touchgfx::TextAreaWithOneWildcard noteText;
    touchgfx::Unicode::UnicodeChar noteBuffer[40];
};

} // namespace gui

#endif // PLACEHOLDER_CONTENT_HPP
