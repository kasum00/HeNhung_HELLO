#ifndef HISTORYVIEW_HPP
#define HISTORYVIEW_HPP

/**
 * @file    HistoryView.hpp
 * @brief   Measurement history: paged records with empty/unavailable states.
 * @note    Owner: user (non-generated).
 */

#include <gui_generated/history_screen/HistoryViewBase.hpp>
#include <gui/history_screen/HistoryPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/TextButton.hpp>
#include <gui/widgets/HistoryRow.hpp>
#include <gui/common/GuiTypes.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class HistoryView : public HistoryViewBase
{
public:
    HistoryView();
    virtual ~HistoryView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    void refresh();
    void onPrev(const gui::TextButton& button);
    void onNext(const gui::TextButton& button);

    touchgfx::Box background;
    gui::TopBar topBar;
    gui::TextButton prevButton;
    gui::TextButton nextButton;
    touchgfx::TextAreaWithOneWildcard pageText;
    touchgfx::TextAreaWithOneWildcard stateText;
    gui::HistoryRow rows[gui::HISTORY_RECORDS_PER_PAGE];

    touchgfx::Callback<HistoryView, const gui::TextButton&> prevClicked;
    touchgfx::Callback<HistoryView, const gui::TextButton&> nextClicked;

    touchgfx::Unicode::UnicodeChar pageBuffer[16];
    touchgfx::Unicode::UnicodeChar stateBuffer[28];

    uint16_t pageIndex;
    uint16_t pageCount;
};

#endif // HISTORYVIEW_HPP
