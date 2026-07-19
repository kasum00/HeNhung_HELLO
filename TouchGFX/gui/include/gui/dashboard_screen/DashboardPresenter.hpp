#ifndef DASHBOARDPRESENTER_HPP
#define DASHBOARDPRESENTER_HPP

/**
 * @file    DashboardPresenter.hpp
 * @brief   Presenter for the measurement dashboard.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <gui/common/GuiCommands.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class DashboardView;

class DashboardPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    DashboardPresenter(DashboardView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~DashboardPresenter() {}

    /** @brief Read-access to the active data provider. */
    gui::IGuiDataProvider& data() { return model->data(); }

    /** @brief Forwards a user command to the data provider. */
    void postCommand(const gui::GuiCommand& command) { model->postCommand(command); }

private:
    DashboardPresenter();

    DashboardView& view;
};

#endif // DASHBOARDPRESENTER_HPP
