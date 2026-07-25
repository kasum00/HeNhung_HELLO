#ifndef GUI_LAYOUT_HPP
#define GUI_LAYOUT_HPP

/**
 * @file    GuiLayout.hpp
 * @brief   Single source of truth for all screen geometry (positions/sizes).
 *
 * Every widget position and size lives here so the whole layout can be tuned in
 * one place without touching screen logic. Values are grouped by common metrics
 * and then per screen. Changing a value here moves the corresponding widget on
 * the next build; no other edits are needed.
 *
 * ---------------------------------------------------------------------------
 * TEXT SIZE (FONT) is NOT set here. It comes from the typographies in
 *   TouchGFX/assets/texts/texts.xml
 * Each role maps to a typography; change its Size= there and regenerate:
 *     Large   = 32 px  -> big metric values (BPM/SpO2/SQI)
 *     Default = 16 px  -> titles, badges, primary labels, buttons
 *     Medium  = 12 px  -> data rows (InfoRow), settings, dense text
 *     Small   = 8 px   -> captions, status bar, secondary info
 * After editing texts.xml, delete generated/texts and generated/fonts, then
 * regenerate so the new glyph sizes are rebuilt.
 * ---------------------------------------------------------------------------
 *
 * @note  Owner: user (non-generated). No TouchGFX dependency (plain constants).
 */

#include <cstdint>

