#pragma once

#include "common.h"

void blit_copy(APTR src_base,
               UWORD src_stride_b,
               UWORD src_x,
               UWORD src_y,
               APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               UWORD copy_w,
               UWORD copy_h,
               BOOL replace_bg,
               BOOL force_desc);

void blit_rect(APTR dst_base,
               UWORD dst_stride_b,
               UWORD dst_x,
               UWORD dst_y,
               APTR mask_base,
               UWORD mask_stride_b,
               UWORD mask_x,
               UWORD mask_y,
               UWORD width,
               UWORD height,
               BOOL set_bits);

void blit_line(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x0,
               UWORD y0,
               UWORD x1,
               UWORD y1);

void blit_fill(APTR dst_base,
               UWORD dst_stride_b,
               UWORD x,
               UWORD y,
               UWORD width,
               UWORD height);

void blit_char(APTR font_base,
               UWORD glyph_idx,
               APTR dst_row_base,
               UWORD dst_x,
               UWORD color,
               BOOL replace_bg);
