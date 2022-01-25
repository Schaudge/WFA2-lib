/*
 *                             The MIT License
 *
 * Wavefront Alignments Algorithms
 * Copyright (c) 2017 by Santiago Marco-Sola  <santiagomsola@gmail.com>
 *
 * This file is part of Wavefront Alignments Algorithms.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * PROJECT: Wavefront Alignments Algorithms
 * AUTHOR(S): Santiago Marco-Sola <santiagomsola@gmail.com>
 * DESCRIPTION: WaveFront alignment module for computing wavefronts (gap-affine)
 */

#include "utils/string_padded.h"
#include "wavefront_compute.h"

/*
 * Wavefront BT-Piggyback (offloading the piggyback blocks)
 */
#define WAVEFRONT_COMPUTE_BT_BUFFER_OFFLOAD(offsets,bt_pcigar,bt_prev,k) \
  const int predicated_next = (offsets[k]>=0); \
  /* Store */ \
  bt_block_mem->pcigar = bt_pcigar[k]; \
  bt_block_mem->prev_idx = bt_prev[k]; \
  bt_block_mem += predicated_next; \
  /* Reset */ \
  bt_pcigar[k] = 0; \
  bt_prev[k] = current_pos; \
  current_pos += predicated_next; \
  /* Update pos */ \
  if (current_pos >= max_pos) { \
    wf_backtrace_buffer_add_used(bt_buffer,current_pos-global_pos); \
    global_pos = wf_backtrace_buffer_get_mem(bt_buffer,&bt_block_mem,&bt_blocks_available); \
  }
void wavefront_compute_affine_idm_piggyback_offload_wf(
    wf_offset_t* const out_offsets,
    pcigar_t* const out_bt_pcigar,
    block_idx_t* const out_bt_prev,
    const int lo,
    const int hi,
    wf_backtrace_buffer_t* const bt_buffer) {
  // Fetch BT-buffer free memory
  int bt_blocks_available;
  wf_backtrace_block_t* bt_block_mem;
  block_idx_t global_pos = wf_backtrace_buffer_get_mem(bt_buffer,&bt_block_mem,&bt_blocks_available);
  block_idx_t current_pos = global_pos;
  const int max_pos = current_pos + bt_blocks_available;
  // Check PCIGAR buffers full and off-load if needed
  int k;
  for (k=lo;k<=hi;++k) {
    // if (PCIGAR_IS_ALMOST_FULL(out_bt_pcigar[k]) && out_offsets[k]>=0) {
    if (PCIGAR_IS_HALF_FULL(out_bt_pcigar[k]) && out_offsets[k]>=0) {
      // Store
      bt_block_mem->pcigar = out_bt_pcigar[k];
      bt_block_mem->prev_idx = out_bt_prev[k];
      bt_block_mem++;
      // Reset
      out_bt_pcigar[k] = 0;
      out_bt_prev[k] = current_pos;
      current_pos++;
      // Update pos
      if (current_pos >= max_pos) {
        wf_backtrace_buffer_add_used(bt_buffer,current_pos-global_pos);
        global_pos = wf_backtrace_buffer_get_mem(bt_buffer,&bt_block_mem,&bt_blocks_available);
      }
    }
  }
  wf_backtrace_buffer_add_used(bt_buffer,current_pos-global_pos);
}
void wavefront_compute_affine_idm_piggyback_offload(
    const wavefront_set_t* const wavefront_set,
    const int lo,
    const int hi,
    wf_backtrace_buffer_t* const bt_buffer) {
  // Parameters
  pcigar_t* const out_m_bt_pcigar   = wavefront_set->out_mwavefront->bt_pcigar;
  block_idx_t* const out_m_bt_prev  = wavefront_set->out_mwavefront->bt_prev;
  pcigar_t* const out_i1_bt_pcigar  = wavefront_set->out_i1wavefront->bt_pcigar;
  block_idx_t* const out_i1_bt_prev = wavefront_set->out_i1wavefront->bt_prev;
  pcigar_t* const out_d1_bt_pcigar  = wavefront_set->out_d1wavefront->bt_pcigar;
  block_idx_t* const out_d1_bt_prev = wavefront_set->out_d1wavefront->bt_prev;
  // Out Offsets
  wf_offset_t* const out_m  = wavefront_set->out_mwavefront->offsets;
  wf_offset_t* const out_i1 = wavefront_set->out_i1wavefront->offsets;
  wf_offset_t* const out_d1 = wavefront_set->out_d1wavefront->offsets;
  // Compute BT occupancy maximum
  const int bt_occupancy_max_i1 =
      MAX(wavefront_set->in_mwavefront_gap1->bt_occupancy_max,
          wavefront_set->in_i1wavefront_ext->bt_occupancy_max) + 1;
  const int bt_occupancy_max_d1 =
      MAX(wavefront_set->in_mwavefront_gap1->bt_occupancy_max,
          wavefront_set->in_d1wavefront_ext->bt_occupancy_max) + 1;
  const int bt_occupancy_max_indel = MAX(bt_occupancy_max_i1,bt_occupancy_max_d1);
  const int bt_occupancy_max_m =
      MAX(wavefront_set->in_mwavefront_sub->bt_occupancy_max,bt_occupancy_max_indel) + 1;
  wavefront_set->out_i1wavefront->bt_occupancy_max = bt_occupancy_max_i1;
  wavefront_set->out_d1wavefront->bt_occupancy_max = bt_occupancy_max_d1;
  wavefront_set->out_mwavefront->bt_occupancy_max = bt_occupancy_max_m;
  // Check maximum occupancy reached and skip if offloading is not necessary
  if (bt_occupancy_max_i1 >= PCIGAR_MAX_LENGTH) {
    wavefront_compute_affine_idm_piggyback_offload_wf(
        out_i1,out_i1_bt_pcigar,out_i1_bt_prev,lo,hi,bt_buffer);
    wavefront_set->out_i1wavefront->bt_occupancy_max = PCIGAR_MAX_LENGTH/2; // Reset occupancy
  }
  if (bt_occupancy_max_d1 >= PCIGAR_MAX_LENGTH) {
    wavefront_compute_affine_idm_piggyback_offload_wf(
        out_d1,out_d1_bt_pcigar,out_d1_bt_prev,lo,hi,bt_buffer);
    wavefront_set->out_d1wavefront->bt_occupancy_max = PCIGAR_MAX_LENGTH/2; // Reset occupancy
  }
  if (bt_occupancy_max_m >= PCIGAR_MAX_LENGTH) {
    wavefront_compute_affine_idm_piggyback_offload_wf(
        out_m,out_m_bt_pcigar,out_m_bt_prev,lo,hi,bt_buffer);
    wavefront_set->out_mwavefront->bt_occupancy_max = PCIGAR_MAX_LENGTH/2; // Reset occupancy
  }
}
/*
 * Compute Kernels
 */
