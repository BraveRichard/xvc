/******************************************************************************
* Copyright (C) 2017, Divideon.
*
* Redistribution and use in source and binary form, with or without
* modifications is permitted only under the terms and conditions set forward
* in the xvc License Agreement. For commercial redistribution and use, you are
* required to send a signed copy of the xvc License Agreement to Divideon.
*
* Redistribution and use in source and binary form is permitted free of charge
* for non-commercial purposes. See definition of non-commercial in the xvc
* License Agreement.
*
* All redistribution of source code must retain this copyright notice
* unmodified.
*
* The xvc License Agreement is available at https://xvc.io/license/.
******************************************************************************/

#ifndef XVC_COMMON_LIB_INTER_PREDICTION_H_
#define XVC_COMMON_LIB_INTER_PREDICTION_H_

#include <array>

#include "xvc_common_lib/common.h"
#include "xvc_common_lib/coding_unit.h"
#include "xvc_common_lib/sample_buffer.h"

namespace xvc {

typedef std::array<MotionVector,
  constants::kNumInterMvPredictors> InterPredictorList;

struct MergeCandidate {
  InterDir inter_dir = InterDir::kL0;
  std::array<MotionVector, static_cast<int>(RefPicList::kTotalNumber)> mv;
  std::array<int, static_cast<int>(RefPicList::kTotalNumber)> ref_idx = { {0} };
};

struct InterMergeCandidateList
  : public std::array<MergeCandidate, constants::kNumInterMergeCandidates> {
  int num = 0;
};

class InterPrediction {
public:
  static const int kNumTapsLuma = 8;
  static const int kNumTapsChroma = 4;
  static const int kInternalPrecision = 14;
  static const int kFilterPrecision = 6;
  static const int kInternalOffset = 1 << (kInternalPrecision - 1);
  static const int kMergeLevelShift = 2;
  struct SimdFunc;

  InterPrediction(const SimdFunc &simd, int bitdepth)
    : simd_(simd),
    bitdepth_(bitdepth) {
  }

  InterPredictorList GetMvPredictors(const CodingUnit &cu, RefPicList ref_list,
                                     int ref_idx);
  InterMergeCandidateList GetMergeCandidates(const CodingUnit &cu,
                                             int merge_cand_idx = -1);
  void CalculateMV(CodingUnit *cu);
  void ApplyMerge(CodingUnit *cu, const MergeCandidate &merge_cand);
  void MotionCompensation(const CodingUnit &cu, YuvComponent comp,
                          Sample *pred_ptr, ptrdiff_t pred_stride);
  void ClipMV(const CodingUnit &cu, const YuvPicture &ref_pic,
              int *mv_x, int *mv_y) const;
  void DetermineMinMaxMv(const CodingUnit &cu, const YuvPicture &ref_pic,
                         int center_x, int center_y, int search_range,
                         MotionVector *mv_min, MotionVector *mv_max) const;
  template<typename SrcT, bool Clip>
  static int GetFilterShift(int bitdepth);
  template<typename SrcT, bool Clip>
  static int GetFilterOffset(int shift);

protected:
  void MotionCompensationMv(const CodingUnit &cu, YuvComponent comp,
                            const YuvPicture &ref_pic, int mv_x, int mv_y,
                            Sample *pred, ptrdiff_t pred_stride);

private:
  static const int kBufSize = constants::kMaxBlockSize *
    (constants::kMaxBlockSize + kNumTapsLuma - 1);
  static const std::array<std::array<int16_t, kNumTapsLuma>, 4> kLumaFilter;
  static const std::array<std::array<int16_t, kNumTapsChroma>, 8> kChromaFilter;
  static const std::array<std::array<uint8_t, 2>, 12> kMergeCandL0L1Idx;
  static void ScaleMv(PicNum poc_current1, PicNum poc_ref1, PicNum poc_current2,
                      PicNum poc_ref2, MotionVector *out);

