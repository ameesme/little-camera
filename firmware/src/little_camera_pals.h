// little_camera_pals.h  —  companion sprites for the mood screen
// Generated from the Camera Companions bitmap spec. Do not hand-edit bitmaps here.
//
// Units: half-blocks. Screen = 50 x 30 half-blocks (25 x 15 face blocks, each split 2 x 2).
// Bitmaps: row-major, MSB first, 1 = ink, rows padded to whole bytes. All sprites face right.
// Mirror horizontally when placed on the right side, and negate dx.
#pragma once
#include <stdint.h>

typedef struct { const uint8_t *bmp; int8_t dx, dy; uint16_t ms; } pal_step_t;
typedef struct { const pal_step_t *steps; uint8_t n; uint8_t loop; } pal_anim_t;
typedef struct { uint8_t w, h, bpr; } pal_dim_t;

// ---- Mochi: 10 x 10, 2 byte(s) per row
static const pal_dim_t PAL_MOCHI_DIM = {10, 10, 2};
static const uint8_t PAL_MOCHI_BMP_IDLE_A[20] = {
  0x42,0x00,
  0x66,0x00,
  0x7e,0x00,
  0x5a,0x00,
  0x7e,0x80,
  0x7e,0x40,
  0xff,0x40,
  0xff,0x40,
  0xff,0xc0,
  0xe7,0x80
};
static const uint8_t PAL_MOCHI_BMP_IDLE_B[20] = {
  0x42,0x00,
  0x66,0x00,
  0x7e,0x00,
  0x5a,0x40,
  0x7e,0x40,
  0x7e,0x40,
  0xff,0x40,
  0xff,0x40,
  0xff,0x80,
  0xe7,0x00
};
static const uint8_t PAL_MOCHI_BMP_BLINK[20] = {
  0x42,0x00,
  0x66,0x00,
  0x7e,0x00,
  0x7e,0x00,
  0x7e,0x80,
  0x7e,0x40,
  0xff,0x40,
  0xff,0x40,
  0xff,0xc0,
  0xe7,0x80
};
static const uint8_t PAL_MOCHI_BMP_PERK[20] = {
  0xc3,0x00,
  0x66,0x00,
  0x7e,0x00,
  0x5a,0x40,
  0x7e,0x40,
  0x7e,0x40,
  0xff,0x40,
  0xff,0x40,
  0xff,0x80,
  0xe7,0x00
};
static const pal_step_t PAL_MOCHI_ANIM_IDLE_STEPS[] = {
  {PAL_MOCHI_BMP_IDLE_A, 0, 0, 1200},
  {PAL_MOCHI_BMP_IDLE_B, 0, 0, 400}
};
static const pal_anim_t PAL_MOCHI_ANIM_IDLE = {PAL_MOCHI_ANIM_IDLE_STEPS, 2, 1};
static const pal_step_t PAL_MOCHI_ANIM_BLINK_STEPS[] = {
  {PAL_MOCHI_BMP_BLINK, 0, 0, 120}
};
static const pal_anim_t PAL_MOCHI_ANIM_BLINK = {PAL_MOCHI_ANIM_BLINK_STEPS, 1, 0};
static const pal_step_t PAL_MOCHI_ANIM_REACT_STEPS[] = {
  {PAL_MOCHI_BMP_PERK, 0, -2, 150},
  {PAL_MOCHI_BMP_PERK, 0, -1, 100},
  {PAL_MOCHI_BMP_IDLE_A, 0, 0, 200}
};
static const pal_anim_t PAL_MOCHI_ANIM_REACT = {PAL_MOCHI_ANIM_REACT_STEPS, 3, 0};

// ---- Sir Croaks: 10 x 7, 2 byte(s) per row
static const pal_dim_t PAL_CROAKS_DIM = {10, 7, 2};
static const uint8_t PAL_CROAKS_BMP_IDLE_A[14] = {
  0x73,0x80,
  0x5e,0x80,
  0xff,0xc0,
  0xe1,0xc0,
  0xff,0xc0,
  0xbf,0x40,
  0xd2,0xc0
};
static const uint8_t PAL_CROAKS_BMP_BLINK[14] = {
  0x00,0x00,
  0x7f,0x80,
  0xff,0xc0,
  0xe1,0xc0,
  0xff,0xc0,
  0xbf,0x40,
  0xd2,0xc0
};
static const uint8_t PAL_CROAKS_BMP_CROUCH[14] = {
  0x00,0x00,
  0x73,0x80,
  0x5e,0x80,
  0xff,0xc0,
  0xe1,0xc0,
  0xff,0xc0,
  0xff,0xc0
};
static const uint8_t PAL_CROAKS_BMP_JUMP[14] = {
  0x73,0x80,
  0x5e,0x80,
  0xff,0xc0,
  0xe1,0xc0,
  0xff,0xc0,
  0x7f,0x80,
  0xa1,0x40
};
static const pal_step_t PAL_CROAKS_ANIM_IDLE_STEPS[] = {
  {PAL_CROAKS_BMP_IDLE_A, 0, 0, 1000}
};
static const pal_anim_t PAL_CROAKS_ANIM_IDLE = {PAL_CROAKS_ANIM_IDLE_STEPS, 1, 1};
static const pal_step_t PAL_CROAKS_ANIM_BLINK_STEPS[] = {
  {PAL_CROAKS_BMP_BLINK, 0, 0, 150}
};
static const pal_anim_t PAL_CROAKS_ANIM_BLINK = {PAL_CROAKS_ANIM_BLINK_STEPS, 1, 0};
static const pal_step_t PAL_CROAKS_ANIM_REACT_STEPS[] = {
  {PAL_CROAKS_BMP_CROUCH, 0, 0, 150},
  {PAL_CROAKS_BMP_JUMP, 0, -3, 200},
  {PAL_CROAKS_BMP_JUMP, 0, -1, 100},
  {PAL_CROAKS_BMP_CROUCH, 0, 0, 120},
  {PAL_CROAKS_BMP_IDLE_A, 0, 0, 200}
};
static const pal_anim_t PAL_CROAKS_ANIM_REACT = {PAL_CROAKS_ANIM_REACT_STEPS, 5, 0};

