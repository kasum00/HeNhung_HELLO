/**
 * @file    Model.cpp
 * @brief   MVP Model implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

Model::Model()
    : modelListener(0),
      provider(),
      frame(0U)
{
}

void Model::tick()
{
    ++frame;
    provider.tick(frame);
}
