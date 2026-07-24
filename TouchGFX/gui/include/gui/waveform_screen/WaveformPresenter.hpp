#ifndef WAVEFORMPRESENTER_HPP
#define WAVEFORMPRESENTER_HPP

/**
 * @file    WaveformPresenter.hpp
 * @brief   Presenter for the waveform screen.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <gui/common/GuiCommands.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class WaveformView;

class WaveformPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    WaveformPresenter(WaveformView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~WaveformPresenter() {}

    /** @brief Read-access to the active data provider. */
    gui::IGuiDataProvider& data() { return model->data(); }

    /** @brief Forwards a user command (e.g. filter mode/window) to the provider. */
    void postCommand(const gui::GuiCommand& command) { model->postCommand(command); }

private:
    WaveformPresenter();

    WaveformView& view;
};

#endif // WAVEFORMPRESENTER_HPP
