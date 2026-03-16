
/*
// Copyright (c) 2009-2014 Joe Bertolami. All Right Reserved.
//
// scan.h
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

#ifndef __EVX_SCAN_H__
#define __EVX_SCAN_H__

#include "base.h"
#include "config.h"

namespace evx {

// Our scan lists assume a contiguous (no stride) buffer, thus our offsets
// are unpadded for 8x8 and 16x16.

const uint8 EVX_MACROBLOCK_4x4_ZIGZAG[] = 
{    
    0,   1,   4,   8,
    5,   2,   3,   6,
    9,  12,  13,  10,
    7,  11,  14,  15,
};

const uint8 EVX_MACROBLOCK_4x4_ZIGZAG_SOURCE[] = 
{
    0,   1,   5,   6,
    2,   4,   7,  12,
    3,   8,  11,  13,
    9,  10,  14,  15,
};

const uint8 EVX_MACROBLOCK_8x8_ZIGZAG[] = 
{
     0,   1,   8,  16,   9,   2,   3,  10,
    17,  24,  32,  25,  18,  11,   4,   5,
    12,  19,  26,  33,  40,  48,  41,  34, 
    27,  20,  13,  6,    7,  14,  21,  28, 
    35,  42,  49,  56,  57,  50,  43,  36, 
    29,  22,  15,  23,  30,  37,  44,  51, 
    58,  59,  52,  45,  38,  31,  39,  46, 
    53,  60,  61,  54,  47,  55,  62,  63,
};

const uint8 EVX_MACROBLOCK_8x8_ZIGZAG_SOURCE[] = 
{
     0,   1,    5,    6,   14,   15,   27,   28,
     2,   4,    7,   13,   16,   26,   29,   42,
     3,   8,   12,   17,   25,   30,   41,   43,
     9,  11,   18,   24,   31,   40,   44,   53,
    10,  19,   23,   32,   39,   45,   52,   54,
    20,  22,   33,   38,   46,   51,   55,   60,
    21,  34,   37,   47,   50,   56,   59,   61,
    35,  36,   48,   49,   57,   58,   62,   63
};

const uint8 EVX_MACROBLOCK_16x16_ZIGZAG[] = 
{
    0,    1,   16,   32,   17,    2,    3,   18,   33,   48,   64,   49,   34,   19,    4,    5,
   20,   35,   50,   65,   80,   96,   81,   66,   51,   36,   21,    6,    7,   22,   37,   52,
   67,   82,   97,  112,  113,   98,   83,   68,   53,   38,   23,   39,   54,   69,   84,   99,
  114,  115,  100,   85,   70,   55,   71,   86,  101,  116,  117,  102,   87,  103,  118,  119,
    8,    9,   24,   40,   25,   10,   11,   26,   41,   56,   72,   57,   42,   27,   12,   13,
   28,   43,   58,   73,   88,  104,   89,   74,   59,   44,   29,   14,   15,   30,   45,   60,
   75,   90,  105,  120,  121,  106,   91,   76,   61,   46,   31,   47,   62,   77,   92,  107,
  122,  123,  108,   93,   78,   63,   79,   94,  109,  124,  125,  110,   95,  111,  126,  127,
  128,  129,  144,  160,  145,  130,  131,  146,  161,  176,  192,  177,  162,  147,  132,  133,
  148,  163,  178,  193,  208,  224,  209,  194,  179,  164,  149,  134,  135,  150,  165,  180,
  195,  210,  225,  240,  241,  226,  211,  196,  181,  166,  151,  167,  182,  197,  212,  227,
  242,  243,  228,  213,  198,  183,  199,  214,  229,  244,  245,  230,  215,  231,  246,  247,
  136,  137,  152,  168,  153,  138,  139,  154,  169,  184,  200,  185,  170,  155,  140,  141,
  156,  171,  186,  201,  216,  232,  217,  202,  187,  172,  157,  142,  143,  158,  173,  188,
  203,  218,  233,  248,  249,  234,  219,  204,  189,  174,  159,  175,  190,  205,  220,  235,
  250,  251,  236,  221,  206,  191,  207,  222,  237,  252,  253,  238,  223,  239,  254,  255
};

const uint8 EVX_MACROBLOCK_16x16_ZIGZAG_SOURCE[] =
{
    0,    1,    5,    6,   14,   15,   27,   28,   64,   65,   69,   70,   78,   79,   91,   92,
    2,    4,    7,   13,   16,   26,   29,   42,   66,   68,   71,   77,   80,   90,   93,  106,
    3,    8,   12,   17,   25,   30,   41,   43,   67,   72,   76,   81,   89,   94,  105,  107,
    9,   11,   18,   24,   31,   40,   44,   53,   73,   75,   82,   88,   95,  104,  108,  117,
   10,   19,   23,   32,   39,   45,   52,   54,   74,   83,   87,   96,  103,  109,  116,  118,
   20,   22,   33,   38,   46,   51,   55,   60,   84,   86,   97,  102,  110,  115,  119,  124,
   21,   34,   37,   47,   50,   56,   59,   61,   85,   98,  101,  111,  114,  120,  123,  125,
   35,   36,   48,   49,   57,   58,   62,   63,   99,  100,  112,  113,  121,  122,  126,  127,
  128,  129,  133,  134,  142,  143,  155,  156,  192,  193,  197,  198,  206,  207,  219,  220,
  130,  132,  135,  141,  144,  154,  157,  170,  194,  196,  199,  205,  208,  218,  221,  234,
  131,  136,  140,  145,  153,  158,  169,  171,  195,  200,  204,  209,  217,  222,  233,  235,
  137,  139,  146,  152,  159,  168,  172,  181,  201,  203,  210,  216,  223,  232,  236,  245,
  138,  147,  151,  160,  167,  173,  180,  182,  202,  211,  215,  224,  231,  237,  244,  246,
  148,  150,  161,  166,  174,  179,  183,  188,  212,  214,  225,  230,  238,  243,  247,  252,
  149,  162,  165,  175,  178,  184,  187,  189,  213,  226,  229,  239,  242,  248,  251,  253,
  163,  164,  176,  177,  185,  186,  190,  191,  227,  228,  240,  241,  249,  250,  254,  255,
};

// Horizontal scan (row-first) for residuals after vertical prediction.
const uint8 EVX_MACROBLOCK_8x8_HSCAN[] =
{
     0,  1,  2,  3,  4,  5,  6,  7,
     8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39,
    40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63,
};

// Vertical scan (column-first) for residuals after horizontal prediction.
const uint8 EVX_MACROBLOCK_8x8_VSCAN[] =
{
     0,  8, 16, 24, 32, 40, 48, 56,
     1,  9, 17, 25, 33, 41, 49, 57,
     2, 10, 18, 26, 34, 42, 50, 58,
     3, 11, 19, 27, 35, 43, 51, 59,
     4, 12, 20, 28, 36, 44, 52, 60,
     5, 13, 21, 29, 37, 45, 53, 61,
     6, 14, 22, 30, 38, 46, 54, 62,
     7, 15, 23, 31, 39, 47, 55, 63,
};

// Select scan table based on intra prediction mode.
// Vertical prediction -> horizontal scan (residual energy is horizontal).
// Horizontal prediction -> vertical scan (residual energy is vertical).
// All others -> zigzag.
inline const uint8 *select_scan_table_8x8(uint8 intra_mode)
{
    if (intra_mode == 1 || intra_mode == 5) return EVX_MACROBLOCK_8x8_HSCAN;  // vert/vert-r pred -> horiz scan
    if (intra_mode == 2 || intra_mode == 6) return EVX_MACROBLOCK_8x8_VSCAN;  // horiz/horiz-d pred -> vert scan
    return EVX_MACROBLOCK_8x8_ZIGZAG;
}

} // namespace evx

#endif // __EVX_SCAN_H__