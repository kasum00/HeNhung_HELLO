#ifndef MOCK_HISTORY_PROVIDER_HPP
#define MOCK_HISTORY_PROVIDER_HPP

/**
 * @file    MockHistoryProvider.hpp
 * @brief   Serves pages of mock measurement history.
 *
 * Holds a fixed, statically-sized set of history records and returns them one
 * page at a time. It never touches FATFS or the SD card. It can also emulate an
 * empty store or an unavailable-storage condition so the history screen's edge
 * states can be exercised. Later this is replaced by a StorageService-backed
 * source behind the same @ref gui::GuiHistoryPageSnapshot.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/GuiSnapshots.hpp>

namespace gui
{
/** @brief Fixed in-memory history with page access and simulated edge states. */
class MockHistoryProvider
{
public:
    MockHistoryProvider();

    /**
     * @brief Overrides the record count to emulate an empty store.
     * @param empty When true, subsequent pages report HistoryPageStatus::Empty.
     */
    void setEmpty(bool empty) { emulateEmpty = empty; }

    /**
     * @brief Emulates storage being unavailable.
     * @param unavailable When true, pages report StorageUnavailable.
     */
    void setStorageUnavailable(bool unavailable) { emulateUnavailable = unavailable; }

    /**
     * @brief Fills a page of history records.
     * @param[in]  pageIndex Requested 0-based page (clamped to valid range).
     * @param[out] snapshot  Destination page snapshot.
     * @param generation     Generation counter to stamp into the snapshot.
     * @return Always true; @c snapshot.status conveys empty/unavailable states.
     */
    bool getPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot, uint32_t generation) const;

private:
    GuiHistoryRecord records[HISTORY_TOTAL_RECORDS];
    bool emulateEmpty;
    bool emulateUnavailable;
};

} // namespace gui

#endif // MOCK_HISTORY_PROVIDER_HPP
