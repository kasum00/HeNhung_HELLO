#ifndef APPLICATION_GUI_BRIDGE_HPP
#define APPLICATION_GUI_BRIDGE_HPP

/**
 * @file    application_gui_bridge.hpp
 * @brief   Nguồn dữ liệu GUI thật (target): đọc kết quả engine đo (đã công bố bởi
 *          DSP task) cùng RTC service, đọc lịch sử tạm, và ủy quyền các màn hình
 *          chưa dùng dữ liệu thật (settings/system-info) cho một
 *          MockGuiDataProvider nội bộ.
 *
 * Model chọn bridge này trên target (SIMULATOR dùng mock). Engine chạy trong DSP
 * task; tick() ở đây chỉ đọc kết quả đã công bố nên không cần khóa engine. Nằm
 * ngoài cây gui/ để bản build simulator không bao giờ biên dịch phần phụ thuộc
 * HAL/firmware này.
 */

#include <gui/common/IGuiDataProvider.hpp>
#include <gui/common/MockGuiDataProvider.hpp>

extern "C" {
#include "ppg_types.h"
}

namespace gui
{
/** @brief IGuiDataProvider dựa trên ứng dụng, cho target. */
class ApplicationGuiBridge : public IGuiDataProvider
{
public:
    ApplicationGuiBridge();

    void tick(uint32_t frameCounter) override;
    void postCommand(const GuiCommand& command) override;
    void notifyScreenTransition() override;
    bool getMeasurementSnapshot(GuiMeasurementSnapshot& snapshot) override;
    bool getWaveformSnapshot(GuiWaveformSnapshot& snapshot) override;
    bool getHistoryPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot) override;

private:
    MockGuiDataProvider mock_;   /**< ủy quyền cho các màn hình chưa dùng dữ liệu thật */
    PpgResult ppg_;              /**< kết quả engine công bố mới nhất */
    uint32_t generation_;
    uint32_t resultReadyMs_;     /**< HAL_GetTick khi lần đầu thấy RESULT_READY */
    bool     wasResultReady_;    /**<prevState == RESULT_READY để detect cạnh lên */
};

} // namespace gui

#endif // APPLICATION_GUI_BRIDGE_HPP
