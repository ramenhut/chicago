#include <gtest/gtest.h>
#include <cstring>
#include "video/interp.h"
#include "video/image.h"
#include "video/imageset.h"
#include "video/macroblock.h"
#include "video/config.h"

using namespace evx;

#if EVX_ENABLE_6TAP_INTERPOLATION

class InterpTest : public ::testing::Test {
protected:
    image_set ref;
    // Output macroblock backing storage.
    static const uint32 MB_STRIDE = EVX_MACROBLOCK_SIZE;
    static const uint32 LUMA_COUNT = MB_STRIDE * EVX_MACROBLOCK_SIZE;
    static const uint32 CHROMA_STRIDE = MB_STRIDE >> 1;
    static const uint32 CHROMA_COUNT = CHROMA_STRIDE * (EVX_MACROBLOCK_SIZE >> 1);

    int16 *out_y, *out_u, *out_v;
    macroblock output;

    void SetUp() override {
        // Create a 64x64 R16S image set.
        ref.initialize(EVX_IMAGE_FORMAT_R16S, 64, 64);

        out_y = new int16[LUMA_COUNT]();
        out_u = new int16[CHROMA_COUNT]();
        out_v = new int16[CHROMA_COUNT]();
        output.data_y = out_y;
        output.data_u = out_u;
        output.data_v = out_v;
        output.stride = MB_STRIDE;
    }

    void TearDown() override {
        ref.deinitialize();
        delete[] out_y;
        delete[] out_u;
        delete[] out_v;
    }

    void fillRefConstant(int16 value) {
        image *y_img = ref.query_y_image();
        image *u_img = ref.query_u_image();
        image *v_img = ref.query_v_image();

        int16 *y_data = reinterpret_cast<int16 *>(y_img->query_data());
        int16 *u_data = reinterpret_cast<int16 *>(u_img->query_data());
        int16 *v_data = reinterpret_cast<int16 *>(v_img->query_data());

        uint32 y_stride = y_img->query_row_pitch() / sizeof(int16);
        uint32 c_stride = u_img->query_row_pitch() / sizeof(int16);

        for (uint32 j = 0; j < y_img->query_height(); j++)
        for (uint32 i = 0; i < y_img->query_width(); i++)
            y_data[j * y_stride + i] = value;

        for (uint32 j = 0; j < u_img->query_height(); j++)
        for (uint32 i = 0; i < u_img->query_width(); i++) {
            u_data[j * c_stride + i] = value;
            v_data[j * c_stride + i] = value;
        }
    }

    void fillRefHorizontalRamp() {
        image *y_img = ref.query_y_image();
        int16 *y_data = reinterpret_cast<int16 *>(y_img->query_data());
        uint32 y_stride = y_img->query_row_pitch() / sizeof(int16);

        for (uint32 j = 0; j < y_img->query_height(); j++)
        for (uint32 i = 0; i < y_img->query_width(); i++)
            y_data[j * y_stride + i] = (int16)i * 4;
    }
};

TEST_F(InterpTest, HorizontalHalfPelConstant) {
    // For a constant image, the 6-tap filter should produce the same constant.
    // [1 - 5 + 20 + 20 - 5 + 1] / 32 = 32/32 = 1.0x for constant input.
    fillRefConstant(100);

    interpolate_subpixel_6tap(ref, 16, 16, 1, 0, false, &output);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        EXPECT_EQ(output.data_y[j * output.stride + i], 100)
            << "Horizontal half-pel of constant should be constant at (" << i << "," << j << ")";
    }
}

TEST_F(InterpTest, VerticalHalfPelConstant) {
    fillRefConstant(80);

    interpolate_subpixel_6tap(ref, 16, 16, 0, 1, false, &output);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        EXPECT_EQ(output.data_y[j * output.stride + i], 80)
            << "Vertical half-pel of constant should be constant at (" << i << "," << j << ")";
    }
}

TEST_F(InterpTest, DiagonalHalfPelConstant) {
    fillRefConstant(60);

    interpolate_subpixel_6tap(ref, 16, 16, 1, 1, false, &output);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        EXPECT_EQ(output.data_y[j * output.stride + i], 60)
            << "Diagonal half-pel of constant should be constant at (" << i << "," << j << ")";
    }
}

TEST_F(InterpTest, QuarterPelAveragesCorrectly) {
    // Quarter-pel = average of full-pel and half-pel.
    // For constant input, both should be 100, so quarter-pel should be 100.
    fillRefConstant(100);

    interpolate_subpixel_6tap(ref, 16, 16, 1, 0, true, &output);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++)
    for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
        EXPECT_EQ(output.data_y[j * output.stride + i], 100)
            << "Quarter-pel of constant should be constant at (" << i << "," << j << ")";
    }
}

TEST_F(InterpTest, BoundaryClamping) {
    // Place block at edge of image — should not crash.
    fillRefConstant(50);

    // Near top-left corner: base at (0,0), dir right.
    interpolate_subpixel_6tap(ref, 0, 0, 1, 0, false, &output);
    // Just verify it doesn't crash and produces reasonable values.
    EXPECT_GE(output.data_y[0], 0);

    // Near bottom-right: base at max valid position.
    int32 max_x = ref.query_width() - EVX_MACROBLOCK_SIZE;
    int32 max_y = ref.query_height() - EVX_MACROBLOCK_SIZE;
    interpolate_subpixel_6tap(ref, max_x, max_y, -1, -1, false, &output);
    EXPECT_GE(output.data_y[0], 0);
}

TEST_F(InterpTest, HorizontalRampProducesIntermediateValues) {
    // A horizontal ramp (pixel value = x*4) should produce half-pel values
    // between adjacent integer positions.
    fillRefHorizontalRamp();

    interpolate_subpixel_6tap(ref, 16, 16, 1, 0, false, &output);

    // For a linear ramp, the 6-tap filter should produce the exact midpoint
    // since the filter is symmetric and the input is linear.
    image *y_img = ref.query_y_image();
    int16 *y_data = reinterpret_cast<int16 *>(y_img->query_data());
    uint32 y_stride = y_img->query_row_pitch() / sizeof(int16);

    for (int j = 0; j < EVX_MACROBLOCK_SIZE; j++) {
        for (int i = 0; i < EVX_MACROBLOCK_SIZE; i++) {
            int16 left = y_data[(16 + j) * y_stride + (16 + i)];
            int16 right = y_data[(16 + j) * y_stride + (16 + i + 1)];
            int16 expected = (left + right + 1) >> 1;
            // Allow small rounding difference from 6-tap vs simple average.
            EXPECT_NEAR(output.data_y[j * output.stride + i], expected, 1)
                << "Ramp half-pel at (" << i << "," << j << ")";
        }
    }
}

#endif // EVX_ENABLE_6TAP_INTERPOLATION
