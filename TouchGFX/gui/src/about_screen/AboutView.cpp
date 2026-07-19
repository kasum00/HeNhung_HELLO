/**
 * @file    AboutView.cpp
 * @brief   About / system information implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/about_screen/AboutView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;
using namespace gui;

namespace
{
constexpr int16_t ROWS_TOP = layout::about::ROWS_TOP;
constexpr int16_t ROW_H = layout::about::ROW_H;

const char* const ROW_LABELS[AboutView::ROW_COUNT] = {
    "Firmware", "Build", "MCU", "Display", "Sensor", "Algorithm"
};
}

AboutView::AboutView()
{
    titleBuffer[0] = 0;
    disclaimer1Buffer[0] = 0;
    disclaimer2Buffer[0] = 0;
}

void AboutView::setupScreen()
{
    AboutViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    GuiSystemInfoSnapshot info;
    presenter->data().getSystemInfoSnapshot(info);

    titleText.setPosition(0, layout::about::TITLE_Y, SCREEN_WIDTH, layout::about::TITLE_H);
    titleText.setColor(theme::textPrimary());
    titleText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    Unicode::strncpy(titleBuffer, info.projectName, 24);
    titleText.setWildcard1(titleBuffer);
    add(titleText);

    for (uint8_t i = 0U; i < ROW_COUNT; ++i)
    {
        rows[i].setup(layout::MARGIN, static_cast<int16_t>(ROWS_TOP + i * ROW_H),
                      layout::CONTENT_W, ROW_H, ROW_LABELS[i]);
        add(rows[i]);
    }
    rows[0].setValueText(info.firmwareVersion);
    rows[1].setValueText(info.buildProfile);
    rows[2].setValueText(info.mcu);
    rows[3].setValueText(info.displayResolution);
    rows[4].setValueText(info.sensorName);
    rows[5].setValueText(info.algorithmStatus);

    /* Mandatory medical disclaimer (English keeps within the available glyphs). */
    disclaimerBox.setPosition(layout::MARGIN, layout::about::DISCLAIMER_Y, layout::CONTENT_W,
                              layout::about::DISCLAIMER_H);
    disclaimerBox.setColor(theme::surfaceAlt());
    add(disclaimerBox);

    disclaimerLine1.setPosition(10, layout::about::DISCLAIMER_LINE1_Y, 220, layout::about::DISCLAIMER_LINE_H);
    disclaimerLine1.setColor(theme::warning());
    disclaimerLine1.setTypedText(TypedText(T_WCMEDIUMCENTER));
    Unicode::strncpy(disclaimer1Buffer, "Nam chan be du.", 28);
    disclaimerLine1.setWildcard1(disclaimer1Buffer);
    add(disclaimerLine1);

    disclaimerLine2.setPosition(10, layout::about::DISCLAIMER_LINE2_Y, 220, layout::about::DISCLAIMER_LINE_H);
    disclaimerLine2.setColor(theme::warning());
    disclaimerLine2.setTypedText(TypedText(T_WCMEDIUMCENTER));
    Unicode::strncpy(disclaimer2Buffer, "Coder bi nghe.", 28);
    disclaimerLine2.setWildcard1(disclaimer2Buffer);
    add(disclaimerLine2);

    topBar.setup("About", ScreenId::Home);
    add(topBar);
}

void AboutView::tearDownScreen()
{
    AboutViewBase::tearDownScreen();
}
