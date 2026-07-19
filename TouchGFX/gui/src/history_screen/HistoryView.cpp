/**
 * @file    HistoryView.cpp
 * @brief   Measurement history screen implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/history_screen/HistoryView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;
using namespace gui;

namespace
{
constexpr int16_t ROWS_TOP = layout::history::ROWS_TOP;
constexpr int16_t ROW_H = layout::history::ROW_H;
constexpr int16_t ROW_GAP = layout::history::ROW_GAP;
}

HistoryView::HistoryView()
    : prevClicked(this, &HistoryView::onPrev),
      nextClicked(this, &HistoryView::onNext),
      pageIndex(0U),
      pageCount(1U)
{
    pageBuffer[0] = 0;
    stateBuffer[0] = 0;
}

void HistoryView::setupScreen()
{
    HistoryViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    namespace L = layout::history;
    prevButton.setup(layout::MARGIN, L::NAV_Y, L::NAV_BTN_W, L::NAV_BTN_H);
    prevButton.setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
    prevButton.setLabel("<");
    prevButton.setAction(prevClicked);
    add(prevButton);

    nextButton.setup(L::NEXT_X, L::NAV_Y, L::NAV_BTN_W, L::NAV_BTN_H);
    nextButton.setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
    nextButton.setLabel(">");
    nextButton.setAction(nextClicked);
    add(nextButton);

    pageText.setPosition(L::PAGE_TEXT_X, L::PAGE_TEXT_Y, L::PAGE_TEXT_W, L::PAGE_TEXT_H);
    pageText.setColor(theme::textPrimary());
    pageText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    pageText.setWildcard1(pageBuffer);
    add(pageText);

    for (uint8_t i = 0U; i < HISTORY_RECORDS_PER_PAGE; ++i)
    {
        rows[i].setup(layout::MARGIN, static_cast<int16_t>(ROWS_TOP + i * (ROW_H + ROW_GAP)),
                      layout::CONTENT_W, ROW_H);
        add(rows[i]);
    }

    stateText.setPosition(10, L::STATE_Y, SCREEN_WIDTH - 20, L::STATE_H);
    stateText.setColor(theme::textSecondary());
    stateText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    stateText.setWildcard1(stateBuffer);
    stateText.setVisible(false);
    add(stateText);

    topBar.setup("History", ScreenId::Home);
    add(topBar);

    pageIndex = 0U;
    refresh();
}

void HistoryView::tearDownScreen()
{
    HistoryViewBase::tearDownScreen();
}

void HistoryView::refresh()
{
    GuiHistoryPageSnapshot page;
    if (!presenter->data().getHistoryPage(pageIndex, page))
    {
        return;
    }

    pageCount = (page.pageCount > 0U) ? page.pageCount : 1U;

    if (page.status == HistoryPageStatus::Ok)
    {
        stateText.setVisible(false);
        for (uint8_t i = 0U; i < HISTORY_RECORDS_PER_PAGE; ++i)
        {
            if (i < page.recordCount)
            {
                rows[i].setRecord(page.records[i]);
            }
            else
            {
                rows[i].hideRow();
            }
        }
        Unicode::snprintf(pageBuffer, 16, "Page %u/%u",
                          static_cast<unsigned>(page.pageIndex + 1U),
                          static_cast<unsigned>(page.pageCount));
    }
    else
    {
        for (uint8_t i = 0U; i < HISTORY_RECORDS_PER_PAGE; ++i)
        {
            rows[i].hideRow();
        }
        const char* msg = "No records";
        switch (page.status)
        {
        case HistoryPageStatus::Empty:              msg = "No measurements yet"; break;
        case HistoryPageStatus::Loading:            msg = "Loading..."; break;
        case HistoryPageStatus::StorageUnavailable: msg = "Storage unavailable"; break;
        default:                                    break;
        }
        Unicode::strncpy(stateBuffer, msg, 28);
        stateText.setWildcard1(stateBuffer);
        stateText.setVisible(true);
        stateText.invalidate();
        Unicode::strncpy(pageBuffer, "Page 0/0", 16);
    }
    pageText.setWildcard1(pageBuffer);
    pageText.invalidate();
}

void HistoryView::onPrev(const TextButton& /*button*/)
{
    if (pageIndex > 0U)
    {
        --pageIndex;
        refresh();
    }
}

void HistoryView::onNext(const TextButton& /*button*/)
{
    if ((pageIndex + 1U) < pageCount)
    {
        ++pageIndex;
        refresh();
    }
}
