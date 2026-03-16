
/*
// Copyright (c) 2002-2014 Joe Bertolami. All Right Reserved.
//
// imageset.h
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

#ifndef __EVX_IMAGE_SET_H__
#define __EVX_IMAGE_SET_H__

#include "base.h"
#include "evx_math.h"
#include "image.h"

namespace evx {
 
// this is a helper class that encapsulates three image objects
// to produce a single YCbCr 420p image.

class image_set
{
    image y_plane;
    image u_plane;
    image v_plane;

public:

    image_set();		
    virtual ~image_set();	
  
    // format must be R8 or R16S.
    evx_status initialize(EVX_IMAGE_FORMAT format, uint16 width, uint16 height);
    evx_status deinitialize();

    // Creates a lightweight copy that references src's backing memory via
    // placement allocation. Borrowed planes are not freed on destruction.
    evx_status borrow(const image_set &src);
 
public:

    image *query_y_image() const;
    image *query_u_image() const;
    image *query_v_image() const;

    EVX_IMAGE_FORMAT query_image_format() const;

    uint16 query_width() const;
    uint16 query_height()  const;
};

} // namespace evx

#endif // __EVX_IMAGE_SET_H__