void wavefront_compute_affine_idm(
    const wavefront_set_t* const wavefront_set,
    const int lo,
    const int hi) {
  // In Offsets
  const wf_offset_t* const m_sub = wavefront_set->in_mwavefront_sub->offsets;
  const wf_offset_t* const m_open1 = wavefront_set->in_mwavefront_gap1->offsets;
  const wf_offset_t* const i1_ext = wavefront_set->in_i1wavefront_ext->offsets;
  const wf_offset_t* const d1_ext = wavefront_set->in_d1wavefront_ext->offsets;
  // Out Offsets
  wf_offset_t* const out_m = wavefront_set->out_mwavefront->offsets;
  wf_offset_t* const out_i1 = wavefront_set->out_i1wavefront->offsets;
  wf_offset_t* const out_d1 = wavefront_set->out_d1wavefront->offsets;
  // Compute-Next kernel loop
  int k;
  PRAGMA_LOOP_VECTORIZE
  for (k=lo;k<=hi;++k) {
    // Update I1
    const wf_offset_t ins1_o = m_open1[k-1];
    const wf_offset_t ins1_e = i1_ext[k-1];
    const wf_offset_t ins1 = MAX(ins1_o,ins1_e) + 1;
    out_i1[k] = ins1;
    // Update D1
    const wf_offset_t del1_o = m_open1[k+1];
    const wf_offset_t del1_e = d1_ext[k+1];
    const wf_offset_t del1 = MAX(del1_o,del1_e);
    out_d1[k] = del1;
    // Update M
    const wf_offset_t sub = m_sub[k] + 1;
    out_m[k] = MAX(del1,MAX(sub,ins1));
  }
}
/*
 * Compute Kernel (Piggyback)
 *   Scratchpad https://godbolt.org/z/16h39jfPb
 */
