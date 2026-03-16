
/*
// Copyright (c) 2002-2014 Joe Bertolami. All Right Reserved.
//
// convert.h
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_CONVERT_H__
#define __EVX_CONVERT_H__

#include "base.h"
#include "evx_math.h"
#include "image.h"
#include "imageset.h"

// YUV is a family of color spaces including NTSC/PAL YUV, Digital YCbCr, 
// YPbPr, etc. Imagine uses YCbCr 4:2:0 exclusively, although it is referred
// to simply as YUV throughout the code.

namespace evx {

// convert_image (RGB to YUV)
//
//   ConvertImage performs RGB to YUV420P image space conversion. If the destination
//   image is smaller than the source image, then automatic cropping will occur.
//
// Supported Image Formats: 
//    
//   The source image must be R8G8B8.
//   The destination image should be R16S in order to avoid format conversion.

evx_status convert_image(const image &src_rgb, image *dest_y, image *dest_u, image *dest_v);
evx_status convert_image(const image &src_rgb, image_set *dest_yuv);

// convert_image (YUV to RGB)
//
//   ConvertImage performs YUV420P to RGB image space conversion. If the destination
//   image is smaller than the source image, then automatic cropping will occur.
//
// Supported Image Formats: 
//    
//   The source image must be R16S.
//   The destination image should be R8G8B8 in order to avoid format conversion.

evx_status convert_image(const image &src_y, const image &src_u, const image &src_v, image *dest_rgb);
evx_status convert_image(const image_set &src_yuv, image *dest_rgb);

} // namespace evx

#endif // __EVX_CONVERT_H__
