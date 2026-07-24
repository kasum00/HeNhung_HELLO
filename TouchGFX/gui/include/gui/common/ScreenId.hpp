#ifndef SCREEN_ID_HPP
#define SCREEN_ID_HPP

/**
 * @file    ScreenId.hpp
 * @brief   Stable identifiers for every application screen.
 *
 * Views request navigation with a ScreenId; @ref FrontendApplication performs
 * the actual (deferred) MVP transition. This keeps navigation centralized and
 * screens decoupled from each other's concrete View/Presenter types.
 *
 * @note  Owner: user (non-generated).
 */

#include <cstdint>

namespace gui
{
/** @brief Identifies a navigable screen. */
enum class ScreenId : uint8_t
{
    None = 0,
    Boot,
    Home,
    Dashboard,
    Waveform,
    History,
    DateTimeSettings
};

} // namespace gui

#endif // SCREEN_ID_HPP
