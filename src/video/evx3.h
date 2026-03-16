
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// evx3.h
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
//   * You must inform the original author of your initial use, incorporation, or 
//     redistribution of this software, whether in source or binary form.
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
// Description:
//
//   EVX-1 is a branch of the P.264 video codec that was developed from 2009-2010 
//   as part of the everyAir mobile cloud gaming project.
//
//   Unlike P.264, which focused on efficient low latency video compression, EVX-1 
//   was designed as a basic platform for experimentation. As a result, EVX-1 is 
//   slow, lacks many modern features, and has limited platform support. In exchange 
//   for these limitations however, EVX-1 is significantly easier to tweak, analyze,
//   and debug than its predecessor.  
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX3_H__
#define __EVX3_H__

#include "base.h"
#include "bitstream.h"
#include "config.h"

namespace evx {

enum EVX_PEEK_STATE
{
    EVX_PEEK_SOURCE = 0,        // The padded input image in YUV420P.
    EVX_PEEK_PREDICTION,        // Initial prediction state.
    EVX_PEEK_BLOCK_TABLE,       // Color coded image representing the block table
    EVX_PEEK_QUANT_TABLE,       // Color coded image representing the quant levels
    EVX_PEEK_SPMP_TABLE,        // Sub-pixel motion prediction table
    EVX_PEEK_BLOCK_VARIANCE,    // Block variance level
    EVX_PEEK_DESTINATION,       // Updated prediction state.
};

class evx3_encoder 
{

protected: 
    
    virtual ~evx3_encoder() {}

public:

    // Clears the state of the encoder. Use this to reset the state 
    // of the encoder without having to recreate it.
    virtual evx_status clear() = 0;

    // Inserts a new intra frame into the stream. You may want to do
    // this when recovering from dropped packets or to enable seekability.
    virtual evx_status insert_intra() = 0;

    // Sets a quality level between 0-31, with 0 being the highest quality.
    virtual evx_status set_quality(uint8 quality) = 0;
     
    // The input image must contain R8G8B8 formatted data. Upon return, output will
    // contain the encoded frame. Note that this engine does not provide a container
    // for video serialization.
    virtual evx_status encode(void *image, uint32 width, uint32 height, bit_stream *output) = 0;

#if EVX_ENABLE_B_FRAMES
    // Flushes any buffered frames from the reorder buffer. Call this at end of stream.
    virtual evx_status flush(bit_stream *output) = 0;
#endif

    // Debug routine that enables visibility into the internal encoder state. This is
    // a very expensive operation that should only be used in testing.
    virtual evx_status peek(EVX_PEEK_STATE peek_state, void *output) = 0;
};

class evx3_decoder
{

protected: 
    
    virtual ~evx3_decoder() {}

public:

    // Clears the state of the decoder. Use this to reset the state 
    // of the decoder without having to recreate it.
    virtual evx_status clear() = 0;

    // Decodes the contents of input and places the resulting frame within the output.
    // buffer. The caller is responsible for ensuring sufficient size at the output,
    // using frame dimensions provided by a container format.
    virtual evx_status decode(bit_stream *input, void *output) = 0;

#if EVX_ENABLE_B_FRAMES
    // Flushes the next available frame from the display reorder buffer. Call this
    // at end of stream while has_output() returns true to drain remaining frames.
    virtual evx_status flush(void *output) = 0;

    // Returns true when the display reorder buffer has a frame ready to present.
    virtual bool has_output() = 0;
#endif
};

// EV objects must be created using the create* interface. Similarly, 
// these objects must be destroyed using destroy*.

evx_status create_encoder(evx3_encoder **output);
evx_status create_decoder(evx3_decoder **output);

evx_status destroy_encoder(evx3_encoder *input);
evx_status destroy_decoder(evx3_decoder *input);

} // namespace evx

#endif // __EVX3_H__