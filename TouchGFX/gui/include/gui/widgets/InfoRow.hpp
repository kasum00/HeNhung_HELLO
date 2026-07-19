#ifndef INFO_ROW_HPP
#define INFO_ROW_HPP

/**
 * @file    InfoRow.hpp
 * @brief   Reusable label-left / value-right data row.
 *
 * Used by the DSP, diagnostics, calibration and about screens to present a
 * caption and its value on one line. Values are set as ASCII; the caller formats
 * numbers (using integer formatting to stay newlib-nano friendly).
 *
 * @note  Owner: user (non-generated).
 */

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/hal/Types.hpp>

namespace gui
{
/** @brief A one-line caption + value row. */
class InfoRow : public touchgfx::Container
{
public:
    static constexpr uint16_t LABEL_CAPACITY = 22U;
    static constexpr uint16_t VALUE_CAPACITY = 18U;

    InfoRow();

    /**
     * @brief Positions the row and sets the (static) caption.
     * @param x,y,width,height Geometry in pixels.
     * @param label Caption text (ASCII).
     */
    void setup(int16_t x, int16_t y, int16_t width, int16_t height, const char* label);

    /**
     * @brief Updates the caption text (ASCII); useful for reused row pools.
     * @param label Null-terminated ASCII caption.
     */
    void setLabelText(const char* label);

    /**
     * @brief Sets the value text (ASCII).
     * @param value Null-terminated ASCII value.
     */
    void setValueText(const char* value);

    /**
     * @brief Sets the value colour.
     * @param color Colour for the value text.
     */
    void setValueColor(touchgfx::colortype color);

private:
    touchgfx::TextAreaWithOneWildcard labelText;
    touchgfx::TextAreaWithOneWildcard valueText;
    touchgfx::Unicode::UnicodeChar labelBuffer[LABEL_CAPACITY + 1U];
    touchgfx::Unicode::UnicodeChar valueBuffer[VALUE_CAPACITY + 1U];
};

} // namespace gui

#endif // INFO_ROW_HPP
