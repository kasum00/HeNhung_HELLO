#ifndef GUI_COMMANDS_HPP
#define GUI_COMMANDS_HPP

/**
 * @file    GuiCommands.hpp
 * @brief   Typed commands issued by the GUI toward the data source.
 *
 * User interactions never call a driver or a mock method directly. Instead a
 * view asks its presenter, the presenter asks the model, and the model forwards
 * a typed @ref gui::GuiCommand to the active data provider. In this phase the
 * mock provider consumes the command; later the same command is forwarded to
 * the application without changing any view or presenter code.
 *
 * @ref gui::GuiCommand is plain-old-data: a discriminator plus a small,
 * fully-typed payload. No pointers, no unions, no dynamic allocation.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/GuiTypes.hpp>

namespace gui
{
/** @brief Discriminator for a @ref GuiCommand. */
enum class GuiCommandType : uint8_t
{
    None = 0,
    StartMeasurement,
    StopMeasurement,
    SelectFilter,            /**< payload.filterMode                */
    SetFilterWindow,         /**< payload.filterWindow              */
    SelectScenario,          /**< payload.scenario (test control)   */
    SetDateTime              /**< payload date/time fields          */
};

/**
 * @brief A single typed request from the GUI.
 *
 * Only the payload field named by the command type is meaningful; all others
 * are ignored. Use the factory helpers below to build well-formed commands.
 */
struct GuiCommand
{
    GuiCommandType type;

    /* Payload (only the field relevant to @c type is used). */
    FilterMode filterMode;
    MockScenario scenario;
    uint8_t filterWindow;
    uint8_t minimumSqiPercent;
    uint8_t brightnessPercent;
    bool flag;

    /* SetDateTime payload. */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

/**
 * @brief Builds a parameterless command.
 * @param type Command discriminator.
 * @return A zero-initialized command with the given type.
 */
inline GuiCommand makeCommand(GuiCommandType type)
{
    GuiCommand c{};
    c.type = type;
    return c;
}

/**
 * @brief Builds a filter-selection command.
 * @param mode Filter mode to select.
 * @return The command.
 */
inline GuiCommand makeSelectFilter(FilterMode mode)
{
    GuiCommand c{};
    c.type = GuiCommandType::SelectFilter;
    c.filterMode = mode;
    return c;
}

/**
 * @brief Builds a moving-average window command.
 * @param window Window size N (1..max).
 * @return The command.
 */
inline GuiCommand makeSetFilterWindow(uint8_t window)
{
    GuiCommand c{};
    c.type = GuiCommandType::SetFilterWindow;
    c.filterWindow = window;
    return c;
}

/**
 * @brief Builds a scenario-selection command (test control only).
 * @param scenario Scenario to activate.
 * @return The command.
 */
inline GuiCommand makeSelectScenario(MockScenario scenario)
{
    GuiCommand c{};
    c.type = GuiCommandType::SelectScenario;
    c.scenario = scenario;
    return c;
}

/**
 * @brief Builds a "set date/time" command.
 * @return The command.
 */
inline GuiCommand makeSetDateTime(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t weekday, uint8_t hour, uint8_t minute,
                                  uint8_t second)
{
    GuiCommand c{};
    c.type = GuiCommandType::SetDateTime;
    c.year = year;
    c.month = month;
    c.day = day;
    c.weekday = weekday;
    c.hour = hour;
    c.minute = minute;
    c.second = second;
    return c;
}

} // namespace gui

#endif // GUI_COMMANDS_HPP
