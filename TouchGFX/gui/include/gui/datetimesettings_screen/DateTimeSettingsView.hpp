#ifndef DATETIMESETTINGSVIEW_HPP
#define DATETIMESETTINGSVIEW_HPP

/**
 * @file    DateTimeSettingsView.hpp
 * @brief   RTC date/time settings: read current time, edit fields, write.
 *
 * Editable Year/Month/Day/Hour/Minute/Second with +/- steppers. "Read RTC"
 * loads the current time; "Set" validates (days-in-month, leap year), computes
 * the weekday and sends a typed SetDateTime command (View never touches I2C).
 *
 * @note  Owner: user (non-generated).
 */

#include <gui_generated/datetimesettings_screen/DateTimeSettingsViewBase.hpp>
#include <gui/datetimesettings_screen/DateTimeSettingsPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/TextButton.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class DateTimeSettingsView : public DateTimeSettingsViewBase
{
public:
    static constexpr uint8_t FIELD_COUNT = 6U;   /* Y M D H M S */

    DateTimeSettingsView();
    virtual ~DateTimeSettingsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

protected:
    void refreshFields();
    void refreshCurrent();
    void onStep(const gui::TextButton& button);
    void onRead(const gui::TextButton& button);
    void onSet(const gui::TextButton& button);
    void showStatus(const char* msg, touchgfx::colortype color);

    touchgfx::Box background;
    gui::TopBar topBar;
    touchgfx::TextAreaWithOneWildcard currentText;
    touchgfx::TextAreaWithOneWildcard fieldLabel[FIELD_COUNT];
    touchgfx::TextAreaWithOneWildcard fieldValue[FIELD_COUNT];
    gui::TextButton minusButton[FIELD_COUNT];
    gui::TextButton plusButton[FIELD_COUNT];
    touchgfx::TextAreaWithOneWildcard statusText;
    gui::TextButton readButton;
    gui::TextButton setButton;

    touchgfx::Callback<DateTimeSettingsView, const gui::TextButton&> stepClicked;
    touchgfx::Callback<DateTimeSettingsView, const gui::TextButton&> readClicked;
    touchgfx::Callback<DateTimeSettingsView, const gui::TextButton&> setClicked;

    touchgfx::Unicode::UnicodeChar currentBuf[28];
    touchgfx::Unicode::UnicodeChar labelBuf[FIELD_COUNT][8];
    touchgfx::Unicode::UnicodeChar valueBuf[FIELD_COUNT][8];
    touchgfx::Unicode::UnicodeChar statusBuf[28];

    uint16_t editYear;
    uint8_t editMonth;
    uint8_t editDay;
    uint8_t editHour;
    uint8_t editMinute;
    uint8_t editSecond;

    uint32_t tickCounter;
};

#endif // DATETIMESETTINGSVIEW_HPP
