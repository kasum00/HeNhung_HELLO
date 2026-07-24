#ifndef DATETIMESETTINGSPRESENTER_HPP
#define DATETIMESETTINGSPRESENTER_HPP

/**
 * @file    DateTimeSettingsPresenter.hpp
 * @brief   Presenter for the RTC date/time settings screen.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <gui/common/GuiCommands.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class DateTimeSettingsView;

class DateTimeSettingsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    DateTimeSettingsPresenter(DateTimeSettingsView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~DateTimeSettingsPresenter() {}

    /** @brief Read-access to the active data provider. */
    gui::IGuiDataProvider& data() { return model->data(); }

    /** @brief Forwards a user command to the data provider. */
    void postCommand(const gui::GuiCommand& command) { model->postCommand(command); }

private:
    DateTimeSettingsPresenter();

    DateTimeSettingsView& view;
};

#endif // DATETIMESETTINGSPRESENTER_HPP
