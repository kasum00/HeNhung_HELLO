/**
 * @file    HistoryRow.cpp
 * @brief   Implementation of the reusable history record row.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/HistoryRow.hpp>
#include <gui/common/GuiTheme.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>

using namespace touchgfx;

namespace gui
{
HistoryRow::HistoryRow()
{
    line1Buffer[0] = 0;
    line2Buffer[0] = 0;
}

void HistoryRow::setup(int16_t x, int16_t y, int16_t width, int16_t height)
{
    setPosition(x, y, width, height);

    background.setPosition(0, 0, width, height);
    background.setColor(theme::surface());
    add(background);

    line1.setPosition(6, layout::widget::HISTROW_LINE1_Y, static_cast<int16_t>(width - 12),
                      layout::widget::HISTROW_LINE1_H);
    line1.setColor(theme::textPrimary());
    line1.setTypedText(TypedText(T_WCMEDIUMLEFT));
    line1.setWildcard1(line1Buffer);
    add(line1);

    line2.setPosition(6, layout::widget::HISTROW_LINE2_Y, static_cast<int16_t>(width - 12),
                      layout::widget::HISTROW_LINE2_H);
    line2.setColor(theme::textSecondary());
    line2.setTypedText(TypedText(T_WCSMALLLEFT));
    line2.setWildcard1(line2Buffer);
    add(line2);
}

void HistoryRow::setRecord(const GuiHistoryRecord& record)
{
    setVisible(true);

    char buf[32];
    if (record.valid)
    {
        snprintf(buf, sizeof(buf), "%02u/%02u %02u:%02u   %d bpm",
                 record.time.day, record.time.month, record.time.hour, record.time.minute,
                 static_cast<int>(record.bpm + 0.5F));
    }
    else
    {
        snprintf(buf, sizeof(buf), "%02u/%02u %02u:%02u   -- bpm",
                 record.time.day, record.time.month, record.time.hour, record.time.minute);
    }
    Unicode::strncpy(line1Buffer, buf, 28);
    line1.setWildcard1(line1Buffer);

    if (record.valid)
    {
        snprintf(buf, sizeof(buf), "SpO2 %d%%  SQI %d%%  OK",
                 static_cast<int>(record.spo2Percent + 0.5F),
                 static_cast<int>(record.sqiPercent + 0.5F));
        line2.setColor(theme::ok());
    }
    else
    {
        snprintf(buf, sizeof(buf), "SQI %d%%  %s",
                 static_cast<int>(record.sqiPercent + 0.5F), toText(record.invalidReason));
        line2.setColor(theme::warning());
    }
    Unicode::strncpy(line2Buffer, buf, 32);
    line2.setWildcard1(line2Buffer);

    invalidate();
}

void HistoryRow::hideRow()
{
    setVisible(false);
    invalidate();
}

} // namespace gui
