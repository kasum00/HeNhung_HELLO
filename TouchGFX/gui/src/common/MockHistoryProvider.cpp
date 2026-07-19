/**
 * @file    MockHistoryProvider.cpp
 * @brief   Implementation of the mock measurement-history source.
 * @note    Owner: user (non-generated).
 */

#include <gui/common/MockHistoryProvider.hpp>

namespace gui
{
namespace
{
/** @brief Deterministically synthesizes one history record for index @p i. */
GuiHistoryRecord makeRecord(uint16_t i)
{
    GuiHistoryRecord r{};

    /* Spread records backwards over recent days/minutes. */
    const uint16_t minutesAgo = static_cast<uint16_t>((i + 1U) * 37U);
    uint16_t hour = static_cast<uint16_t>((9U * 60U + 41U) + minutesAgo);
    r.time.day = static_cast<uint8_t>(17U - (hour / (24U * 60U)));
    hour %= (24U * 60U);
    r.time.hour = static_cast<uint8_t>(hour / 60U);
    r.time.minute = static_cast<uint8_t>(hour % 60U);
    r.time.second = 0U;
    r.time.month = 7U;
    r.time.year = 2026U;

    /* Most records are valid; every 5th is an invalid example. */
    const bool invalid = ((i % 5U) == 4U);
    if (invalid)
    {
        r.valid = false;
        r.bpm = 0.0F;
        r.spo2Percent = 0.0F;
        r.sqiPercent = static_cast<float>(15U + (i % 20U));
        r.invalidReason = ((i % 2U) == 0U) ? MeasurementInvalidReason::WeakSignal
                                           : MeasurementInvalidReason::MotionDetected;
    }
    else
    {
        r.valid = true;
        r.bpm = static_cast<float>(66U + ((i * 7U) % 24U));
        r.spo2Percent = static_cast<float>(95U + (i % 4U));
        r.sqiPercent = static_cast<float>(72U + ((i * 3U) % 24U));
        r.invalidReason = MeasurementInvalidReason::None;
    }
    return r;
}
} // namespace

MockHistoryProvider::MockHistoryProvider()
    : emulateEmpty(false),
      emulateUnavailable(false)
{
    for (uint16_t i = 0U; i < HISTORY_TOTAL_RECORDS; ++i)
    {
        records[i] = makeRecord(i);
    }
}

bool MockHistoryProvider::getPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot, uint32_t generation) const
{
    snapshot.generation = generation;
    snapshot.recordCount = 0U;
    for (uint8_t i = 0U; i < HISTORY_RECORDS_PER_PAGE; ++i)
    {
        snapshot.records[i] = GuiHistoryRecord{};
    }

    if (emulateUnavailable)
    {
        snapshot.status = HistoryPageStatus::StorageUnavailable;
        snapshot.pageIndex = 0U;
        snapshot.pageCount = 0U;
        snapshot.totalRecords = 0U;
        return true;
    }

    if (emulateEmpty)
    {
        snapshot.status = HistoryPageStatus::Empty;
        snapshot.pageIndex = 0U;
        snapshot.pageCount = 0U;
        snapshot.totalRecords = 0U;
        return true;
    }

    const uint16_t pageCount = static_cast<uint16_t>(
        (HISTORY_TOTAL_RECORDS + HISTORY_RECORDS_PER_PAGE - 1U) / HISTORY_RECORDS_PER_PAGE);
    const uint16_t clampedPage = (pageIndex < pageCount) ? pageIndex
                                                         : static_cast<uint16_t>(pageCount - 1U);

    snapshot.status = HistoryPageStatus::Ok;
    snapshot.pageIndex = clampedPage;
    snapshot.pageCount = pageCount;
    snapshot.totalRecords = HISTORY_TOTAL_RECORDS;

    const uint16_t first = static_cast<uint16_t>(clampedPage * HISTORY_RECORDS_PER_PAGE);
    for (uint8_t i = 0U; i < HISTORY_RECORDS_PER_PAGE; ++i)
    {
        const uint16_t src = static_cast<uint16_t>(first + i);
        if (src >= HISTORY_TOTAL_RECORDS)
        {
            break;
        }
        snapshot.records[i] = records[src];
        ++snapshot.recordCount;
    }
    return true;
}

} // namespace gui