// ---- Tweet Seat: 11 x 7, 2 byte(s) per row
static const pal_dim_t PAL_TWEET_DIM = {11, 7, 2};
static const uint8_t PAL_TWEET_BMP_IDLE_A[14] = {
  0x03,0x80,
  0x02,0xe0,
  0x03,0x80,
  0x1f,0x80,
  0x73,0x80,
  0xdf,0x80,
  0x05,0x00
};
static const uint8_t PAL_TWEET_BMP_IDLE_B[14] = {
  0x03,0x80,
  0x02,0xe0,
  0x03,0x80,
  0x1f,0x80,
  0xf3,0x80,
  0x1f,0x80,
  0x05,0x00
};
static const uint8_t PAL_TWEET_BMP_CHIRP[14] = {
  0x03,0xa0,
  0x02,0xc0,
  0x03,0xc0,
  0x1f,0x80,
  0x73,0x80,
  0xdf,0x80,
  0x05,0x00
};
static const uint8_t PAL_TWEET_BMP_HOP[14] = {
  0x03,0x80,
  0x02,0xe0,
  0x03,0x80,
  0x1f,0x80,
  0x73,0x80,
  0xdf,0x80,
  0x00,0x00
};
static const pal_step_t PAL_TWEET_ANIM_IDLE_STEPS[] = {
  {PAL_TWEET_BMP_IDLE_A, 0, 0, 900},
  {PAL_TWEET_BMP_IDLE_B, 0, 0, 300}
};
static const pal_anim_t PAL_TWEET_ANIM_IDLE = {PAL_TWEET_ANIM_IDLE_STEPS, 2, 1};
static const pal_step_t PAL_TWEET_ANIM_CHIRP_STEPS[] = {
  {PAL_TWEET_BMP_CHIRP, 0, 0, 150},
  {PAL_TWEET_BMP_IDLE_A, 0, 0, 150},
  {PAL_TWEET_BMP_CHIRP, 0, 0, 150}
};
static const pal_anim_t PAL_TWEET_ANIM_CHIRP = {PAL_TWEET_ANIM_CHIRP_STEPS, 3, 0};
static const pal_step_t PAL_TWEET_ANIM_REACT_STEPS[] = {
  {PAL_TWEET_BMP_HOP, 0, -2, 150},
  {PAL_TWEET_BMP_IDLE_A, 0, 0, 150},
  {PAL_TWEET_BMP_HOP, 0, -2, 150},
  {PAL_TWEET_BMP_IDLE_A, 0, 0, 200}
};
static const pal_anim_t PAL_TWEET_ANIM_REACT = {PAL_TWEET_ANIM_REACT_STEPS, 4, 0};

// ---- Pillowcase: 9 x 9, 2 byte(s) per row
static const pal_dim_t PAL_PILLOWCASE_DIM = {9, 9, 2};
static const uint8_t PAL_PILLOWCASE_BMP_IDLE_A[18] = {
  0x3e,0x00,
  0x7f,0x00,
  0xff,0x80,
  0xdd,0x80,
  0xdd,0x80,
  0xff,0x80,
  0xf7,0x80,
  0xff,0x80,
  0xaa,0x80
};
static const uint8_t PAL_PILLOWCASE_BMP_IDLE_B[18] = {
  0x3e,0x00,
  0x7f,0x00,
  0xff,0x80,
  0xdd,0x80,
  0xdd,0x80,
  0xff,0x80,
  0xe3,0x80,
  0xff,0x80,
  0x55,0x00
};
static const pal_step_t PAL_PILLOWCASE_ANIM_IDLE_STEPS[] = {
  {PAL_PILLOWCASE_BMP_IDLE_A, 0, 0, 800},
  {PAL_PILLOWCASE_BMP_IDLE_B, 0, -1, 800}
};
static const pal_anim_t PAL_PILLOWCASE_ANIM_IDLE = {PAL_PILLOWCASE_ANIM_IDLE_STEPS, 2, 1};
