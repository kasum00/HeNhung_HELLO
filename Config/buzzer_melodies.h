#ifndef BUZZER_MELODIES_H
#define BUZZER_MELODIES_H

/**
 * @file    buzzer_melodies.h
 * @brief   Tần số các nốt nhạc và các giai điệu định sẵn (dữ liệu cấu hình).
 *
 * Theo CONFIG RULE, giai điệu là dữ liệu, không phải code nhét trong driver. Tần
 * số nốt theo bình quân luật (A4 = 440 Hz).
 */

#include "buzzer_driver.h"

/** @name Tần số nốt (Hz) */
///@{
#define NOTE_C4   262U
#define NOTE_D4   294U
#define NOTE_E4   330U
#define NOTE_F4   349U
#define NOTE_G4   392U
#define NOTE_A4   440U
#define NOTE_B4   494U

#define NOTE_C5   523U
#define NOTE_D5   587U
#define NOTE_E5   659U
#define NOTE_F5   698U
#define NOTE_G5   784U
#define NOTE_A5   880U
#define NOTE_B5   988U

#define NOTE_REST 0U
///@}

/** @brief Giai điệu khởi động 3 nốt (C5, E5, G5). */
static const BuzzerNote BUZZER_MELODY_STARTUP[] =
{
    { NOTE_C5, 120U, 30U },
    { NOTE_E5, 120U, 30U },
    { NOTE_G5, 180U, 0U  }
};
#define BUZZER_MELODY_STARTUP_LEN  (sizeof(BUZZER_MELODY_STARTUP) / sizeof(BUZZER_MELODY_STARTUP[0]))

/** @brief Self-test lúc bring-up: ba cao độ khác nhau rõ rệt. */
static const BuzzerNote BUZZER_MELODY_SELFTEST[] =
{
    { NOTE_C4, 300U, 100U },
    { NOTE_E4, 300U, 100U },
    { NOTE_G4, 500U, 0U   }
};
#define BUZZER_MELODY_SELFTEST_LEN  (sizeof(BUZZER_MELODY_SELFTEST) / sizeof(BUZZER_MELODY_SELFTEST[0]))

/** @brief Đo xong + đã lưu: giai điệu 2 nốt đi lên dễ chịu. */
static const BuzzerNote BUZZER_MELODY_DONE[] =
{
    { NOTE_E5, 120U, 20U },
    { NOTE_A5, 200U, 0U  }
};
#define BUZZER_MELODY_DONE_LEN  (sizeof(BUZZER_MELODY_DONE) / sizeof(BUZZER_MELODY_DONE[0]))

/** @brief Phiên không được lưu (quá ngắn / không hợp lệ): hai tiếng bíp trầm. */
static const BuzzerNote BUZZER_MELODY_INVALID[] =
{
    { NOTE_C4, 90U, 60U },
    { NOTE_C4, 90U, 0U  }
};
#define BUZZER_MELODY_INVALID_LEN  (sizeof(BUZZER_MELODY_INVALID) / sizeof(BUZZER_MELODY_INVALID[0]))

#endif /* BUZZER_MELODIES_H */