namespace gui
{
namespace layout
{
/** @name Panel */
///@{
constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
///@}

/** @name Shared metrics */
///@{
constexpr int16_t MARGIN        = 6;   /**< Default outer margin.            */
constexpr int16_t STATUS_BAR_H  = 20;  /**< Home status bar height.          */
constexpr int16_t TOP_BAR_H     = 26;  /**< TopBar (back + title) height.    */
constexpr int16_t NAV_BAR_H     = 32;  /**< Reserved bottom-nav height.      */
constexpr int16_t CONTENT_W     = SCREEN_W - (2 * MARGIN);        /**< 228 */
constexpr int16_t CONTENT_TOP   = TOP_BAR_H;                      /**< first row below the bar */
///@}

/** @name Standard control sizes */
///@{
constexpr int16_t BUTTON_H       = 36;  /**< Primary action button.          */
constexpr int16_t SMALL_BUTTON_H = 24;  /**< Compact button (nav, toggles).  */
constexpr int16_t DATA_ROW_H     = 18;  /**< InfoRow default height.         */
///@}

/** @name Reusable widget internals */
namespace widget
{
/* TextButton */
constexpr int16_t BUTTON_LABEL_LINE_H = 20;
/* TopBar */
constexpr int16_t TOPBAR_BACK_X = 4;
constexpr int16_t TOPBAR_BACK_Y = 2;
constexpr int16_t TOPBAR_BACK_W = 44;
constexpr int16_t TOPBAR_TITLE_X_WITH_BACK = 52;
constexpr int16_t TOPBAR_TITLE_X_PLAIN = 6;
constexpr int16_t TOPBAR_TITLE_Y = 4;
constexpr int16_t TOPBAR_TITLE_H = 18;
/* MetricCard */
constexpr int16_t CARD_CAPTION_Y = 4;
constexpr int16_t CARD_CAPTION_H = 10;
constexpr int16_t CARD_VALUE_Y = 14;
constexpr int16_t CARD_VALUE_H = 36;
constexpr int16_t CARD_UNIT_H = 10;   /**< Placed at height - CARD_UNIT_H. */
/* StatusBadge */
constexpr int16_t BADGE_TEXT_H = 18;
/* InfoRow: label gets width * LABEL_NUM / LABEL_DEN, value gets the rest. */
constexpr int16_t INFOROW_LABEL_NUM = 1;
constexpr int16_t INFOROW_LABEL_DEN = 2;
/* HistoryRow */
constexpr int16_t HISTROW_LINE1_Y = 2;
constexpr int16_t HISTROW_LINE1_H = 12;
constexpr int16_t HISTROW_LINE2_Y = 14;
constexpr int16_t HISTROW_LINE2_H = 10;
/* PlaceholderContent */
constexpr int16_t PLACEHOLDER_NOTE_Y = 140;
constexpr int16_t PLACEHOLDER_NOTE_H = 18;
} // namespace widget

/** @name Boot screen */
namespace boot
{
constexpr int16_t LOGO_SIZE   = 56;
constexpr int16_t LOGO_Y      = 60;
constexpr int16_t TITLE_Y     = 126;
constexpr int16_t TITLE_H     = 28;
constexpr int16_t VERSION_Y   = 156;
constexpr int16_t VERSION_H   = 14;
constexpr int16_t BAR_X       = 24;
constexpr int16_t BAR_W       = SCREEN_W - (2 * BAR_X);
constexpr int16_t BAR_Y       = 190;
constexpr int16_t BAR_H       = 8;
constexpr int16_t STATUS_Y    = BAR_Y + 16;
constexpr int16_t STATUS_H    = 16;
} // namespace boot

/** @name Home menu */
namespace home
{
constexpr int16_t STATUS_TEXT_X = 4;
constexpr int16_t STATUS_TEXT_Y = 3;
constexpr int16_t STATUS_TEXT_W = 140;
constexpr int16_t STATUS_TEXT_H = 14;
constexpr int16_t CLOCK_W = 80;
constexpr int16_t CLOCK_Y = 2;
constexpr int16_t CLOCK_H = 16;
constexpr int16_t GRID_TOP  = STATUS_BAR_H + 6;
constexpr int16_t GRID_LEFT = 6;
constexpr int16_t BTN_W = 105;
constexpr int16_t BTN_H = 38;
constexpr int16_t COL_GAP = 6;
constexpr int16_t ROW_GAP = 8;
} // namespace home

/** @name Dashboard */
namespace dashboard
{
constexpr int16_t BADGE_Y = 34;
constexpr int16_t BADGE_H = 26;
constexpr int16_t CARD_Y = 60;
constexpr int16_t CARD_W = 72;
constexpr int16_t CARD_H = 76;
constexpr int16_t CARD_GAP = 4;
constexpr int16_t CARD1_X = 6;
constexpr int16_t CARD2_X = 82;
constexpr int16_t CARD3_X = 162;
constexpr int16_t REASON_Y = 142;
constexpr int16_t REASON_H = 18;
constexpr int16_t INFO_Y = 164;
constexpr int16_t INFO_H = 12;
constexpr int16_t ACTION_Y = 190;
constexpr int16_t ACTION_H = 48;
constexpr int16_t ACTION1_X = 6;
constexpr int16_t ACTION2_X = 124;
constexpr int16_t ACTION_W = 110;
} // namespace dashboard

/** @name Waveform */
namespace waveform
{
constexpr int16_t CTRL_Y = 32;
constexpr int16_t CTRL_H = 26;
constexpr int16_t CHANNEL_X = 6;
constexpr int16_t CHANNEL_W = 68;
constexpr int16_t MODE_X = 78;
constexpr int16_t MODE_W = 86;
constexpr int16_t PLOT_Y = 60;
constexpr int16_t PLOT_H = 168;   /* taller plot: fills down to just above INFO_Y */
constexpr int16_t PLOT_W = SCREEN_W;
constexpr int16_t PEAK_MARKER_W = 2;
constexpr int16_t PEAK_MARKER_H = 10;
constexpr int16_t INFO_Y = 232;
constexpr int16_t INFO_H = 12;
} // namespace waveform

/** @name History */
namespace history
{
constexpr int16_t NAV_Y = 32;
constexpr int16_t NAV_BTN_W = 52;
constexpr int16_t NAV_BTN_H = 26;
constexpr int16_t NEXT_X = 182;
constexpr int16_t PAGE_TEXT_X = 60;
constexpr int16_t PAGE_TEXT_Y = 36;
constexpr int16_t PAGE_TEXT_W = 120;
constexpr int16_t PAGE_TEXT_H = 16;
constexpr int16_t ROWS_TOP = 64;
constexpr int16_t ROW_H = 34;
constexpr int16_t ROW_GAP = 3;
constexpr int16_t STATE_Y = 140;
constexpr int16_t STATE_H = 18;
} // namespace history


/** @name Settings */
namespace settings
{
constexpr int16_t ROW_TOP = 34;
constexpr int16_t ROW_H = 24;
constexpr int16_t LABEL_W = 118;
constexpr int16_t VALUE_X = 128;
constexpr int16_t VALUE_W = 104;
constexpr int16_t VALUE_H = 20;
constexpr int16_t STATUS_Y = 182;
constexpr int16_t STATUS_H = 14;
constexpr int16_t ACTION_Y = 200;
constexpr int16_t ACTION_H = 32;
constexpr int16_t ACTION_W = 72;
constexpr int16_t APPLY_X = 6;
constexpr int16_t CANCEL_X = 84;
constexpr int16_t RESTORE_X = 162;
} // namespace settings

/** @name About */
namespace about
{
constexpr int16_t TITLE_Y = 34;
constexpr int16_t TITLE_H = 20;
constexpr int16_t ROWS_TOP = 58;
constexpr int16_t ROW_H = 20;
constexpr int16_t DISCLAIMER_Y = 210;
constexpr int16_t DISCLAIMER_H = 46;
constexpr int16_t DISCLAIMER_LINE1_Y = 216;
constexpr int16_t DISCLAIMER_LINE2_Y = 234;
constexpr int16_t DISCLAIMER_LINE_H = 16;
} // namespace about

} // namespace layout
} // namespace gui

#endif // GUI_LAYOUT_HPP
