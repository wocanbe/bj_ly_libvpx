/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp9/common/vp9_blockd.h"

MB_PREDICTION_MODE vp9_left_block_mode(const MODE_INFO *cur_mi,
                                       const MODE_INFO *left_mi, int b) {
  if (b == 0 || b == 2) {
    if (!left_mi || is_inter_block(&left_mi->mbmi))
      return DC_PRED;

    return left_mi->mbmi.sb_type < BLOCK_8X8 ? left_mi->bmi[b + 1].as_mode
                                             : left_mi->mbmi.mode;
  } else {
    assert(b == 1 || b == 3);
    return cur_mi->bmi[b - 1].as_mode;
  }
}

MB_PREDICTION_MODE vp9_above_block_mode(const MODE_INFO *cur_mi,
                                        const MODE_INFO *above_mi, int b) {
  if (b == 0 || b == 1) {
    if (!above_mi || is_inter_block(&above_mi->mbmi))
      return DC_PRED;

    return above_mi->mbmi.sb_type < BLOCK_8X8 ? above_mi->bmi[b + 2].as_mode
                                              : above_mi->mbmi.mode;
  } else {
    assert(b == 2 || b == 3);
    return cur_mi->bmi[b - 2].as_mode;
  }
}

void vp9_foreach_transformed_block_in_plane(
    const MACROBLOCKD *const xd, BLOCK_SIZE bsize, int plane,
    foreach_transformed_block_visitor visit, void *arg) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const MB_MODE_INFO* mbmi = &xd->mi_8x8[0]->mbmi;
  // block and transform sizes, in number of 4x4 blocks log 2 ("*_b")
  // 4x4=0, 8x8=2, 16x16=4, 32x32=6, 64x64=8
  // transform size varies per plane, look it up in a common way.
  const TX_SIZE tx_size = plane ? get_uv_tx_size(mbmi)
                                : mbmi->tx_size;
  const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
  const int num_4x4_w = num_4x4_blocks_wide_lookup[plane_bsize];
  const int num_4x4_h = num_4x4_blocks_high_lookup[plane_bsize];
  const int step = 1 << (tx_size << 1);
  int i;

  // If mb_to_right_edge is < 0 we are in a situation in which
  // the current block size extends into the UMV and we won't
  // visit the sub blocks that are wholly within the UMV.
  if (xd->mb_to_right_edge < 0 || xd->mb_to_bottom_edge < 0) {
    int r, c;

    int max_blocks_wide = num_4x4_w;
    int max_blocks_high = num_4x4_h;

    // xd->mb_to_right_edge is in units of pixels * 8.  This converts
    // it to 4x4 block sizes.
    if (xd->mb_to_right_edge < 0)
      max_blocks_wide += (xd->mb_to_right_edge >> (5 + pd->subsampling_x));

    if (xd->mb_to_bottom_edge < 0)
      max_blocks_high += (xd->mb_to_bottom_edge >> (5 + pd->subsampling_y));

    i = 0;
    // Unlike the normal case - in here we have to keep track of the
    // row and column of the blocks we use so that we know if we are in
    // the unrestricted motion border.
    for (r = 0; r < num_4x4_h; r += (1 << tx_size)) {
      for (c = 0; c < num_4x4_w; c += (1 << tx_size)) {
        if (r < max_blocks_high && c < max_blocks_wide)
          visit(plane, i, plane_bsize, tx_size, arg);
        i += step;
      }
    }
  } else {
    for (i = 0; i < num_4x4_w * num_4x4_h; i += step)
      visit(plane, i, plane_bsize, tx_size, arg);
  }
}

void vp9_foreach_transformed_block(const MACROBLOCKD* const xd,
                                   BLOCK_SIZE bsize,
                                   foreach_transformed_block_visitor visit,
                                   void *arg) {
  int plane;

  for (plane = 0; plane < MAX_MB_PLANE; plane++)
    vp9_foreach_transformed_block_in_plane(xd, bsize, plane, visit, arg);
}

void vp9_set_contexts(const MACROBLOCKD *xd, struct macroblockd_plane *pd,
                      BLOCK_SIZE plane_bsize, TX_SIZE tx_size, int has_eob,
                      int aoff, int loff) {
  ENTROPY_CONTEXT *const a = pd->above_context + aoff;
  ENTROPY_CONTEXT *const l = pd->left_context + loff;
  const int tx_size_in_blocks = 1 << tx_size;

  // above
  if (has_eob && xd->mb_to_right_edge < 0) {
    int i;
    const int blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize] +
                            (xd->mb_to_right_edge >> (5 + pd->subsampling_x));
    int above_contexts = tx_size_in_blocks;
    if (above_contexts + aoff > blocks_wide)
      above_contexts = blocks_wide - aoff;

    for (i = 0; i < above_contexts; ++i)
      a[i] = has_eob;
    for (i = above_contexts; i < tx_size_in_blocks; ++i)
      a[i] = 0;
  } else {
    vpx_memset(a, has_eob, sizeof(ENTROPY_CONTEXT) * tx_size_in_blocks);
  }

  // left
  if (has_eob && xd->mb_to_bottom_edge < 0) {
    int i;
    const int blocks_high = num_4x4_blocks_high_lookup[plane_bsize] +
                            (xd->mb_to_bottom_edge >> (5 + pd->subsampling_y));
    int left_contexts = tx_size_in_blocks;
    if (left_contexts + loff > blocks_high)
      left_contexts = blocks_high - loff;

    for (i = 0; i < left_contexts; ++i)
      l[i] = has_eob;
    for (i = left_contexts; i < tx_size_in_blocks; ++i)
      l[i] = 0;
  } else {
    vpx_memset(l, has_eob, sizeof(ENTROPY_CONTEXT) * tx_size_in_blocks);
  }
}

void vp9_setup_block_planes(MACROBLOCKD *xd, int ss_x, int ss_y) {
  int i;

  for (i = 0; i < MAX_MB_PLANE; i++) {
    xd->plane[i].plane_type = i ? PLANE_TYPE_UV : PLANE_TYPE_Y;
    xd->plane[i].subsampling_x = i ? ss_x : 0;
    xd->plane[i].subsampling_y = i ? ss_y : 0;
  }
#if CONFIG_ALPHA
  // TODO(jkoleszar): Using the Y w/h for now
  xd->plane[3].plane_type = PLANE_TYPE_Y;
  xd->plane[3].subsampling_x = 0;
  xd->plane[3].subsampling_y = 0;
#endif
}