  bool GetMvpCand(const CodingUnit *cu, RefPicList ref_list, int ref_idx,
                  PicNum ref_poc, MotionVector *mv_out);
  bool GetScaledMvpCand(const CodingUnit *cu, RefPicList cu_ref_list,
                        int ref_idx, MotionVector *mv_out);
  bool GetTemporalMvPredictor(const CodingUnit &cu, RefPicList ref_list,
                              int ref_idx, MotionVector *mv_out);
  void MotionCompensationBi(const CodingUnit &cu, YuvComponent comp,
                            const YuvPicture &ref_pic, const MotionVector &mv,
                            int16_t *pred, ptrdiff_t pred_stride);
  DataBuffer<const Sample>
    GetFullpelRef(const CodingUnit &cu, YuvComponent comp,
                  const YuvPicture &ref_pic, int mv_x, int mv_y,
                  int *frac_x, int *frac_y);
  void FilterLuma(int width, int height, int frac_x, int frac_y,
                  const Sample *ref, ptrdiff_t ref_stride,
                  Sample *pred, ptrdiff_t pred_stride);
  void FilterChroma(int width, int height, int frac_x, int frac_y,
                    const Sample *ref, ptrdiff_t ref_stride,
                    Sample *pred, ptrdiff_t pred_stride);
  void FilterLumaBipred(int width, int height, int frac_x, int frac_y,
                        const Sample *ref, ptrdiff_t ref_stride,
                        int16_t *pred, ptrdiff_t pred_stride);
  void FilterChromaBipred(int width, int height, int frac_x, int frac_y,
                          const Sample *ref, ptrdiff_t ref_stride,
                          int16_t *pred, ptrdiff_t pred_stride);
  void AddAvgBi(const CodingUnit &cu, YuvComponent comp,
                const int16_t *src_l0, ptrdiff_t src_l0_stride,
                const int16_t *src_l1, ptrdiff_t src_l1_stride,
                Sample *pred, ptrdiff_t pred_stride);
  MergeCandidate GetMergeCandidateFromCu(const CodingUnit &cu);

  const InterPrediction::SimdFunc &simd_;
  std::array<int16_t, kBufSize> filter_buffer_;
  std::array<std::array<int16_t, constants::kMaxBlockSamples>, 2> bipred_temp_;
  int bitdepth_;
};

struct InterPrediction::SimdFunc {
  // 0: width <= 2, 1: width >= 4
  static const int kSize = 2;

  // 0: luma, 1: chroma
  static const int kLC = 2;

  SimdFunc();
  void(*add_avg[kSize])(int width, int height, int offset, int shift,
                        int bitdepth, const int16_t *src1, intptr_t stride1,
                        const int16_t *src2, intptr_t stride2,
                        Sample *dst, intptr_t dst_stride);
  void(*filter_copy_bipred[kSize])(int width, int height,
                                   int16_t offset, int shift,
                                   const Sample *ref, ptrdiff_t ref_stride,
                                   int16_t *pred, ptrdiff_t pred_stride);
  void(*filter_h_sample_sample[kLC])(int width, int height, int bitdepth,
                                     const int16_t *filter,
                                     const Sample *src, ptrdiff_t src_stride,
                                     Sample *dst, ptrdiff_t dst_stride);
  void(*filter_h_sample_short[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const Sample *src, ptrdiff_t src_stride,
                                    int16_t *dst, ptrdiff_t dst_stride);
  void(*filter_v_sample_sample[kLC])(int width, int height, int bitdepth,
                                     const int16_t *filter,
                                     const Sample *src, ptrdiff_t src_stride,
                                     Sample *dst, ptrdiff_t dst_stride);
  void(*filter_v_sample_short[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const Sample *src, ptrdiff_t src_stride,
                                    int16_t *dst, ptrdiff_t dst_stride);
  void(*filter_v_short_sample[kLC])(int width, int height, int bitdepth,
                                    const int16_t *filter,
                                    const int16_t *src, ptrdiff_t src_stride,
                                    Sample *dst, ptrdiff_t dst_stride);
  void(*filter_v_short_short[kLC])(int width, int height, int bitdepth,
                                   const int16_t *filter,
                                   const int16_t *src, ptrdiff_t src_stride,
                                   int16_t *dst, ptrdiff_t dst_stride);
};

template<>
inline int InterPrediction::GetFilterShift<int16_t, true>(int bitdepth) {
  const int head_room = InterPrediction::kInternalPrecision - bitdepth;
  return InterPrediction::kFilterPrecision + head_room;
}
template<>
inline int InterPrediction::GetFilterShift<int16_t, false>(int bitdepth) {
  return InterPrediction::kFilterPrecision;
}
template<>
inline int InterPrediction::GetFilterShift<Sample, false>(int bitdepth) {
  const int head_room = InterPrediction::kInternalPrecision - bitdepth;
  return InterPrediction::kFilterPrecision - head_room;
}
template<>
inline int InterPrediction::GetFilterShift<Sample, true>(int bitdepth) {
  return InterPrediction::kFilterPrecision;
}

template<>
inline int InterPrediction::GetFilterOffset<int16_t, true>(int shift) {
  return
    (InterPrediction::kInternalOffset << InterPrediction::kFilterPrecision) +
    (1 << (shift - 1));
}
template<>
inline int InterPrediction::GetFilterOffset<int16_t, false>(int shift) {
  return 0;
}
template<>
inline int InterPrediction::GetFilterOffset<Sample, false>(int shift) {
  return -(InterPrediction::kInternalOffset << shift);
}
template<>
inline int InterPrediction::GetFilterOffset<Sample, true>(int shift) {
  return 1 << (shift - 1);
}

}   // namespace xvc

#endif  // XVC_COMMON_LIB_INTER_PREDICTION_H_
