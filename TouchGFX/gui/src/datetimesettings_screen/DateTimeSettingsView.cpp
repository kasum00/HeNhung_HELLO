/**
 * @file    DateTimeSettingsView.cpp
 * @brief   RTC date/time settings screen implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/datetimesettings_screen/DateTimeSettingsView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>

using namespace touchgfx;
using namespace gui;

namespace
{
const char* const FIELD_LABELS[DateTimeSettingsView::FIELD_COUNT] =
    { "Year", "Month", "Day", "Hour", "Min", "Sec" };

constexpr int16_t CUR_Y = 38;
constexpr int16_t ROW_TOP = 62;
constexpr int16_t ROW_H = 30;
constexpr int16_t LABEL_X = 6;
constexpr int16_t LABEL_W = 54;
constexpr int16_t MINUS_X = 62;
constexpr int16_t STEP_W = 32;
constexpr int16_t VALUE_X = 98;
constexpr int16_t VALUE_W = 50;
constexpr int16_t PLUS_X = 152;
constexpr int16_t STEP_H = 26;
constexpr uint8_t ID_PLUS_OFFSET = 10U;
constexpr int16_t ID_READ = 100;
constexpr int16_t ID_SET = 101;

bool leapYear(uint16_t y) { return ((y % 400U) == 0U) || (((y % 4U) == 0U) && ((y % 100U) != 0U)); }

uint8_t daysInMonth(uint16_t y, uint8_t m)
{
    static const uint8_t d[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if ((m < 1U) || (m > 12U)) { return 31U; }
    if ((m == 2U) && leapYear(y)) { return 29U; }
    return d[m - 1U];
}

/** ISO weekday (1=Mon..7=Sun) via Zeller's congruence. */
uint8_t weekdayOf(uint16_t y, uint8_t m, uint8_t d)
{
    int mm = m;
    int yy = y;
    if (mm < 3) { mm += 12; yy -= 1; }
    const int K = yy % 100;
    const int J = yy / 100;
    const int h = (d + (13 * (mm + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7; /* 0=Sat */
    static const uint8_t iso[7] = {6U, 7U, 1U, 2U, 3U, 4U, 5U};
    return iso[h];
}
}

DateTimeSettingsView::DateTimeSettingsView()
    : stepClicked(this, &DateTimeSettingsView::onStep),
      readClicked(this, &DateTimeSettingsView::onRead),
      setClicked(this, &DateTimeSettingsView::onSet),
      editYear(2026U), editMonth(1U), editDay(1U),
      editHour(0U), editMinute(0U), editSecond(0U),
      tickCounter(0U)
{
    currentBuf[0] = 0;
    statusBuf[0] = 0;
}

void DateTimeSettingsView::setupScreen()
{
    DateTimeSettingsViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    currentText.setPosition(6, CUR_Y, SCREEN_WIDTH - 12, 16);
    currentText.setColor(theme::textSecondary());
    currentText.setTypedText(TypedText(T_WCSMALLLEFT));
    currentText.setWildcard1(currentBuf);
    add(currentText);

    for (uint8_t i = 0U; i < FIELD_COUNT; ++i)
    {
        const int16_t y = static_cast<int16_t>(ROW_TOP + i * ROW_H);

        fieldLabel[i].setPosition(LABEL_X, static_cast<int16_t>(y + 4), LABEL_W, 18);
        fieldLabel[i].setColor(theme::textSecondary());
        fieldLabel[i].setTypedText(TypedText(T_WCMEDIUMLEFT));
        Unicode::strncpy(labelBuf[i], FIELD_LABELS[i], 8);
        fieldLabel[i].setWildcard1(labelBuf[i]);
        add(fieldLabel[i]);

        minusButton[i].setup(MINUS_X, y, STEP_W, STEP_H);
        minusButton[i].setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
        minusButton[i].setLabel("-");
        minusButton[i].setId(static_cast<int16_t>(i));
        minusButton[i].setAction(stepClicked);
        add(minusButton[i]);

        fieldValue[i].setPosition(VALUE_X, static_cast<int16_t>(y + 3), VALUE_W, 20);
        fieldValue[i].setColor(theme::textPrimary());
        fieldValue[i].setTypedText(TypedText(T_WCDEFAULTCENTER));
        fieldValue[i].setWildcard1(valueBuf[i]);
        add(fieldValue[i]);

        plusButton[i].setup(PLUS_X, y, STEP_W, STEP_H);
        plusButton[i].setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
        plusButton[i].setLabel("+");
        plusButton[i].setId(static_cast<int16_t>(i + ID_PLUS_OFFSET));
        plusButton[i].setAction(stepClicked);
        add(plusButton[i]);
    }

    statusText.setPosition(6, 236, SCREEN_WIDTH - 12, 16);
    statusText.setColor(theme::textSecondary());
    statusText.setTypedText(TypedText(T_WCSMALLCENTER));
    statusText.setWildcard1(statusBuf);
    add(statusText);

    readButton.setup(8, 258, 108, 42);
    readButton.setColors(theme::surfaceAlt(), theme::primaryDark(), theme::textPrimary());
    readButton.setLabel("Read RTC");
    readButton.setId(ID_READ);
    readButton.setAction(readClicked);
    add(readButton);

    setButton.setup(124, 258, 108, 42);
    setButton.setColors(theme::ok(), theme::primaryDark(), theme::textOnPrimary());
    setButton.setLabel("Set");
    setButton.setId(ID_SET);
    setButton.setAction(setClicked);
    add(setButton);

    topBar.setup("Date & Time", ScreenId::Home);
    add(topBar);

    tickCounter = 0U;
    refreshFields();
    refreshCurrent();
}

void DateTimeSettingsView::tearDownScreen()
{
    DateTimeSettingsViewBase::tearDownScreen();
}

void DateTimeSettingsView::refreshFields()
{
    /* Keep the day within the selected month/year. */
    const uint8_t maxDay = daysInMonth(editYear, editMonth);
    if (editDay > maxDay) { editDay = maxDay; }

    Unicode::snprintf(valueBuf[0], 8, "%u", editYear);
    Unicode::snprintf(valueBuf[1], 8, "%02u", editMonth);
    Unicode::snprintf(valueBuf[2], 8, "%02u", editDay);
    Unicode::snprintf(valueBuf[3], 8, "%02u", editHour);
    Unicode::snprintf(valueBuf[4], 8, "%02u", editMinute);
    Unicode::snprintf(valueBuf[5], 8, "%02u", editSecond);
    for (uint8_t i = 0U; i < FIELD_COUNT; ++i) { fieldValue[i].invalidate(); }
}

void DateTimeSettingsView::refreshCurrent()
{
    GuiMeasurementSnapshot m;
    if (!presenter->data().getMeasurementSnapshot(m))
    {
        return;
    }
    if (m.rtcValid)
    {
        Unicode::snprintf(currentBuf, 28, "RTC %02u/%02u/%u  %02u:%02u:%02u",
                          m.time.day, m.time.month, m.time.year,
                          m.time.hour, m.time.minute, m.time.second);
    }
    else
    {
        Unicode::strncpy(currentBuf, "RTC --/--/----  --:--:--", 28);
    }
    currentText.setWildcard1(currentBuf);
    currentText.invalidate();
}

void DateTimeSettingsView::showStatus(const char* msg, colortype color)
{
    Unicode::strncpy(statusBuf, msg, 28);
    statusText.setColor(color);
    statusText.setWildcard1(statusBuf);
    statusText.invalidate();
}

static uint8_t stepU8(uint8_t v, int dir, uint8_t lo, uint8_t hi)
{
    if (dir > 0) { return (v >= hi) ? lo : (uint8_t)(v + 1U); }
    return (v <= lo) ? hi : (uint8_t)(v - 1U);
}

void DateTimeSettingsView::onStep(const TextButton& button)
{
    const int16_t id = button.getId();
    const uint8_t field = (id < static_cast<int16_t>(ID_PLUS_OFFSET))
                              ? static_cast<uint8_t>(id)
                              : static_cast<uint8_t>(id - ID_PLUS_OFFSET);
    const int dir = (id < static_cast<int16_t>(ID_PLUS_OFFSET)) ? -1 : 1;

    switch (field)
    {
    case 0: /* Year */
        editYear = (dir > 0) ? ((editYear >= 2099U) ? 2000U : (uint16_t)(editYear + 1U))
                             : ((editYear <= 2000U) ? 2099U : (uint16_t)(editYear - 1U));
        break;
    case 1: editMonth = stepU8(editMonth, dir, 1U, 12U); break;
    case 2:
    {
        const uint8_t maxDay = daysInMonth(editYear, editMonth);
        editDay = stepU8(editDay, dir, 1U, maxDay);
        break;
    }
    case 3: editHour = stepU8(editHour, dir, 0U, 23U); break;
    case 4: editMinute = stepU8(editMinute, dir, 0U, 59U); break;
    case 5: editSecond = stepU8(editSecond, dir, 0U, 59U); break;
    default: break;
    }
    refreshFields();
}

void DateTimeSettingsView::onRead(const TextButton& /*button*/)
{
    GuiMeasurementSnapshot m;
    if (!presenter->data().getMeasurementSnapshot(m) || !m.rtcValid)
    {
        showStatus("RTC unavailable", theme::error());
        return;
    }
    editYear = m.time.year;
    editMonth = m.time.month;
    editDay = m.time.day;
    editHour = m.time.hour;
    editMinute = m.time.minute;
    editSecond = m.time.second;
    refreshFields();
    showStatus("Loaded from RTC", theme::neutral());
}

void DateTimeSettingsView::onSet(const TextButton& /*button*/)
{
    /* Values are already range-clamped by the steppers; validate defensively. */
    if ((editMonth < 1U) || (editMonth > 12U))
    {
        showStatus("Invalid month", theme::error());
        return;
    }
    if ((editDay < 1U) || (editDay > daysInMonth(editYear, editMonth)))
    {
        showStatus("Invalid day for month", theme::error());
        return;
    }
    if ((editHour > 23U) || (editMinute > 59U) || (editSecond > 59U))
    {
        showStatus("Invalid time", theme::error());
        return;
    }

    const uint8_t wd = weekdayOf(editYear, editMonth, editDay);
    presenter->postCommand(makeSetDateTime(editYear, editMonth, editDay, wd,
                                           editHour, editMinute, editSecond));
    showStatus("Time set", theme::ok());
}

void DateTimeSettingsView::handleTickEvent()
{
    ++tickCounter;
    if ((tickCounter % 60U) == 0U)
    {
        refreshCurrent();
    }
}