//void wavefront_compute_affine_idm_piggyback(
//    const wavefront_set_t* const wavefront_set,
//    const int lo,
//    const int hi,
//    wf_backtrace_buffer_t* const bt_buffer) {
//  // In Offsets
//  const wf_offset_t* const m_sub   = wavefront_set->in_mwavefront_sub->offsets;
//  const wf_offset_t* const m_open1 = wavefront_set->in_mwavefront_gap1->offsets;
//  const wf_offset_t* const i1_ext  = wavefront_set->in_i1wavefront_ext->offsets;
//  const wf_offset_t* const d1_ext  = wavefront_set->in_d1wavefront_ext->offsets;
//  // Out Offsets
//  wf_offset_t* const out_m  = wavefront_set->out_mwavefront->offsets;
//  wf_offset_t* const out_i1 = wavefront_set->out_i1wavefront->offsets;
//  wf_offset_t* const out_d1 = wavefront_set->out_d1wavefront->offsets;
//  // In BT-pcigar
//  const pcigar_t* const m_sub_bt_pcigar   = wavefront_set->in_mwavefront_sub->bt_pcigar;
//  const pcigar_t* const m_open1_bt_pcigar = wavefront_set->in_mwavefront_gap1->bt_pcigar;
//  const pcigar_t* const i1_ext_bt_pcigar  = wavefront_set->in_i1wavefront_ext->bt_pcigar;
//  const pcigar_t* const d1_ext_bt_pcigar  = wavefront_set->in_d1wavefront_ext->bt_pcigar;
//  // In BT-prev
//  const block_idx_t* const m_sub_bt_prev   = wavefront_set->in_mwavefront_sub->bt_prev;
//  const block_idx_t* const m_open1_bt_prev = wavefront_set->in_mwavefront_gap1->bt_prev;
//  const block_idx_t* const i1_ext_bt_prev  = wavefront_set->in_i1wavefront_ext->bt_prev;
//  const block_idx_t* const d1_ext_bt_prev  = wavefront_set->in_d1wavefront_ext->bt_prev;
//  // Out BT-pcigar
//  pcigar_t* const out_m_bt_pcigar   = wavefront_set->out_mwavefront->bt_pcigar;
//  pcigar_t* const out_i1_bt_pcigar  = wavefront_set->out_i1wavefront->bt_pcigar;
//  pcigar_t* const out_d1_bt_pcigar  = wavefront_set->out_d1wavefront->bt_pcigar;
//  // Out BT-prev
//  block_idx_t* const out_m_bt_prev  = wavefront_set->out_mwavefront->bt_prev;
//  block_idx_t* const out_i1_bt_prev = wavefront_set->out_i1wavefront->bt_prev;
//  block_idx_t* const out_d1_bt_prev = wavefront_set->out_d1wavefront->bt_prev;
//  // Compute-Next kernel loop
//  int k;
//  PRAGMA_LOOP_VECTORIZE
//  for (k=lo;k<=hi;++k) {
//    // Update I1
//    const wf_offset_t ins1_o = m_open1[k-1];
//    const wf_offset_t ins1_e = i1_ext[k-1];
//    const bool cond_ins1 = (ins1_e >= ins1_o);
//    const wf_offset_t ins1 = (cond_ins1) ? ins1_e+1 : ins1_o+1;
//    const block_idx_t ins1_bt = (cond_ins1) ? i1_ext_bt_prev[k-1] : m_open1_bt_prev[k-1];
//    pcigar_t ins1_pcigar = (cond_ins1) ? i1_ext_bt_pcigar[k-1] : m_open1_bt_pcigar[k-1];
//    ins1_pcigar = PCIGAR_PUSH_BACK_INS(ins1_pcigar);
//    out_i1_bt_pcigar[k] = ins1_pcigar;
//    out_i1_bt_prev[k] = ins1_bt;
//    out_i1[k] = ins1;
//    // Update D1
//    const wf_offset_t del1_o = m_open1[k+1];
//    const wf_offset_t del1_e = d1_ext[k+1];
//    const bool cond_del1 = (del1_e >= del1_o);
//    const wf_offset_t del1 = (cond_del1) ? del1_e : del1_o;
//    const block_idx_t del1_bt = (cond_del1) ? d1_ext_bt_prev[k+1] : m_open1_bt_prev[k+1];
//    pcigar_t del1_pcigar = (cond_del1) ? d1_ext_bt_pcigar[k+1] : m_open1_bt_pcigar[k+1];
//    del1_pcigar = PCIGAR_PUSH_BACK_DEL(del1_pcigar);
//    out_d1_bt_pcigar[k] = del1_pcigar;
//    out_d1_bt_prev[k] = del1_bt;
//    out_d1[k] = del1;
//    // Update Indel1
//    const bool cond_indel1 = (del1 >= ins1);
//    const wf_offset_t indel1 = (cond_indel1) ? del1 : ins1;
//    const pcigar_t indel1_pcigar = (cond_indel1) ? del1_pcigar : ins1_pcigar;
//    const block_idx_t indel1_bt = (cond_indel1) ? del1_bt : ins1_bt;
//    // Update M
//    const wf_offset_t sub = m_sub[k] + 1;
//    const bool cond_misms = (sub >= indel1);
//    const wf_offset_t misms = (cond_misms) ? sub : indel1;
//    const pcigar_t misms_pcigar = (cond_misms) ? m_sub_bt_pcigar[k] : indel1_pcigar;
//    const block_idx_t misms_bt = (cond_misms) ? m_sub_bt_prev[k] : indel1_bt;
//    // Coming from I/D -> X is fake to represent gap-close
//    // Coming from M -> X is real to represent mismatch
//    out_m_bt_pcigar[k] = PCIGAR_PUSH_BACK_MISMS(misms_pcigar);
//    out_m_bt_prev[k] = misms_bt;
//    out_m[k] = misms;
//  }
//  // Offload backtrace
//  wavefront_compute_affine_idm_piggyback_offload(wavefront_set,lo,hi,bt_buffer);
//}
void wavefront_compute_affine_idm_piggyback(
    const wavefront_set_t* const wavefront_set,
    const int lo,
    const int hi,
    wf_backtrace_buffer_t* const bt_buffer) {
  // In Offsets
  const wf_offset_t* const m_sub   = wavefront_set->in_mwavefront_sub->offsets;
  const wf_offset_t* const m_open1 = wavefront_set->in_mwavefront_gap1->offsets;
  const wf_offset_t* const i1_ext  = wavefront_set->in_i1wavefront_ext->offsets;
  const wf_offset_t* const d1_ext  = wavefront_set->in_d1wavefront_ext->offsets;
  // Out Offsets
  wf_offset_t* const out_m  = wavefront_set->out_mwavefront->offsets;
  wf_offset_t* const out_i1 = wavefront_set->out_i1wavefront->offsets;
  wf_offset_t* const out_d1 = wavefront_set->out_d1wavefront->offsets;
  // In BT-pcigar
  const pcigar_t* const m_sub_bt_pcigar   = wavefront_set->in_mwavefront_sub->bt_pcigar;
  const pcigar_t* const m_open1_bt_pcigar = wavefront_set->in_mwavefront_gap1->bt_pcigar;
  const pcigar_t* const i1_ext_bt_pcigar  = wavefront_set->in_i1wavefront_ext->bt_pcigar;
  const pcigar_t* const d1_ext_bt_pcigar  = wavefront_set->in_d1wavefront_ext->bt_pcigar;
  // In BT-prev
  const block_idx_t* const m_sub_bt_prev   = wavefront_set->in_mwavefront_sub->bt_prev;
  const block_idx_t* const m_open1_bt_prev = wavefront_set->in_mwavefront_gap1->bt_prev;
  const block_idx_t* const i1_ext_bt_prev  = wavefront_set->in_i1wavefront_ext->bt_prev;
  const block_idx_t* const d1_ext_bt_prev  = wavefront_set->in_d1wavefront_ext->bt_prev;
  // Out BT-pcigar
  pcigar_t* const out_m_bt_pcigar   = wavefront_set->out_mwavefront->bt_pcigar;
  pcigar_t* const out_i1_bt_pcigar  = wavefront_set->out_i1wavefront->bt_pcigar;
  pcigar_t* const out_d1_bt_pcigar  = wavefront_set->out_d1wavefront->bt_pcigar;
  // Out BT-prev
  block_idx_t* const out_m_bt_prev  = wavefront_set->out_mwavefront->bt_prev;
  block_idx_t* const out_i1_bt_prev = wavefront_set->out_i1wavefront->bt_prev;
  block_idx_t* const out_d1_bt_prev = wavefront_set->out_d1wavefront->bt_prev;
  // Compute-Next kernel loop
  int k;
  PRAGMA_LOOP_VECTORIZE
  for (k=lo;k<=hi;++k) {
    // Update I1
    const wf_offset_t ins1_o = m_open1[k-1];
    const wf_offset_t ins1_e = i1_ext[k-1];
    wf_offset_t ins1;
    if (ins1_e >= ins1_o) { // MAX (predicated by the compiler)
      ins1 = ins1_e + 1;
      out_i1_bt_pcigar[k] = PCIGAR_PUSH_BACK_INS(i1_ext_bt_pcigar[k-1]);
      out_i1_bt_prev[k] = i1_ext_bt_prev[k-1];
    } else {
      ins1 = ins1_o + 1;
      out_i1_bt_pcigar[k] = PCIGAR_PUSH_BACK_INS(m_open1_bt_pcigar[k-1]);
      out_i1_bt_prev[k] = m_open1_bt_prev[k-1];
    }
    out_i1[k] = ins1;
    // Update D1
    const wf_offset_t del1_o = m_open1[k+1];
    const wf_offset_t del1_e = d1_ext[k+1];
    wf_offset_t del1;
    if (del1_e >= del1_o) { // MAX (predicated by the compiler)
      del1 = del1_e;
      out_d1_bt_pcigar[k] = PCIGAR_PUSH_BACK_DEL(d1_ext_bt_pcigar[k+1]);
      out_d1_bt_prev[k] = d1_ext_bt_prev[k+1];
    } else {
      del1 = del1_o;
      out_d1_bt_pcigar[k] = PCIGAR_PUSH_BACK_DEL(m_open1_bt_pcigar[k+1]);
      out_d1_bt_prev[k] = m_open1_bt_prev[k+1];
    }
    out_d1[k] = del1;
    // Update M
    const wf_offset_t sub = m_sub[k] + 1;
    const wf_offset_t max = MAX(del1,MAX(sub,ins1));
    if (max == ins1) {
      out_m_bt_pcigar[k] = out_i1_bt_pcigar[k];
      out_m_bt_prev[k] = out_i1_bt_prev[k];
    }
    if (max == del1) {
      out_m_bt_pcigar[k] = out_d1_bt_pcigar[k];
      out_m_bt_prev[k] = out_d1_bt_prev[k];
    }
    if (max == sub) {
      out_m_bt_pcigar[k] = m_sub_bt_pcigar[k];
      out_m_bt_prev[k] = m_sub_bt_prev[k];
    }
    // Coming from I/D -> X is fake to represent gap-close
    // Coming from M -> X is real to represent mismatch
    out_m_bt_pcigar[k] = PCIGAR_PUSH_BACK_MISMS(out_m_bt_pcigar[k]);
    out_m[k] = max;
  }
  // Offload backtrace
  wavefront_compute_affine_idm_piggyback_offload(wavefront_set,lo,hi,bt_buffer);
}
/*
 * Compute next wavefront
 */
