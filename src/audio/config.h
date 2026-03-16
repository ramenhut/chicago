
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// audio_config.h
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

#ifndef __EVX_AUDIO_CONFIG_H__
#define __EVX_AUDIO_CONFIG_H__

// MDCT window and hop parameters
#define EVX_AUDIO_WINDOW_SIZE                                   (1024)
#define EVX_AUDIO_HOP_SIZE                                      (512)
#define EVX_AUDIO_FREQ_COEFFS                                   (512)

// Quality parameters
#define EVX_AUDIO_MAX_QUALITY                                   (31)
#define EVX_AUDIO_DEFAULT_QUALITY                               (8)

// Channel limits
#define EVX_AUDIO_MAX_CHANNELS                                  (2)

// Bark-scale critical band count
#define EVX_AUDIO_BARK_BAND_COUNT                               (25)

// Entropy coding context groups (frequency bands)
#define EVX_AUDIO_ENTROPY_BAND_COUNT                            (8)

// Feature flags
#define EVX_AUDIO_ENABLE_MID_SIDE                               (1)
#define EVX_AUDIO_ENABLE_DELTA_DC                               (1)

#endif // __EVX_AUDIO_CONFIG_H__
