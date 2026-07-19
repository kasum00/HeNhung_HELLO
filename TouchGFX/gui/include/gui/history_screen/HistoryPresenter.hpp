#ifndef HISTORYPRESENTER_HPP
#define HISTORYPRESENTER_HPP

/**
 * @file    HistoryPresenter.hpp
 * @brief   Presenter for the measurement history screen.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class HistoryView;

class HistoryPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    HistoryPresenter(HistoryView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~HistoryPresenter() {}

    /** @brief Read-access to the active data provider. */
    gui::IGuiDataProvider& data() { return model->data(); }

private:
    HistoryPresenter();

    HistoryView& view;
};

#endif // HISTORYPRESENTER_HPP
