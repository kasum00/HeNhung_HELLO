#ifndef GUI_THEME_HPP
#define GUI_THEME_HPP

/**
 * @file    GuiTheme.hpp
 * @brief   Central colour and layout theme for all screens.
 *
 * Screens must not hard-code colours; they reference this theme so the look is
 * consistent and adjustable in one place (per the project TouchGFX colour rule).
 * Colours are stored as packed RGB565-compatible values via touchgfx::Color.
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/Color.hpp>
#include <touchgfx/hal/Types.hpp>
#include <gui/common/GuiTypes.hpp>
#include <gui/common/GuiLayout.hpp>

namespace gui
{
/** @brief Named colours and shared layout metrics for the GUI. */
namespace theme
{
/* Backgrounds. */
inline touchgfx::colortype background()   { return touchgfx::Color::getColorFromRGB(18, 20, 26); }
inline touchgfx::colortype surface()      { return touchgfx::Color::getColorFromRGB(30, 34, 44); }
inline touchgfx::colortype surfaceAlt()   { return touchgfx::Color::getColorFromRGB(42, 47, 60); }
inline touchgfx::colortype statusBar()    { return touchgfx::Color::getColorFromRGB(12, 14, 18); }

/* Brand / accents. */
inline touchgfx::colortype primary()      { return touchgfx::Color::getColorFromRGB(48, 122, 232); }
inline touchgfx::colortype primaryDark()  { return touchgfx::Color::getColorFromRGB(32, 88, 176); }

/* Semantic states (paired with text/icons, never colour-only). */
inline touchgfx::colortype ok()           { return touchgfx::Color::getColorFromRGB(52, 184, 108); }
inline touchgfx::colortype warning()      { return touchgfx::Color::getColorFromRGB(226, 176, 46); }
inline touchgfx::colortype error()        { return touchgfx::Color::getColorFromRGB(224, 72, 72); }
inline touchgfx::colortype neutral()      { return touchgfx::Color::getColorFromRGB(120, 130, 148); }

/* Text. */
inline touchgfx::colortype textPrimary()  { return touchgfx::Color::getColorFromRGB(238, 240, 245); }
inline touchgfx::colortype textSecondary(){ return touchgfx::Color::getColorFromRGB(158, 166, 182); }
inline touchgfx::colortype textOnPrimary(){ return touchgfx::Color::getColorFromRGB(255, 255, 255); }

/* Waveform channels. */
inline touchgfx::colortype waveIr()       { return touchgfx::Color::getColorFromRGB(80, 200, 255); }
inline touchgfx::colortype waveRed()      { return touchgfx::Color::getColorFromRGB(240, 96, 96); }
inline touchgfx::colortype waveGrid()     { return touchgfx::Color::getColorFromRGB(46, 52, 66); }
inline touchgfx::colortype wavePeak()     { return touchgfx::Color::getColorFromRGB(255, 214, 80); }

/**
 * @brief Maps a signal-quality band to its semantic colour.
 * @param q Quality band.
 * @return Colour representing @p q.
 */
inline touchgfx::colortype qualityColor(SignalQuality q)
{
    switch (q)
    {
    case SignalQuality::Good: return ok();
    case SignalQuality::Fair: return warning();
    case SignalQuality::Poor: return error();
    default:                  return neutral();
    }
}

/* Shared layout metrics — defined once in GuiLayout.hpp; aliased here so
   existing theme::NAME references keep working. Edit geometry in GuiLayout. */
constexpr int16_t STATUS_BAR_HEIGHT = layout::STATUS_BAR_H;
constexpr int16_t NAV_BAR_HEIGHT    = layout::NAV_BAR_H;
constexpr int16_t CONTENT_TOP       = layout::CONTENT_TOP;
constexpr int16_t CONTENT_HEIGHT    = SCREEN_HEIGHT - layout::STATUS_BAR_H - layout::NAV_BAR_H;
constexpr int16_t MARGIN            = layout::MARGIN;

} // namespace theme
} // namespace gui

#endif // GUI_THEME_HPP