void wavefront_compute_affine(
    wavefront_aligner_t* const wf_aligner,
    const int score) {
  // Select wavefronts
  wavefront_set_t wavefront_set;
  wavefront_aligner_fetch_input(wf_aligner,&wavefront_set,score);
  // Check null wavefronts
  if (wavefront_set.in_mwavefront_sub->null &&
      wavefront_set.in_mwavefront_gap1->null &&
      wavefront_set.in_i1wavefront_ext->null &&
      wavefront_set.in_d1wavefront_ext->null) {
    wavefront_aligner_allocate_output_null(wf_aligner,score); // Null s-wavefront
    return;
  }
  // Set limits
  int hi, lo;
  wavefront_compute_limits(&wavefront_set,gap_affine,&lo,&hi);
  // Allocate wavefronts
  wavefront_aligner_allocate_output(wf_aligner,&wavefront_set,score,lo,hi);
  // Init wavefront ends
  wavefront_aligner_init_ends(wf_aligner,&wavefront_set,lo,hi);
  // Compute next wavefront
  if (wf_aligner->wf_components.bt_piggyback) {
    wavefront_compute_affine_idm_piggyback(&wavefront_set,lo,hi,wf_aligner->wf_components.bt_buffer);
  } else {
    wavefront_compute_affine_idm(&wavefront_set,lo,hi);
  }
  // Trim wavefront ends
  wavefront_aligner_trim_ends(wf_aligner,&wavefront_set);
}


