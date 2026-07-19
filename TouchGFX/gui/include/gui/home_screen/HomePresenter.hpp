#ifndef HOMEPRESENTER_HPP
#define HOMEPRESENTER_HPP

/**
 * @file    HomePresenter.hpp
 * @brief   Presenter for the Home menu; exposes the data provider to the view.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/ModelListener.hpp>
#include <gui/common/IGuiDataProvider.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class HomeView;

class HomePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    HomePresenter(HomeView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~HomePresenter() {}

    /** @brief Provides the view read-access to the active data provider. */
    gui::IGuiDataProvider& data()
    {
        return model->data();
    }

private:
    HomePresenter();

    HomeView& view;
};

#endif // HOMEPRESENTER_HPP
