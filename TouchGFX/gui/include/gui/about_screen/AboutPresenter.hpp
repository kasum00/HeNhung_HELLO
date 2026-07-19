#ifndef ABOUTPRESENTER_HPP
#define ABOUTPRESENTER_HPP

/**
 * @file    AboutPresenter.hpp
 * @brief   Presenter for the about / system information screen.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class AboutView;

class AboutPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    AboutPresenter(AboutView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~AboutPresenter() {}

    /** @brief Read-access to the active data provider. */
    gui::IGuiDataProvider& data() { return model->data(); }

private:
    AboutPresenter();

    AboutView& view;
};

#endif // ABOUTPRESENTER_HPP
